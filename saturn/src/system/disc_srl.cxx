#include <srl.hpp>
#include "disc_manifest.h"
#include "saturn_compat.h"

#include "disc.h"
#include "discsec.h"
#include "discfmt.h"
#include "cdtoc.h"
#include "cdda_classify.h"
#include "debug.h"
#include "client.h"
#include "platform.h"

static bool g_discOpened = false;

static uint32_t g_toc[CDTOC_WORDS];
static int g_maxAudioTrack = 0;

static int g_musicTrack = -1;
static int g_musicLoop = 0;

static unsigned int g_musicStartedAt = 0;
static uint32_t g_musicLengthMs = 0;

static uint32_t g_pauseFad = 0;

static bool g_musicPaused = false;
static bool g_wasPlaying = false;

static bool g_musicObserved = false;

static uint32_t g_tailSector[DISC_MAX_SECTOR_BYTES / 4];

static bool normalize_name(const char *name, char *out, int32_t outSize)
{
	if (name == nullptr || out == nullptr || outSize < 2)
	{
		return false;
	}

	const char *base = name;

	for (const char *p = name; *p != '\0'; p++)
	{
		if (*p == '/' || *p == '\\')
		{
			base = p + 1;
		}
	}

	int32_t n = 0;
	bool hasDot = false;

	while (base[n] != '\0' && n < outSize - 2)
	{
		char c = base[n];

		if (c >= 'a' && c <= 'z')
		{
			c = (char)(c - 'a' + 'A');
		}

		if (c == '.')
		{
			hasDot = true;
		}

		out[n] = c;
		n++;
	}

	if (n == 0)
	{
		return false;
	}

	if (!hasDot)
	{
		out[n++] = '.';
	}

	out[n] = '\0';
	return true;
}

extern "C" {

#define CDDA_SOUND_CAP_MS 3000

#define CDDA_AUDIBLE_SECTORS 4u

#define DISC_BOUNCE_SECTORS 16

static uint32_t *g_bounce = nullptr;

static uint32_t *bounce_acquire(void)
{
	if (g_bounce == nullptr)
	{
		void *raw = saturn_lwram_alloc(DISC_BOUNCE_SECTORS * DISC_MAX_SECTOR_BYTES + 4);

		if (raw != nullptr)
		{
			g_bounce = (uint32_t *)(((uintptr_t)raw + 3u) & ~(uintptr_t)3u);
		}
	}

	return g_bounce;
}

static disc_tick_fn g_tick = nullptr;

void disc_set_tick(disc_tick_fn tick)
{
	g_tick = tick;
}

static void disc_tick(void)
{
	if (g_tick != nullptr)
	{
		g_tick();
	}
}

static void cdda_wait_for_sound(void)
{
	unsigned int t0 = platform_ticks();
	int cue = discfmt_cue_track_for_music(g_musicTrack);
	uint32_t start = cdtoc_track_start(g_toc, cue);
	uint32_t end = cdtoc_track_end(g_toc, cue);
	uint32_t base = 0;
	int baseSet = 0;

	for (;;)
	{
		CdcStat stat;
		uint32_t fad;

		disc_tick();
		platform_frame();

		if (platform_ticks() - t0 >= CDDA_SOUND_CAP_MS)
		{
			return;
		}

		CDC_GetCurStat(&stat);

		if (CDC_GET_STC(&stat) != CDC_ST_PLAY)
		{
			baseSet = 0;
			continue;
		}

		fad = (uint32_t)CDC_STAT_FAD(&stat);

		if (end != 0 && (fad < start || fad >= end))
		{
			baseSet = 0;
			continue;
		}

		if (!baseSet || fad < base)
		{
			base = fad;
			baseSet = 1;
			continue;
		}

		if (fad - base >= CDDA_AUDIBLE_SECTORS)
		{
			return;
		}
	}
}

static void cdda_halt(void)
{
	CdcPos pos;

	CDC_POS_PTYPE(&pos) = CDC_PTYPE_DFL;
	CDC_CdSeek(&pos);
}

static uint32_t cdda_track_length_ms(int cue)
{
	uint32_t start = cdtoc_track_start(g_toc, cue);
	uint32_t end = cdtoc_track_end(g_toc, cue);

	if (end == 0 || end <= start)
	{
		return 0;
	}

	return ((end - start) * 1000u) / 75u;
}

static void cdda_play_range(int cue, uint32_t fromFad, int loop)
{
	CdcPly ply;
	uint32_t start = cdtoc_track_start(g_toc, cue);
	uint32_t end = cdtoc_track_end(g_toc, cue);

	if (end == 0 || end <= start || fromFad < start || fromFad >= end)
	{
		SRL::Sound::Cdda::PlaySingle((uint16_t)cue, loop != 0);
		return;
	}

	CDC_PLY_STYPE(&ply) = CDC_PTYPE_FAD;
	CDC_PLY_SFAD(&ply) = fromFad;
	CDC_PLY_ETYPE(&ply) = CDC_PTYPE_FAD;
	CDC_PLY_EFAS(&ply) = end - fromFad;
	CDC_PLY_PMODE(&ply) = (uint8_t)(CDC_PM_DFL | (loop ? 0xf : 0));
	CDC_CdPlay(&ply);
}

static void cdda_suspend(void)
{
	CdcStat stat;

	if (g_musicTrack < 0)
	{
		return;
	}

	CDC_GetCurStat(&stat);
	g_wasPlaying = (CDC_GET_STC(&stat) == CDC_ST_PLAY);
	g_pauseFad = (uint32_t)CDC_STAT_FAD(&stat);

	cdda_halt();
}

static void cdda_restore(void)
{
	int cue;
	uint32_t start;
	uint32_t end;
	cdda_action action;

	if (g_musicTrack < 0)
	{
		return;
	}

	cue = discfmt_cue_track_for_music(g_musicTrack);
	start = cdtoc_track_start(g_toc, cue);
	end = cdtoc_track_end(g_toc, cue);

	if (g_wasPlaying && end != 0 && g_pauseFad >= start && g_pauseFad < end)
	{
		g_musicObserved = true;
	}

	action = cdda_classify(g_wasPlaying ? 1 : 0, g_musicLoop,
		g_musicObserved ? 1 : 0, g_pauseFad, start, end);

	switch (action)
	{
	case CDDA_FORGET:
		printf("cdda_restore: restore classified as finished or never-started\n");
		g_musicTrack = -1;
		return;

	case CDDA_RESUME:
	{
		CdcPly ply;

		CDC_PLY_STYPE(&ply) = CDC_PTYPE_FAD;
		CDC_PLY_SFAD(&ply) = g_pauseFad;
		CDC_PLY_ETYPE(&ply) = CDC_PTYPE_FAD;
		CDC_PLY_EFAS(&ply) = end - g_pauseFad;
		CDC_PLY_PMODE(&ply) = CDC_PM_DFL;
		CDC_CdPlay(&ply);
		cdda_wait_for_sound();
		return;
	}

	case CDDA_RESTART:
	default:
		if (!cdtoc_is_audio(g_toc, cue))
		{
			printf("cdda_restore: restart declined, track %d not playable\n", cue);
			g_musicTrack = -1;
			return;
		}

		cdda_play_range(cue, cdtoc_track_start(g_toc, cue), g_musicLoop);
		cdda_wait_for_sound();
		return;
	}
}

#define DISC_BOOT_PROGRAM "0.BIN"

static int disc_manifest_scan(int *presentOut, const char *reportPrefix)
{
	int presentCount = 0;
	int missingCount = 0;
	int sizeBadCount = 0;
	const char *firstBad = 0;
	int firstBadGot = 0;
	int firstBadWant = 0;

#define DISC_MANIFEST_CHECK(name, lba, size)                                          \
	{                                                                                  \
		SRL::Cd::File manifestFile(name);                                             \
		if (!manifestFile.Exists())                                                   \
		{                                                                              \
			missingCount++;                                                           \
			if (firstBad == 0) { firstBad = name; firstBadGot = -1; firstBadWant = (int)(size); } \
		}                                                                              \
		else                                                                           \
		{                                                                              \
			presentCount++;                                                           \
			if (manifestFile.Size.Bytes != (int32_t)(size))                           \
			{                                                                          \
				sizeBadCount++;                                                       \
				if (firstBad == 0) { firstBad = name; firstBadGot = (int)manifestFile.Size.Bytes; firstBadWant = (int)(size); } \
			}                                                                          \
		}                                                                              \
	}

	DISC_MANIFEST_LIST(DISC_MANIFEST_CHECK)
#undef DISC_MANIFEST_CHECK

	if (presentOut != 0)
	{
		*presentOut = presentCount;
	}

	if (reportPrefix != 0 && (missingCount != 0 || sizeBadCount != 0))
	{
		printf("%s: %d missing, %d wrong size\n", reportPrefix, missingCount, sizeBadCount);
		printf("%s: %s got %d want %d\n", reportPrefix, firstBad, firstBadGot, firstBadWant);
	}

	return missingCount + sizeBadCount;
}

int disc_open(const char *cue_path)
{
	(void)cue_path;

	disc_close();

	SRL::Cd::Initialize();

	int presentCount = 0;

	if (disc_manifest_scan(&presentCount, "disc_open") != 0)
	{
		printf("disc_open: Part II data incomplete, its menu entry will not confirm\n");
	}

	if (presentCount == 0)
	{
		SRL::Cd::File bootProgram(DISC_BOOT_PROGRAM);
		if (!bootProgram.Exists())
		{
			printf("disc_open: no file resolves, not even %s -- drive or GFS is down\n",
			       DISC_BOOT_PROGRAM);
			disc_close();
			return 0;
		}
	}

	printf("build %s\n", __TIME__);

	SRL::Sound::Cdda::Analysis::Start();

	CDC_TgetToc(g_toc);
	g_maxAudioTrack = cdtoc_max_audio_track(g_toc);
	printf("disc_open: highest audio track %d\n", g_maxAudioTrack);

	g_discOpened = true;
	return 1;
}

int disc_part2_available(void)
{
	return disc_manifest_scan(0, 0) == 0;
}

static int disc_read_file_body(const char *name, void *out, int max_size)
{
	char resolved[32];

	if (!g_discOpened)
	{
		printf("disc_read_file: disc not open, can't read '%s'\n", name);
		return -1;
	}

	if (!normalize_name(name, resolved, (int32_t)sizeof(resolved)))
	{
		printf("disc_read_file: bad filename\n");
		return -1;
	}

	SRL::Cd::File file(resolved);

	if (!file.Exists())
	{
		printf("disc_read_file: '%s' not found on disc\n", resolved);
		return -1;
	}

	if (max_size < 0 || file.Size.Bytes > max_size)
	{
		printf("disc_read_file: '%s' is %d bytes, only %d available at destination\n",
			resolved, (int)file.Size.Bytes, max_size);
		return -1;
	}

	if (!file.Open())
	{
		printf("disc_read_file: can't open '%s'\n", resolved);
		return -1;
	}

	int32_t whole = discsec_whole_sectors(file.Size.Bytes, file.Size.SectorSize);
	int32_t tail = discsec_tail_bytes(file.Size.Bytes, file.Size.SectorSize);

	if (file.Size.Bytes <= 0 ||
		file.Size.SectorSize <= 0 ||
		file.Size.SectorSize > DISC_MAX_SECTOR_BYTES ||
		whole + (tail != 0 ? 1 : 0) != file.Size.Sectors ||
		(tail != 0 ? tail : file.Size.SectorSize) != file.Size.LastSectorSize)
	{
		printf("split bad %s\n", resolved);
		printf(" b%d s%d ss%d ls%d w%d t%d\n",
			(int)file.Size.Bytes, (int)file.Size.Sectors,
			(int)file.Size.SectorSize, (int)file.Size.LastSectorSize,
			(int)whole, (int)tail);
		file.Close();
		return -1;
	}

	int32_t got = 0;
	int32_t maxChunk = GFS_GetNumCdbuf(file.Handle);
	int32_t remaining = whole;

	static bool smallCdbufWarned = false;

	if (maxChunk <= 0 || maxChunk > DISC_MAX_REQUEST_SECTORS)
	{
		maxChunk = DISC_MAX_REQUEST_SECTORS;
	}
	else if (maxChunk < DISC_MAX_REQUEST_SECTORS && !smallCdbufWarned)
	{
		printf("cdbuf %d < %d\n", (int)maxChunk, DISC_MAX_REQUEST_SECTORS);
		smallCdbufWarned = true;
	}

	uint32_t *bounce = nullptr;

	if (((uintptr_t)out & 3u) != 0)
	{
		bounce = bounce_acquire();

		if (bounce == nullptr)
		{
			bounce = g_tailSector;
			maxChunk = 1;
		}
		else if (maxChunk > DISC_BOUNCE_SECTORS)
		{
			maxChunk = DISC_BOUNCE_SECTORS;
		}
	}

	while (remaining > 0)
	{
		int32_t take = discsec_request_sectors(remaining, maxChunk);
		int32_t chunkGot;

		if (take <= 0)
		{
			break;
		}

		disc_tick();

		uint8_t *dest = (uint8_t *)out + (whole - remaining) * file.Size.SectorSize;

		if (bounce != nullptr)
		{
			chunkGot = file.ReadSectors(take, bounce);

			if (chunkGot == take * file.Size.SectorSize)
			{
				memcpy(dest, bounce, (size_t)chunkGot);
			}
		}
		else
		{
			chunkGot = file.ReadSectors(take, dest);
		}

		if (chunkGot != take * file.Size.SectorSize)
		{
			break;
		}

		got += chunkGot;
		remaining -= take;
	}

	if (remaining == 0 && tail > 0)
	{
		int32_t tailGot = file.ReadSectors(1, g_tailSector);

		if (tailGot >= tail)
		{
			memcpy((uint8_t *)out + (whole * file.Size.SectorSize), g_tailSector, (size_t)tail);
			got += tail;
		}
	}

	file.Close();

	if (got != file.Size.Bytes)
	{
		printf("disc_read_file: error reading '%s'\n", resolved);
		return -1;
	}

	return 0;
}

int disc_read_file(const char *name, void *out, int max_size)
{
	int result;

	cdda_suspend();
	result = disc_read_file_body(name, out, max_size);
	cdda_restore();

	return result;
}

void disc_play_track(int engine_index, int loop)
{
	int cue;

	if (!g_discOpened || cls.nosound != 0)
	{
		return;
	}

	g_musicPaused = false;

	cue = discfmt_cue_track_for_music(engine_index);

	if (cue == 0 || !cdtoc_is_audio(g_toc, cue))
	{
		printf("disc_play_track: refused, track %d not playable\n", cue);
		return;
	}

	if (g_musicTrack == engine_index && g_musicLoop != 0 && loop != 0)
	{
		CdcStat stat;

		CDC_GetCurStat(&stat);

		if (CDC_GET_STC(&stat) == CDC_ST_PLAY)
		{
			return;
		}
	}

	cdda_play_range(cue, cdtoc_track_start(g_toc, cue), loop);
	g_musicTrack = engine_index;
	g_musicLoop = (loop != 0);
	g_musicObserved = false;
	g_musicStartedAt = platform_ticks();
	g_musicLengthMs = cdda_track_length_ms(cue);

	if (loop == 0)
	{
#if HOTA_DIAG
		fprintf(stderr, "cue: trk%d len%ums\n", cue,
			(unsigned int)g_musicLengthMs);
#endif
	}
}

void disc_wait_for_music(void)
{
	if (!g_discOpened || cls.nosound != 0 || g_musicTrack < 0)
	{
		return;
	}

	cdda_wait_for_sound();
}

void disc_pause_music(void)
{
	if (g_musicPaused)
	{
		return;
	}
	g_musicPaused = true;
	cdda_suspend();
}

void disc_resume_music(void)
{
	if (!g_musicPaused)
	{
		return;
	}
	g_musicPaused = false;
	cdda_restore();
}

void disc_stop_track(void)
{
	g_musicPaused = false;

	if (g_musicTrack < 0)
	{
		return;
	}

	g_musicTrack = -1;
	g_musicObserved = false;
	cdda_halt();
}

static int cdda_oneshot_in_track(void)
{
	CdcStat stat;
	int cue;
	uint32_t start;
	uint32_t end;
	uint32_t fad;

	if (g_musicTrack < 0 || g_musicLoop != 0)
	{
		return 0;
	}

	cue = discfmt_cue_track_for_music(g_musicTrack);
	start = cdtoc_track_start(g_toc, cue);
	end = cdtoc_track_end(g_toc, cue);

	if (end == 0 || end <= start)
	{
		return 0;
	}

	CDC_GetCurStat(&stat);

	if (CDC_GET_STC(&stat) != CDC_ST_PLAY)
	{
		return 0;
	}

	fad = (uint32_t)CDC_STAT_FAD(&stat);

	return (fad >= start && fad < end) ? 1 : 0;
}

static int cdda_oneshot_time_left(void)
{
	if (g_musicTrack < 0 || g_musicLoop != 0 || g_musicLengthMs == 0)
	{
		return 0;
	}

	return (platform_ticks() - g_musicStartedAt < g_musicLengthMs) ? 1 : 0;
}

int disc_music_probe(int *status, int *fadRel, int *lenSectors)
{
	CdcStat stat;
	int cue;
	uint32_t start;
	uint32_t end;

	if (g_musicTrack < 0)
	{
		return 0;
	}

	cue = discfmt_cue_track_for_music(g_musicTrack);
	start = cdtoc_track_start(g_toc, cue);
	end = cdtoc_track_end(g_toc, cue);

	CDC_GetCurStat(&stat);

	if (status != NULL)
	{
		*status = (int)CDC_GET_STC(&stat);
	}

	if (fadRel != NULL)
	{
		*fadRel = (int)((uint32_t)CDC_STAT_FAD(&stat)) - (int)start;
	}

	if (lenSectors != NULL)
	{
		*lenSectors = (end > start) ? (int)(end - start) : 0;
	}

	return 1;
}

void disc_music_restart_at(unsigned int ms)
{
	int cue;
	uint32_t start;
	uint32_t end;
	uint32_t from;

	if (!g_discOpened || cls.nosound != 0 || g_musicTrack < 0 || g_musicLoop != 0)
	{
		return;
	}

	cue = discfmt_cue_track_for_music(g_musicTrack);
	start = cdtoc_track_start(g_toc, cue);
	end = cdtoc_track_end(g_toc, cue);
	from = start + (ms * 75u) / 1000u;

	if (end == 0 || end <= start || from >= end)
	{
		return;
	}

	cdda_play_range(cue, from, g_musicLoop);
	g_musicStartedAt = platform_ticks();
	g_musicLengthMs = ((end - from) * 1000u) / 75u;
}

void disc_wait_for_music_end(unsigned int cap_ms)
{
	unsigned int t0 = platform_ticks();
#if HOTA_DIAG
	int stalled = 0;
#endif

	if (!g_discOpened || cls.nosound != 0 || g_musicTrack < 0 || g_musicLoop != 0)
	{
		return;
	}

	while (cdda_oneshot_in_track() || cdda_oneshot_time_left())
	{
		if (platform_ticks() - t0 >= cap_ms)
		{
#if HOTA_DIAG
			fprintf(stderr, "cue: wait hit cap\n");
#endif
			return;
		}

		disc_tick();
		platform_frame();

#if HOTA_DIAG
		{
			int st;
			int rel;
			int len;

			if (!stalled && disc_music_probe(&st, &rel, &len)
			    && !cdda_oneshot_in_track() && cdda_oneshot_time_left())
			{
				stalled = 1;
				fprintf(stderr, "cue: STALL st%d fad%d/%d t%u\n", st, rel,
					len, platform_ticks() - t0);
			}
		}
#endif
	}
}

int disc_current_track(int *loop)
{
	if (loop != NULL)
	{
		*loop = g_musicLoop;
	}
	return g_musicTrack;
}

void disc_set_music_volume(uint8_t level)
{
	if (level > 7)
	{
		level = 7;
	}

	SRL::Sound::Cdda::SetVolume(level);
}

void disc_close(void)
{
	disc_stop_track();
	g_maxAudioTrack = 0;
	g_discOpened = false;
}

}

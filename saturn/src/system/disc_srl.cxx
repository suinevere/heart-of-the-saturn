/*----------------------
 | disc_srl.cxx
 | Description: Saturn implementation of disc.h, over SRL::Cd::File. Sibling
 |   of src/host/disc_cue.c: same five functions, no FILE *, no bin/cue --
 |   there is one drive and one mounted disc, so disc_open validates the
 |   19-file manifest (disc_manifest.h) against live GFS lookups instead of a
 |   cached ISO9660 directory, and disc_read_file bounds every read at
 |   max_size the same way the host backend does. CD-DA plays through
 |   SRL::Sound::Cdda::PlaySingle and raw CDC_* calls -- not through
 |   Cdda::Resume or Cdda::StopPause, and not through
 |   SRL::Cd::TableOfContents, all of which read a table SRL lays out wrong
 |   (see cdtoc.h). There is one drive, so playback and file reads contend:
 |   disc_read_file brackets itself with a suspend and a restore, which is
 |   the only reason music survives the whole-file load that follows every
 |   disc_play_track in play_anm.
 |
 |   Heap cost, which is not obvious from anything in this file:
 |   srl_cd.hpp:441 allocates SectorSize * SectorsToReadAtOnce = 2048 * 5 =
 |   10,240 bytes on the first Read of every SRL::Cd::File and frees it at
 |   close. That is 14.7% of the 69,440-byte HWRAM heap the linker map leaves,
 |   held for the duration of each disc_read_file -- and saturn_compat.cxx's
 |   malloc deliberately refuses to fall back to LWRAM, so a caller holding
 |   59 KB across a read turns it into a NULL rather than a slow read. Nothing
 |   here stacks two open files to make it worse, and it is transient, but any
 |   future allocator sharing this heap has to be sized against what is left
 |   after it, not against the whole.
 | Author: suinevere
 | Dependencies: srl.hpp, disc.h, disc_manifest.h, saturn_compat.h
 ----------------------*/
#include <srl.hpp>
#include "disc_manifest.h"
#include "saturn_compat.h"

#include "disc.h"
#include "discfmt.h"
#include "cdtoc.h"
#include "cdda_classify.h"
#include "client.h"

/*----------------------
 | g_discOpened
 | Description: Whether disc_open has succeeded and disc_close has not run
 |   since. disc_read_file consults this instead of re-probing the CD, so a
 |   call before disc_open or after disc_close refuses the same way the host
 |   backend's disc_opened flag does.
 | Author: suinevere
 ----------------------*/
static bool g_discOpened = false;

/*----------------------
 | g_toc / g_maxAudioTrack
 | Description: The disc's BIOS table of contents, fetched once by disc_open
 |   and decoded through cdtoc.h -- never through SRL::Cd::TableOfContents,
 |   which reads the wrong track (see cdtoc.h). g_maxAudioTrack is 0 on the
 |   data-only disc HOTA_AUDIO=none builds by default, and that 0 is what
 |   turns every music request on such a disc into a no-op instead of a
 |   CDC_CdPlay for a track the drive cannot find. No separate "is it fetched"
 |   flag: nothing reads either of these except through a live g_musicTrack,
 |   which only a successful disc_open can produce.
 | Author: suinevere
 ----------------------*/
static uint32_t g_toc[CDTOC_WORDS];
static int g_maxAudioTrack = 0;

/*----------------------
 | g_musicTrack / g_musicLoop
 | Description: The music this backend believes it is playing -- engine index,
 |   -1 for none, plus whether it was asked to repeat. The engine has no way
 |   to tell us what is playing and the CD block cannot be asked what it was
 |   asked for, so this is the only record of intent, and it is what lets a
 |   file read put the music back afterwards.
 | Author: suinevere
 ----------------------*/
static int g_musicTrack = -1;
static int g_musicLoop = 0;

/*----------------------
 | g_pauseFad / g_wasPlaying
 | Description: What the drive was doing when the last file read took it away.
 |   g_wasPlaying separates two states that look identical afterwards and need
 |   opposite treatment: a track genuinely interrupted mid-play, and a track
 |   commanded microseconds earlier that never got going because the read beat
 |   it to the drive -- which is what happens on every animation, since
 |   play_anm calls disc_play_track and then immediately reads a whole file.
 | Author: suinevere
 ----------------------*/
static uint32_t g_pauseFad = 0;
static bool g_wasPlaying = false;

/*----------------------
 | g_musicObserved
 | Description: Whether the current g_musicTrack has actually been caught
 |   playing -- CDC_ST_PLAY, head inside its own bounds -- at any suspend
 |   since it was last commanded. Separate from g_wasPlaying, which is only
 |   this instant's status: a track that was only ever commanded, never
 |   observed, must not be treated as "finished" by a stale head position
 |   left over from whatever played before it (see cdda_classify.h). Cleared
 |   whenever a new track is commanded or stopped, so observation never
 |   carries across tracks.
 | Author: suinevere
 ----------------------*/
static bool g_musicObserved = false;

/*----------------------
 | normalize_name
 | Description: Turns the engine's filename into the 8.3 uppercase form GFS
 |   matches on the disc: drop any directory part, upper-case the rest,
 |   append a trailing '.' when there is no extension (an extensionless
 |   ISO9660 name still carries one). Adapted from Another-Saturn's
 |   saturn_cdfile.cxx; every name this engine actually passes already
 |   arrives upper-cased with an extension, but this keeps the seam correct
 |   for names that do not.
 | Author: suinevere
 | Params: name -- source name; out -- destination; outSize -- its capacity
 | Returns: true if a usable name was produced
 ----------------------*/
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

/*----------------------
 | cdda_halt
 | Description: Stops CD-DA output. The seek is what silences it -- there is
 |   no stop command as such; SRL::Sound::Cdda::StopPause does the same two
 |   things, but also stashes the frame address into a private static that
 |   only its broken Resume consumes, so this port issues the pair itself.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void cdda_halt(void)
{
	CdcPos pos;

	CDC_POS_PTYPE(&pos) = CDC_PTYPE_DFL;
	CDC_CdSeek(&pos);
}

/*----------------------
 | cdda_suspend
 | Description: Gives the drive up for a file read, remembering enough to put
 |   the music back. Reads the head position and play status before seeking,
 |   because the seek is what silences output and there is no way to ask
 |   afterwards where it had reached. A no-op when no music is wanted, which
 |   is what keeps the cost off every read on a data-only disc.
 | Author: suinevere
 | Globals: g_musicTrack, g_pauseFad, g_wasPlaying
 | Params: N/A
 | Returns: N/A
 ----------------------*/
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

/*----------------------
 | cdda_restore
 | Description: Puts the music back after a file read, delegating the
 |   three-way choice to cdda_classify (cdda_classify.h) so that decision
 |   carries host-compiled test coverage instead of being provable only by
 |   listening to an emulator.
 |
 |   Before classifying, this call's g_wasPlaying/g_pauseFad are folded into
 |   g_musicObserved: the track is marked observed once it is caught actually
 |   playing inside its own bounds. The fold happens before the classify
 |   call, not after, and that order is deliberate rather than incidental --
 |   CDDA_FORGET requires !was_playing while the fold only sets observed when
 |   was_playing is true, so this call's own fold can never be what lets this
 |   call's own classify return CDDA_FORGET. It can only affect a later
 |   suspend/restore of the same track. The fold only ever sets observed to
 |   true, never clears it, so a track observed once stays observed until
 |   disc_play_track or disc_stop_track resets it for the next track.
 |
 |   CDDA_RESUME plays the remainder as its own frame range so the listener
 |   hears the track continue rather than restart. CDDA_RESTART is what a
 |   looping track gets even when it looks finished -- resuming one would
 |   drop the repeat and leave the room silent once the remainder ended, and
 |   an infinite-repeat play re-seeking past its own end looks identical, on
 |   was_playing/fad alone, to a one-shot finishing; cdda_classify's loop
 |   exclusion is what tells them apart. CDDA_RESTART is also the fallback
 |   cdda_classify reaches for an unreadable TOC (start or end reading 0) --
 |   in this codebase g_maxAudioTrack is 0 whenever the TOC is unreadable, so
 |   disc_play_track refuses every index and g_musicTrack never leaves -1,
 |   meaning this call returns at the guard below before cdda_classify ever
 |   sees that input. The fallback exists for cdda_classify's own contract,
 |   not because this call is known to exercise it.
 | Author: suinevere
 | Globals: g_toc, g_musicTrack, g_musicLoop, g_pauseFad, g_wasPlaying,
 |   g_musicObserved
 | Params: N/A
 | Returns: N/A
 ----------------------*/
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
		return;
	}

	case CDDA_RESTART:
	default:
		SRL::Sound::Cdda::PlaySingle((uint16_t)cue, g_musicLoop != 0);
		return;
	}
}

/*----------------------
 | disc_open
 | Description: Confirms SRL::Cd is up and validates the 19-file manifest
 |   against live GFS lookups before returning success, matching
 |   disc_cue.c's "fail loudly at startup, not quietly three minutes later"
 |   contract. cue_path is accepted and ignored: a Saturn disc has no cue
 |   sheet, there is one drive and one mounted disc. LBA is not checked --
 |   SRL::Cd::File exposes a GFS handle and a size, not a sector address --
 |   so only existence and size are verified; disc_cue.c treats an LBA
 |   mismatch as a non-fatal warning anyway, so nothing fatal is lost.
 |   disc_close() up front makes this idempotent: a re-open always starts
 |   from a clean slate rather than layering state on a previous one, and a
 |   failure here leaves g_discOpened false, exactly as disc.h requires.
 |
 |   The SRL::Cd::Initialize() call is now a re-check, not the bring-up:
 |   SRL::Core::Initialize() runs GFS_Init itself (srl_core.hpp) and
 |   platform_srl.cxx's platform_init is ordered ahead of this function in
 |   main(). Cd::Initialize guards on its own isInitialized flag, so the
 |   second call just returns the cached result -- kept because it is the only
 |   place a CD bring-up failure is reported, and Core::Initialize discards
 |   the return value.
 |
 |   The TOC is read here, once, because it cannot change while a disc is
 |   mounted and because disc_play_track must not be the thing that discovers
 |   the disc has no audio -- on the default HOTA_AUDIO=none build that is
 |   every call.
 | Author: suinevere
 | Params: cue_path -- unused on Saturn
 | Returns: 1 on success, 0 on failure
 ----------------------*/
int disc_open(const char *cue_path)
{
	(void)cue_path;

	disc_close();

	/* SRL::Cd::Initialize()'s return value must not be trusted, and this is
	   the one place in the port that ever asked it. It sets its flag from
	   (GFS_Init(...) <= 2), but GFS_Init returns the count of directory
	   records it read -- 28 on this disc, 26 files plus . and .. -- not a
	   status, and its error codes are negative. So the test rejects every
	   healthy disc with more than two root entries and would accept a real
	   failure. Measured 2026-08-04: GFS_Init returned 28 and the flag came
	   back false, which is why the port panicked with the data sitting
	   correctly on the disc.

	   Calling it is still right -- it performs the actual GFS_Init, and it
	   is what SRL::Core::Initialize does, discarding the result exactly as
	   here. SRL::Cd::File resolves names through GFS_NameToId without ever
	   consulting the flag, which is why every other SRL project works.
	   The manifest check below is the real proof the drive and filesystem
	   are up, so it, not the flag, decides what this function returns. */
	SRL::Cd::Initialize();

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
		else if (manifestFile.Size.Bytes != (int32_t)(size))                          \
		{                                                                              \
			sizeBadCount++;                                                            \
			if (firstBad == 0) { firstBad = name; firstBadGot = (int)manifestFile.Size.Bytes; firstBadWant = (int)(size); } \
		}                                                                              \
	}

	DISC_MANIFEST_LIST(DISC_MANIFEST_CHECK)
#undef DISC_MANIFEST_CHECK

	if (missingCount != 0 || sizeBadCount != 0)
	{
		printf("disc_open: %d missing, %d wrong size\n", missingCount, sizeBadCount);
		printf("disc_open: %s got %d want %d\n", firstBad, firstBadGot, firstBadWant);
		disc_close();
		return 0;
	}

	CDC_TgetToc(g_toc);
	g_maxAudioTrack = cdtoc_max_audio_track(g_toc);
	printf("disc_open: %d audio tracks\n", g_maxAudioTrack > 1 ? g_maxAudioTrack - 1 : 0);

	g_discOpened = true;
	return 1;
}

/*----------------------
 | disc_read_file_body
 | Description: Opens name by 8.3 uppercase match through SRL::Cd::File and
 |   reads the whole file into out, refusing anything the file's size would
 |   overrun past max_size. That bound is not optional: all three callers
 |   (main.c, animation.c, game2bin.c) hand in raw addresses into the
 |   emulated 68000 map with no bounds check of their own, and the map is
 |   512 KB. File::Read copies byte-by-byte into the destination through an
 |   internal sector buffer rather than GFS_Load's sector-rounded
 |   destination write, so it cannot overshoot max_size the way a raw
 |   LoadBytes into a tight buffer could.
 |
 |   Private because it must not be called without the CD-DA bracket around
 |   it; disc_read_file below is the only caller.
 | Author: suinevere
 | Params: name -- disc filename; out -- destination; max_size -- capacity
 |   of out in bytes
 | Returns: 0 on success, negative on failure
 ----------------------*/
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

	int32_t got = file.Read(file.Size.Bytes, out);

	file.Close();

	if (got != file.Size.Bytes)
	{
		printf("disc_read_file: error reading '%s'\n", resolved);
		return -1;
	}

	return 0;
}

/*----------------------
 | disc_read_file
 | Description: disc_read_file_body with the drive handed over and taken back.
 |   There is one drive: a read seeks away from whatever CD-DA was playing and
 |   silences it, so every read is bracketed. The split exists because the
 |   body has six early returns and an inline bracket would need a restore on
 |   each -- a missed one leaves the music off for the rest of the session,
 |   with nothing to indicate why.
 |
 |   Both halves are no-ops when no music is wanted, so a data-only disc pays
 |   two branches per read and nothing else.
 | Author: suinevere
 | Globals: via cdda_suspend and cdda_restore
 | Params: name -- disc filename; out -- destination; max_size -- capacity of
 |   out in bytes
 | Returns: 0 on success, negative on failure
 ----------------------*/
int disc_read_file(const char *name, void *out, int max_size)
{
	int result;

	cdda_suspend();
	result = disc_read_file_body(name, out, max_size);
	cdda_restore();

	return result;
}

/*----------------------
 | disc_play_track
 | Description: Starts a CD-DA track for the engine's music index. Refuses,
 |   silently and by contract, before disc_open, after disc_close, with
 |   cls.nosound set, for an index outside 0..40, and -- the case that matters
 |   for every routine build -- for any track the disc does not carry, which
 |   is all of them on the 12 MB HOTA_AUDIO=none disc. That last guard is not
 |   defensive programming: CDC_CdPlay for a track outside the TOC is
 |   undefined, and the failure mode to avoid is one that leaves the CD block
 |   unable to serve the file reads the game is about to make.
 |
 |   A refusal leaves g_musicTrack untouched: whatever was already playing
 |   keeps playing and stays correctly tracked, matching disc_cue.c's host
 |   implementation, which runs every refusal check before it ever touches
 |   playback state. The two backends share one disc.h contract, and this is
 |   what keeps it literally true of both.
 |
 |   The +2 mapping is not repeated here. discfmt_cue_track_for_music owns it
 |   for both backends and returns 0, an invalid track, when out of range.
 |
 |   SRL::Sound::Cdda::PlaySingle is safe to use where the rest of Cdda is
 |   not: it is CDC_CdPlay by track number and never consults the table of
 |   contents SRL cannot read.
 |
 |   A request for the track the drive is confirmably already looping is
 |   dropped. Re-issuing CDC_CdPlay costs a seek and an audible gap, and "play
 |   T looping" while T loops is a no-op by definition. This is the one place
 |   this backend deliberately differs from src/host/disc_cue.c, where
 |   restarting an already-hooked stream is free. The status check is what
 |   makes it safe: a track that stopped for any reason we did not cause is
 |   started again rather than assumed to be running.
 |
 |   A freshly commanded track also clears g_musicObserved: nothing has
 |   confirmed it actually playing yet, so a later cdda_restore must not
 |   trust a stale observed flag left over from whatever track played before
 |   it (see cdda_classify.h). The repeat-guard's early return skips this on
 |   purpose -- the same track keeps playing, so whatever it had already
 |   observed is still true.
 | Author: suinevere
 | Globals: g_discOpened, g_maxAudioTrack, g_musicTrack, g_musicLoop,
 |   g_musicObserved
 | Params: engine_index -- music index 0..40; loop -- nonzero to repeat
 |   forever
 | Returns: N/A
 ----------------------*/
void disc_play_track(int engine_index, int loop)
{
	int cue;

	if (!g_discOpened || cls.nosound != 0)
	{
		return;
	}

	cue = discfmt_cue_track_for_music(engine_index);

	if (cue == 0 || cue > g_maxAudioTrack)
	{
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

	SRL::Sound::Cdda::PlaySingle((uint16_t)cue, loop != 0);
	g_musicTrack = engine_index;
	g_musicLoop = (loop != 0);
	g_musicObserved = false;
}

/*----------------------
 | disc_stop_track
 | Description: Stops the music and forgets it. Clearing g_musicTrack is the
 |   load-bearing half: it is what stops the next disc_read_file from putting
 |   back a track the engine deliberately silenced. g_musicObserved is
 |   cleared alongside it so a later track never inherits this one's
 |   observed history. Safe before disc_open, after disc_close, and with
 |   nothing playing, exactly as disc.h requires, which is what lets
 |   atexit_callback call it unconditionally.
 | Author: suinevere
 | Globals: g_musicTrack, g_musicObserved
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void disc_stop_track(void)
{
	if (g_musicTrack < 0)
	{
		return;
	}

	g_musicTrack = -1;
	g_musicObserved = false;
	cdda_halt();
}

/*----------------------
 | disc_close
 | Description: Drops back to the never-been-opened state. Every
 |   SRL::Cd::File this backend touches is stack-local and already closed by
 |   its own destructor, so there is nothing to release here but the flag --
 |   safe before disc_open, after a failed disc_open, and twice in a row.
 |
 |   Stopping the music here as well means a bare disc_close and the
 |   atexit stop-then-close pair land in the same state, and that a re-open
 |   cannot inherit a track request made against the previous disc.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void disc_close(void)
{
	disc_stop_track();
	g_maxAudioTrack = 0;
	g_discOpened = false;
}

}

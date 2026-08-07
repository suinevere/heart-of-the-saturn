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
 |   Heap cost, which is not obvious from anything in this file: there is none.
 |   srl_cd.hpp:441 allocates SectorSize * SectorsToReadAtOnce = 2048 * 5 =
 |   10,240 bytes on the first File::Read of every SRL::Cd::File and frees it
 |   at close; this file no longer calls File::Read, so that buffer is never
 |   created. ReadSectors hands the caller's destination straight to GFS_Fread
 |   and allocates nothing. The linker map leaves 62,528 bytes of HWRAM heap
 |   (__heap_start 0x060b0bc0 .. __heap_end 0x060c0000), and
 |   saturn_compat.cxx's malloc deliberately refuses to fall back to LWRAM, so
 |   that figure is the entire budget for any future allocator on this heap --
 |   but it is no longer competing with a 10,240-byte transient that existed
 |   for exactly as long as a read was in flight.
 |
 |   This file's own static state also comes out of that same HWRAM. Measured
 |   2026-08-04 by building this branch's tip (24f387f) and the commit just
 |   before it (3051d5c) in separate worktrees, both HOTA_AUDIO=none
 |   via the same compile.bat: total .bss went from 581,200 to 581,632 bytes,
 |   a 432-byte increase (the linker map's own .bss line, not an estimate).
 |   This file's own object carries 420 of those bytes (disc_srl.o's .bss
 |   went from 1 byte, g_discOpened alone, to 421). CDTOC_WORDS(102) * 4 = 408
 |   of that 420 is g_toc, unavoidable since CDC_TgetToc writes the whole
 |   table at once, leaving 12 bytes for g_maxAudioTrack, g_musicLoop,
 |   g_pauseFad, g_wasPlaying and g_musicObserved combined (g_musicTrack
 |   initialises to -1, so it lives in .data, not .bss). The other 12 bytes
 |   of the 432-byte total fall outside this file -- plausibly alignment
 |   padding from linking in cdtoc.o and cdda_classify.o, both of which
 |   contribute 0 bytes of their own .bss -- and were not chased further,
 |   since they are not this file's own state. Static, not transient, so it
 |   is already subtracted from the 62,528-byte figure above rather than
 |   stacking on it.
 |
 |   g_tailSector adds 2,048 bytes on top of that, taking total .bss to
 |   583,680 (0x8e800) and this file's own object to 2,469, and frees none of it in
 |   exchange -- the 10,240 bytes it displaces were heap, not .bss. The trade
 |   is still strongly positive: those 10,240 bytes came out of the same HWRAM
 |   the heap figure above is computed from, and came out of it at exactly the
 |   moment a read was in flight, so peak HWRAM during a read drops by 8,192
 |   bytes even though total .bss rises.
 | Author: suinevere
 | Dependencies: srl.hpp, disc.h, disc_manifest.h, saturn_compat.h
 ----------------------*/
#include <srl.hpp>
#include "disc_manifest.h"
#include "saturn_compat.h"

#include "disc.h"
#include "discsec.h"
#include "discfmt.h"
#include "cdtoc.h"
#include "cdda_classify.h"
#include "client.h"
#include "platform.h"

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
 |   data-only disc a HOTA_AUDIO=none build produces, and that 0 is what
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
 | g_tailSector
 | Description: Where the final, partial sector of a file lands so that only
 |   the bytes the file actually has reach the caller. A whole-sector read
 |   straight into the engine's pointer would also write the rest of that
 |   sector -- for ROOMS7.BIN, 966 bytes of whatever the mastering tool put
 |   after 160,826 bytes of room data -- into the emulated 68000 map past the
 |   end of the room. disc.h promises that will not happen, and none of the
 |   three call sites bounds-checks the pointer it hands in.
 |
 |   File-scope static, not a stack array, for three reasons in order of
 |   weight. The stack cannot absorb it: disc_read_file is reached through
 |   main -> play_anm -> play_animation -> disc_read_file, and nothing in this
 |   tree states the SH-2 master stack size -- SGL's convention puts it below
 |   the 0x06004000 load address sgl.linker:4 gives PRELOADER, which is
 |   single-digit kilobytes, so a 2 KB frame against an unstated budget is an
 |   overflow that would present as corruption somewhere else entirely. A
 |   static is visible: it appears in the map as .bss and comes out of the same
 |   HWRAM the heap figure above is computed from, so the cost is subtracted
 |   once and stays subtracted, where a stack frame is invisible to the map and
 |   gets argued about instead of measured. And uint32_t guarantees the
 |   alignment: GFS transfers into this buffer, and a char[2048] has no
 |   alignment contract beyond whatever the compiler happens to give it.
 | Author: suinevere
 ----------------------*/
static uint32_t g_tailSector[DISC_MAX_SECTOR_BYTES / 4];

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
 | cdda_probe_wait
 | Description: TEMPORARY MEASUREMENT PROBE -- remove before this lands.
 |   Spins on CDC_GetCurStat until the drive reports CDC_ST_PLAY or the cap
 |   expires, then prints how long that took. The vblank handler advances
 |   platform_ticks' counter from an interrupt, so this loop does not need
 |   SRL::Core::Synchronize to make the clock move. Answers the only question
 |   the code cannot: how much of the audible gap is the drive acquiring the
 |   track, and how much is anything else.
 | Author: suinevere
 | Globals: N/A
 | Params: where -- call site tag; cue -- cue track commanded
 | Returns: N/A
 ----------------------*/
#define CDDA_PROBE_CAP_MS 4000

/*----------------------
 | g_probeMaxChunk / g_probeRequests
 | Description: TEMPORARY MEASUREMENT PROBE -- removed with the rest of the
 |   probe. What the last body read actually did: the chunk ceiling the drive
 |   reported through GFS_GetNumCdbuf after clamping, and how many requests it
 |   took. Reported so the partition size is read off the hardware rather than
 |   inferred from GAME2.BIN's 200 sectors having been the largest read that
 |   ever returned.
 | Author: suinevere
 ----------------------*/
static int32_t g_probeMaxChunk = 0;
static int32_t g_probeRequests = 0;
static int32_t g_probeCdbuf = 0;
static int32_t g_probeAlign = 0;

static void cdda_probe_wait(const char *where, int cue)
{
	unsigned int t0 = platform_ticks();
	unsigned int waited;
	CdcStat stat;
	int state;

	for (;;)
	{
		CDC_GetCurStat(&stat);
		state = CDC_GET_STC(&stat);
		waited = platform_ticks() - t0;

		if (state == CDC_ST_PLAY || waited >= CDDA_PROBE_CAP_MS)
		{
			break;
		}
	}

	printf("cdda %s c%d %u ms s%d\n", where, cue, waited, state);
}

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
 |   sees that input in practice. The fallback exists for cdda_classify's own
 |   contract, not because this call is known to exercise it -- but the
 |   CDDA_RESTART case does not lean on that invariant to stay safe: it
 |   re-checks cdtoc_is_audio(g_toc, cue) itself before issuing PlaySingle,
 |   closing the same CDC_CdPlay-on-an-unplayable-track hazard disc_play_track
 |   guards against, a second time, rather than arguing it unreachable here.
 |   A classifier result that cannot be honoured this way is treated the same
 |   as CDDA_FORGET -- g_musicTrack is cleared rather than left pointing at a
 |   track this call just declined to play -- but it prints its own message,
 |   not CDDA_FORGET's. cdda_classify returned CDDA_RESTART here, not
 |   finished and not never-started, so "classified as finished or
 |   never-started" would be false on this branch: the decline came from
 |   cdtoc_is_audio, which points at the TOC or the disc, not from
 |   cdda_classify's was_playing/loop/observed/fad inputs. A listening test
 |   with no rebuild available cannot tell those two causes apart without
 |   separate messages, which is why this file emits four diagnostics
 |   against the design's stated budget of three -- see the design spec's
 |   diagnostics paragraph.
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
		cdda_probe_wait("restore/resume", cue);
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

		SRL::Sound::Cdda::PlaySingle((uint16_t)cue, g_musicLoop != 0);
		cdda_probe_wait("restore/restart", cue);
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
 |   the disc has no audio -- on a HOTA_AUDIO=none build that is every call.
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

	printf("build %s\n", __TIME__);

	CDC_TgetToc(g_toc);
	g_maxAudioTrack = cdtoc_max_audio_track(g_toc);
	printf("disc_open: highest audio track %d\n", g_maxAudioTrack);

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
 |   512 KB. That claim used to rest on File::Read, which copied byte-by-byte
 |   into the destination through an internal sector buffer and so could not
 |   overshoot. It no longer does, and the guarantee is now this function's own
 |   to keep: the whole-sector body goes straight into out through one
 |   ReadSectors -- one GFS_Fread instead of File::Read's forty-three
 |   five-sector requests, each of which cost about 160 ms of the drive losing
 |   its place and re-acquiring it -- and the partial final sector goes into
 |   g_tailSector, out of which exactly discsec_tail_bytes bytes are copied.
 |   ROOMS7.BIN is the only file that takes that second path, and it is the
 |   reason the path exists: its last sector carries 1,082 bytes of room data
 |   and 966 bytes belonging to nobody.
 |
 |   The body read is chunked, and the ceiling is not arbitrary: a GFS_Fread for
 |   more sectors than the CD block's buffer partition holds does not fail, it
 |   never returns. Measured on the emulator 2026-08-06, GAME2.BIN -- exactly
 |   200 sectors -- read in 1,450 ms, while every animation at 211 sectors hung
 |   with a black screen and no probe line at all, the probe's printf sitting
 |   after this function returns. 200 is the partition size, which is why that
 |   one file worked and nothing larger did. GFS_GetNumCdbuf reports the real
 |   figure, so this asks the drive rather than trusting the 200 that boundary
 |   implies, and clamps to DISC_MAX_REQUEST_SECTORS besides.
 |
 |   SRL never hit this because SectorsToReadAtOnce is 5, far under any
 |   partition. The deadlock is a property of asking for a whole file at once,
 |   not of this file's destination -- which matters, because destination
 |   alignment looks like the obvious suspect and is not the cause.
 |   ANIMATION_LOAD_BASE is 0x809a, so animations land 2 bytes off a longword
 |   boundary, and that theory was tested on hardware: forcing GFS_TMODE_CPU for
 |   misaligned destinations changed nothing, and a software transfer cannot
 |   care about a 2-byte offset. Do not re-derive it from the load base and try
 |   again.
 |
 |   Successive ReadSectors calls continue where the last stopped -- it takes
 |   its own position from GFS_Tell (srl_cd.hpp:513) -- so the chunk loop needs
 |   no Seek, which would allocate the same 10,240-byte work buffer this change
 |   exists to avoid. The tail read is always one sector and so is never over
 |   any partition.
 |
 |   Both sector counts derive from Size.Bytes, never from Size.Sectors, and
 |   that is load-bearing rather than tidy. ReadSectors clamps its buffer size
 |   to the sectors actually remaining but forwards the unclamped sectorCount
 |   to GFS_Fread (srl_cd.hpp:515-519), so asking for more sectors than the
 |   file has hands GFS a count larger than the buffer it was told about.
 |   Size.Bytes is the one number max_size was compared against, so a split
 |   derived from it cannot exceed what was bounded; the guard below then
 |   cross-checks that split against Size.Sectors and Size.LastSectorSize and
 |   refuses on any disagreement, rather than trusting either pair alone.
 |
 |   Neither Seek nor LoadBytes is used. Seek (srl_cd.hpp:538) allocates the
 |   same 10,240-byte work buffer this change exists to remove. LoadBytes
 |   (srl_cd.hpp:399) would be one GFS_Load for the whole file, but it closes
 |   and reopens the handle around the call and GFS_Load's destination write is
 |   sector-rounded -- precisely the overrun refused above.
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

	g_probeCdbuf = maxChunk;
	g_probeAlign = (int32_t)((uintptr_t)out & 3u);

	if (maxChunk <= 0 || maxChunk > DISC_MAX_REQUEST_SECTORS)
	{
		maxChunk = DISC_MAX_REQUEST_SECTORS;
	}

	if (g_probeAlign != 0)
	{
		GFS_SetTmode(file.Handle, GFS_TMODE_CPU);
	}

	printf("go %s a%d b%d w%d\n", resolved, (int)g_probeAlign,
		(int)g_probeCdbuf, (int)whole);

	while (remaining > 0)
	{
		int32_t take = discsec_request_sectors(remaining, maxChunk);
		int32_t chunkGot;

		if (take <= 0)
		{
			break;
		}

		printf(" req %d of %d\n", (int)take, (int)remaining);

		chunkGot = file.ReadSectors(take, (uint8_t *)out + (whole - remaining) * file.Size.SectorSize);

		if (chunkGot != take * file.Size.SectorSize)
		{
			break;
		}

		got += chunkGot;
		remaining -= take;
		g_probeRequests++;
	}

	g_probeMaxChunk = maxChunk;

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
	unsigned int t0;
	unsigned int t1;

	cdda_suspend();
	g_probeRequests = 0;
	t0 = platform_ticks();
	result = disc_read_file_body(name, out, max_size);
	t1 = platform_ticks();
	printf("rd %s %u ms c%d n%d\n",
		name, t1 - t0, (int)g_probeMaxChunk, (int)g_probeRequests);
	cdda_restore();

	return result;
}

/*----------------------
 | disc_play_track
 | Description: Starts a CD-DA track for the engine's music index. Refuses
 |   silently before disc_open, after disc_close, and with cls.nosound set.
 |   Refuses with a printf -- the one place this function is not silent --
 |   for an index outside 0..40 or, the case that matters for every routine
 |   build, for any track that does not exist on the disc or exists but is
 |   not audio, which is every index on the 12 MB HOTA_AUDIO=none disc. That
 |   last guard is not defensive programming: CDC_CdPlay for a track outside
 |   the TOC, or for a data track sitting below the highest audio track, is
 |   undefined, and the failure mode to avoid is one that leaves the CD block
 |   unable to serve the file reads the game is about to make.
 |   cdtoc_is_audio(g_toc, cue) is the precise predicate for both halves of
 |   that hazard at once -- host-tested in cdtoc.c -- rather than the weaker
 |   cue > g_maxAudioTrack range check this used to be, which would have
 |   admitted a data track sitting below the highest audio track.
 |
 |   A refusal leaves g_musicTrack untouched: whatever was already playing
 |   keeps playing and stays correctly tracked, matching disc_cue.c's host
 |   implementation for every refusal check but one -- disc_cue.c's own
 |   fopen failure for a missing track file runs after its disc_stop_track()
 |   has already silenced whatever was playing, so that one host path does
 |   touch playback state ahead of a failure it could not yet foresee. Every
 |   other refusal on both backends, including this one, runs before
 |   touching state.
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
 | Globals: g_discOpened, g_toc, g_musicTrack, g_musicLoop, g_musicObserved
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

	SRL::Sound::Cdda::PlaySingle((uint16_t)cue, loop != 0);
	cdda_probe_wait("play_track", cue);
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

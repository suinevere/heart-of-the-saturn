/*----------------------
 | disc_srl.cxx
 | Description: Saturn implementation of disc.h, over SRL::Cd::File. Sibling
 |   of src/host/disc_cue.c: same five functions, no FILE *, no bin/cue --
 |   there is one drive and one mounted disc, so disc_open validates the
 |   19-file manifest (disc_manifest.h) against live GFS lookups instead of a
 |   cached ISO9660 directory, and disc_read_file bounds every read at
 |   max_size the same way the host backend does. CD-DA (disc_play_track/
 |   disc_stop_track) is a later sub-project; both are silent no-ops here,
 |   which disc.h's contract explicitly allows.
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

	g_discOpened = true;
	return 1;
}

/*----------------------
 | disc_read_file
 | Description: Opens name by 8.3 uppercase match through SRL::Cd::File and
 |   reads the whole file into out, refusing anything the file's size would
 |   overrun past max_size. That bound is not optional: all three callers
 |   (main.c, animation.c, game2bin.c) hand in raw addresses into the
 |   emulated 68000 map with no bounds check of their own, and the map is
 |   512 KB. File::Read copies byte-by-byte into the destination through an
 |   internal sector buffer rather than GFS_Load's sector-rounded
 |   destination write, so it cannot overshoot max_size the way a raw
 |   LoadBytes into a tight buffer could.
 | Author: suinevere
 | Params: name -- disc filename; out -- destination; max_size -- capacity
 |   of out in bytes
 | Returns: 0 on success, negative on failure
 ----------------------*/
int disc_read_file(const char *name, void *out, int max_size)
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
 | disc_play_track
 | Description: No-op. CD-DA playback is a later sub-project's work --
 |   disc.h's contract explicitly permits a silent no-op here, the same as
 |   the host backend gives when cls.nosound is set.
 | Author: suinevere
 | Params: engine_index -- unused; loop -- unused
 | Returns: N/A
 ----------------------*/
void disc_play_track(int engine_index, int loop)
{
	(void)engine_index;
	(void)loop;
}

/*----------------------
 | disc_stop_track
 | Description: No-op, for the same reason as disc_play_track: CD-DA is a
 |   later sub-project. Safe to call with nothing playing, before disc_open,
 |   or after disc_close, exactly as disc.h requires.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void disc_stop_track(void)
{
}

/*----------------------
 | disc_close
 | Description: Drops back to the never-been-opened state. Every
 |   SRL::Cd::File this backend touches is stack-local and already closed by
 |   its own destructor, so there is nothing to release here but the flag --
 |   safe before disc_open, after a failed disc_open, and twice in a row.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void disc_close(void)
{
	g_discOpened = false;
}

}

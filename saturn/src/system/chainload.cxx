/*----------------------
 | chainload.cxx
 | Description: The SRL seam for handing the console to Part I. Carries no
 |   decisions -- bootmenu.c decides whether this runs, which is what keeps
 |   that decision host-testable when none of this is.
 | Author: suinevere
 | Dependencies: srl.hpp, srl_scu.hpp, chainload.h, disc.h, discsec.h,
 |   fadecalc.h, saturn_bootart.h, sound.h, video.h
 | Globals: N/A
 ----------------------*/
extern "C" {
#include "chainload.h"
#include "disc.h"
#include "discsec.h"
#include "fadecalc.h"
#include "saturn_bootart.h"
#include "saturn_compat.h"
#include "sound.h"
#include "video.h"
}

#include <srl.hpp>
#include <srl_scu.hpp>
#include <sgl.h>
#include <sega_gfs.h>
#include <sega_sys.h>

/*----------------------
 | CHAINLOAD_ENTRY
 | Description: Where both programs link. sgl.linker places PRELOADER here.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_ENTRY 0x06004000u

/*----------------------
 | CHAINLOAD_UNCACHED
 | Description: Added to an address to reach its uncached mirror. The copy
 |   writes Part I through this so the SH-2 instruction cache cannot serve a
 |   stale line of our own code over an address that now holds Part I's, and
 |   the trampoline executes through it for the same reason -- it was written
 |   into LWRAM moments earlier through a cached address.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_UNCACHED 0x20000000u

/*----------------------
 | CHAINLOAD_MAX_BYTES
 | Description: How much HWRAM sits above CHAINLOAD_ENTRY, so an oversized
 |   image is refused rather than written off the end of the 1 MB region.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_MAX_BYTES 0x000fc000u

/*----------------------
 | g_trampoline
 | Description: Copy-and-jump, hand-encoded because it must run from LWRAM
 |   while HWRAM is being overwritten, and a compiled function cannot be
 |   relocated -- SH-2 reads 32-bit constants from a PC-relative literal pool
 |   that does not travel with the code. Every address arrives in a register
 |   (r4 src, r5 dst, r6 longword count, r7 entry) so there is no pool at all.
 |
 |       6046  mov.l  @r4+, r0
 |       2502  mov.l  r0, @r5
 |       7504  add    #4, r5
 |       4610  dt     r6
 |       8bfa  bf     -6        ; back to mov.l @r4+, r0
 |       472b  jmp    @r7
 |       0009  nop              ; jmp's delay slot
 | Author: suinevere
 ----------------------*/
static const unsigned short g_trampoline[7] = {
	0x6046, 0x2502, 0x7504, 0x4610, 0x8bfa, 0x472b, 0x0009
};

/*----------------------
 | chainload_fn
 | Description: How the trampoline is called: the SH ABI puts src, dst,
 |   longword count and entry in r4-r7, which is exactly what its seven
 |   instructions read.
 | Author: suinevere
 ----------------------*/
typedef void (*chainload_fn)(const void *src, void *dst,
                             unsigned long longwords, void *entry);

/*----------------------
 | chainload_restore
 | Description: Puts the picture back after a staging failure. boot_fade_out
 |   ran before this file was entered and latched the art level at zero, and
 |   chainload_run blacks VDP2 on top of that, so every return path here is a
 |   return to an invisible menu unless both are lit again. The CD-DA is not
 |   restarted: bootmenu owns the music and restarts it on its own idle
 |   timeout.
 | Author: suinevere
 | Dependencies: fadecalc.h, saturn_bootart.h, video.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void chainload_restore(void)
{
	/* DIAGNOSTIC BUILD ONLY -- revert to the two-line restore below once the
	   failing gate is known. boot_art_load hides NBG0, which is the layer
	   SRL::Debug::Print (and so printf, via saturn_compat.cxx's diag_write)
	   draws on, so a chainload failure would otherwise print its reason onto a
	   hidden layer behind faded-out sprites. Releasing the art puts NBG0 back
	   and makes the reason readable, at the cost of the menu art.

	   boot_art_fade(FADECALC_LEVEL_NORMAL);
	   video_set_fade(FADECALC_LEVEL_NORMAL); */
	boot_art_release();
	boot_art_present();
	video_set_fade(FADECALC_LEVEL_NORMAL);
}

/*----------------------
 | chainload_available
 | Description: Reports whether Part I's program is on this disc.
 | Author: suinevere
 | Dependencies: srl.hpp, chainload.h
 | Globals: N/A
 | Params: N/A
 | Returns: non-zero when CHAINLOAD_IMAGE exists
 ----------------------*/
int chainload_available(void)
{
	SRL::Cd::File image(CHAINLOAD_IMAGE);
	return image.Exists() ? 1 : 0;
}

/*----------------------
 | chainload_run
 | Description: Fades out, loads Part I over this program and jumps to it.
 |   Never returns on success.
 |
 |   The staging buffer is rounded up to a whole sector because GFS_Load, which
 |   LoadBytes calls, writes sector-rounded into the destination -- the same
 |   overrun disc_read_file_body refuses at disc_srl.cxx:757. The copy length
 |   still comes from the image size; only the allocation grows.
 |
 |   The cache purge before the jump is not redundant with the uncached mirror.
 |   Writing Part I through the mirror keeps the write off the cache, but it
 |   does not invalidate lines already holding our own code over that window --
 |   and we are executing from that window, so such lines are certain to exist.
 |
 |   The sound quiesce is two calls because the SCSP is driven from two sides:
 |   sound_flush_cache is named for its memory job but stops all four PCM
 |   channels first, which is the only teardown this port has for the voices it
 |   programs directly, while slSoundOffWait holds the M68K in reset so the SGL
 |   driver task we booted is not still running when Part I's SND_Init loads its
 |   own over it.
 | Author: suinevere
 | Dependencies: srl.hpp, srl_scu.hpp, chainload.h, disc.h, discsec.h,
 |   sound.h, video.h
 | Globals: g_trampoline
 | Params: N/A
 | Returns: N/A on success; returns to the caller only if staging failed
 ----------------------*/
void chainload_run(void)
{
	SRL::Cd::File image(CHAINLOAD_IMAGE);

	if (!image.Exists())
	{
		printf("chainload: 1 open failed\n");
		chainload_restore();
		return;
	}

	int bytes = (int)image.Size.Bytes;

	printf("chainload: b%d s%d ss%d\n",
		(int)image.Size.Bytes, (int)image.Size.Sectors, (int)image.Size.SectorSize);

	if (bytes <= 0 ||
		(unsigned long)bytes > CHAINLOAD_MAX_BYTES ||
		image.Size.SectorSize <= 0 ||
		image.Size.SectorSize > DISC_MAX_SECTOR_BYTES)
	{
		printf("chainload: 2 bad size\n");
		chainload_restore();
		return;
	}

	unsigned long longwords = ((unsigned long)bytes + 3ul) / 4ul;
	unsigned long staging = (((unsigned long)bytes + (DISC_MAX_SECTOR_BYTES - 1ul)) /
	                         DISC_MAX_SECTOR_BYTES) * DISC_MAX_SECTOR_BYTES;

	void *staged = SRL::Memory::LowWorkRam::Malloc((size_t)staging);

	if (staged == 0)
	{
		printf("chainload: 3 no LWRAM for %d\n", (int)staging);
		chainload_restore();
		return;
	}

	void *tramp = SRL::Memory::LowWorkRam::Malloc(sizeof(g_trampoline));

	if (tramp == 0)
	{
		printf("chainload: 4 no LWRAM for trampoline\n");
		SRL::Memory::LowWorkRam::Free(staged);
		chainload_restore();
		return;
	}

	video_set_fade(0);
	disc_stop_track();

	int loaded = image.LoadBytes(0, bytes, staged);

	if (loaded != bytes)
	{
		printf("chainload: 5 read %d of %d\n", loaded, bytes);
		SRL::Memory::LowWorkRam::Free(tramp);
		SRL::Memory::LowWorkRam::Free(staged);
		chainload_restore();
		return;
	}

	printf("chainload: staged, jumping\n");

	for (unsigned int i = 0; i < sizeof(g_trampoline) / sizeof(g_trampoline[0]); i++)
	{
		((unsigned short *)tramp)[i] = g_trampoline[i];
	}

	GFS_Reset();
	slSlaveFunc(NULL, NULL);
	sound_flush_cache();
	slSoundOffWait();
	SYS_SETSCUIM(0xffffffffu);
	__asm__ __volatile__("ldc %0, sr" :: "r"(0x000000f0u) : "memory");

	chainload_fn go = (chainload_fn)((unsigned long)tramp | CHAINLOAD_UNCACHED);

	*reinterpret_cast<volatile uint16_t *>(SRL::SCU::DSP::RegisterMap::CacheControlRegister) |=
		SRL::SCU::DSP::CachePurgeBit;

	go((const void *)((unsigned long)staged | CHAINLOAD_UNCACHED),
	   (void *)(CHAINLOAD_ENTRY | CHAINLOAD_UNCACHED),
	   longwords,
	   (void *)CHAINLOAD_ENTRY);
}

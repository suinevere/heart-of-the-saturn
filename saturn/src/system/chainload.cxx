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
 | SMPC_COMREG / SMPC_SF / SMPC_SSHOFF / SMPC_SMPC_TRIES
 | Description: The SMPC's command port, its busy flag, the command that powers
 |   the slave processor down, and how many polls to give it before giving up.
 |
 |   Written straight at the port rather than through slSlaveOffWait because
 |   that macro reaches slRequestCommand, which takes a semaphore before doing
 |   anything and returns -1 having done nothing when it cannot have it
 |   (LIBSGL.A, `_slRequestCommand+0x2`: bsr, then cmp/eq #0,r0; bf to the
 |   exit). SGL reads the pads through that same port every frame, so the
 |   semaphore is routinely held, and the halt this teardown depends on was
 |   failing silently.
 | Author: suinevere
 ----------------------*/
#define SMPC_COMREG (*(volatile uint8_t *)0x2010001Fu)
#define SMPC_SF     (*(volatile uint8_t *)0x20100063u)
#define SMPC_SSHOFF 0x03u
#define SMPC_TRIES  100000u

/*----------------------
 | CHAINLOAD_UINT_FIRST / CHAINLOAD_UINT_LAST
 | Description: The SCU interrupt vectors whose BIOS hooks have to be let go of
 |   before Part I lands.
 |
 |   The hooks live at 0x06000a00, in the BIOS work area below CHAINLOAD_ENTRY,
 |   which is the one part of HWRAM the copy does not rewrite -- so Part I
 |   inherits ours. A save state taken in the menu has six of them pointing at
 |   0x0602cbf6, 0x0602cd4e, 0x0602d384, 0x0602cbe4, 0x0602cbd2 and 0x0602cbc0:
 |   vblank in and out, system manager, and the three DMA-end vectors, every one
 |   of them an address the image is about to be written over. slInitSystem
 |   unmasks interrupts partway through its own run and only re-hooks afterwards,
 |   so the first vblank inside that window is dispatched into Part I's data.
 |
 |   smpsys.c:156-157 does exactly this before it jumps to APP_ENTRY, for its own
 |   two hooks and with the comment "hook re-initialisation". The whole range is
 |   cleared here rather than a named few because which vectors SGL took is its
 |   business, not ours.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_UINT_FIRST 0x40u
#define CHAINLOAD_UINT_LAST  0x5fu

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
	boot_art_fade(FADECALC_LEVEL_NORMAL);
	video_set_fade(FADECALC_LEVEL_NORMAL);
}

/*----------------------
 | chainload_slave_off
 | Description: Powers the slave processor down, and reports whether the SMPC
 |   actually did it.
 |
 |   The slave has to stop before HWRAM is overwritten. SGL parks it in a loop
 |   inside our own program -- the BIOS slave entry at 0x06000250 pointed into
 |   the window this overwrites -- and SGL's slInitSystem never turns it back
 |   on, so a slave still running when Part I lands is executing whatever Part
 |   I's image happens to put under its program counter. Part I asks for the
 |   slave nowhere, so leaving it down is the state its startup expects.
 |
 |   Both waits are bounded because this runs with the console half torn down
 |   and a spin that cannot end is the failure this whole file keeps finding.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: non-zero when the SMPC accepted the command and finished it
 ----------------------*/
static int chainload_slave_off(void)
{
	unsigned int spin;

	for (spin = 0; spin < SMPC_TRIES && (SMPC_SF & 1u) != 0u; spin++)
	{
	}

	if ((SMPC_SF & 1u) != 0u)
	{
		return 0;
	}

	SMPC_SF = 1u;
	SMPC_COMREG = SMPC_SSHOFF;

	for (spin = 0; spin < SMPC_TRIES && (SMPC_SF & 1u) != 0u; spin++)
	{
	}

	return (SMPC_SF & 1u) == 0u;
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
 |   The BIOS interrupt hooks are released before the mask goes on, because they
 |   are the one piece of our state Part I cannot avoid inheriting -- see
 |   CHAINLOAD_UINT_FIRST. The mask alone does not cover it: Part I lifts the
 |   mask itself, inside slInitSystem, before it has hooks of its own.
 |
 |   The cache purge before the jump is not redundant with the uncached mirror.
 |   Writing Part I through the mirror keeps the write off the cache, but it
 |   does not invalidate lines already holding our own code over that window --
 |   and we are executing from that window, so such lines are certain to exist.
 |
 |   The slave is stopped by chainload_slave_off, which goes at the SMPC itself
 |   and checks the result. Neither SGL spelling works here: slSlaveFunc(NULL)
 |   registers a function for the slave to run rather than halting it, and
 |   slSlaveOffWait returns having done nothing whenever the SMPC semaphore is
 |   held. It runs first, and its failure returns, because it is the last check
 |   that can still be answered by going back to the menu.
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
		chainload_restore();
		return;
	}

	int bytes = (int)image.Size.Bytes;

	if (bytes <= 0 ||
		(unsigned long)bytes > CHAINLOAD_MAX_BYTES ||
		image.Size.SectorSize <= 0 ||
		image.Size.SectorSize > DISC_MAX_SECTOR_BYTES)
	{
		chainload_restore();
		return;
	}

	unsigned long longwords = ((unsigned long)bytes + 3ul) / 4ul;
	unsigned long staging = (((unsigned long)bytes + (DISC_MAX_SECTOR_BYTES - 1ul)) /
	                         DISC_MAX_SECTOR_BYTES) * DISC_MAX_SECTOR_BYTES;

	void *staged = SRL::Memory::LowWorkRam::Malloc((size_t)staging);

	if (staged == 0)
	{
		chainload_restore();
		return;
	}

	void *tramp = SRL::Memory::LowWorkRam::Malloc(sizeof(g_trampoline));

	if (tramp == 0)
	{
		SRL::Memory::LowWorkRam::Free(staged);
		chainload_restore();
		return;
	}

	video_set_fade(0);
	disc_stop_track();

	int loaded = image.LoadBytes(0, bytes, staged);

	if (loaded != bytes)
	{
		SRL::Memory::LowWorkRam::Free(tramp);
		SRL::Memory::LowWorkRam::Free(staged);
		chainload_restore();
		return;
	}

	for (unsigned int i = 0; i < sizeof(g_trampoline) / sizeof(g_trampoline[0]); i++)
	{
		((unsigned short *)tramp)[i] = g_trampoline[i];
	}

	if (!chainload_slave_off())
	{
		SRL::Memory::LowWorkRam::Free(tramp);
		SRL::Memory::LowWorkRam::Free(staged);
		chainload_restore();
		return;
	}

	GFS_Reset();
	sound_flush_cache();
	slSoundOffWait();

	for (unsigned int vector = CHAINLOAD_UINT_FIRST; vector <= CHAINLOAD_UINT_LAST; vector++)
	{
		SYS_SETUINT(vector, 0);
		SYS_SETSINT(vector, 0);
	}

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

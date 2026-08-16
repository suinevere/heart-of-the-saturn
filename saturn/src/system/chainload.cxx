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
#include "platform.h"
#include "saturn_bootart.h"
#include "sound.h"
#include "video.h"
}

#include <srl.hpp>
#include <srl_scu.hpp>
#include <sgl.h>
#include <sega_gfs.h>
#include <sega_sys.h>

/* Last, and outside the block above, for the reason disc_srl.cxx:65-67 orders
   it the same way: this header carries its own extern "C" guard, and it
   #defines getenv, so any SDK header that declares getenv after it would have
   that declaration rewritten into a syntax error. It only has to precede the
   printf call sites below. */
#include "saturn_compat.h"

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
 | CHAINLOAD_DIAG_MAGIC / g_chainDiag
 | Description: DIAGNOSTIC BUILD ONLY -- delete once the chain-load works.
 |   Records how far chainload_run got, in work RAM, because the screen cannot
 |   be trusted to report it: boot_art_load hides NBG0, which is the layer
 |   printf reaches through saturn_compat.cxx's diag_write, so every message
 |   this function emits lands on a hidden layer.
 |
 |   The magic in slot 0 is what makes this findable without a linker map --
 |   a save state can be searched for the constant and the six words read off
 |   behind it. Slots are: magic, furthest stage reached, file bytes,
 |   LoadBytes' return, the staging buffer's address, its size.
 |
 |   volatile so the stores are not sunk or reordered past the calls between
 |   them; a stage that never runs must leave its slot at the previous value.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_DIAG_MAGIC 0x5a17c0deu

volatile uint32_t g_chainDiag[6] = { CHAINLOAD_DIAG_MAGIC, 0, 0, 0, 0, 0 };

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
 | CHAINLOAD_HOLD_MS / chainload_hold
 | Description: DIAGNOSTIC BUILD ONLY -- delete with g_chainDiag.
 |
 |   Puts the recorded state on screen and keeps it there long enough to read.
 |   Three things conspired to make the earlier prints unreadable and this
 |   answers all of them: boot_art_load hides NBG0, which is the layer printf
 |   reaches, so the art is released first; every path out of here either
 |   blacks the screen or jumps, so the text is held for ten seconds before
 |   either happens; and SRL::Debug::Print writes a string without clearing the
 |   rest of its row, so a short line leaves the tail of a longer one behind it
 |   -- every line below is padded to DIAG_COLS (40) to wipe what it lands on.
 |
 |   Presenting inside the wait is what advances platform_ticks: it is driven
 |   by Core::Synchronize, which boot_art_present is the only thing here that
 |   reaches, so a bare spin would never terminate.
 |
 |   That dependency is also why this may only be called from a path that still
 |   has interrupts: Synchronize waits on a vblank, and the quiesce masks it at
 |   both the SCU and the SH-2, so a call after that point hangs on the first
 |   present -- before the line below that lights the screen back up. Every
 |   caller here is a staging failure, which returns while interrupts are live;
 |   the jump path prints instead, since SRL::Debug::Print writes VDP2 VRAM
 |   directly and needs no vblank to become visible.
 | Author: suinevere
 | Dependencies: platform.h, saturn_bootart.h, video.h
 | Globals: g_chainDiag
 | Params: N/A
 | Returns: N/A
 ----------------------*/
#define CHAINLOAD_HOLD_MS 10000u

static void chainload_hold(void)
{
	unsigned int start;

	boot_art_release();
	boot_art_present();
	video_set_fade(FADECALC_LEVEL_NORMAL);

	printf("-- CHAINLOAD DIAG ---------------------\n");
	printf("stage   %-30d\n", (int)g_chainDiag[1]);
	printf("bytes   %-10d loaded %-11d\n", (int)g_chainDiag[2], (int)g_chainDiag[3]);
	printf("staged  %08x   size   %-11d\n", (unsigned int)g_chainDiag[4], (int)g_chainDiag[5]);
	printf("--------------------------------------\n");

	start = platform_ticks();

	while (platform_ticks() - start < CHAINLOAD_HOLD_MS)
	{
		boot_art_present();
	}
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

	/* DIAGNOSTIC BUILD ONLY. 100 separates "the menu was reached and
	   chainload_run was never called" from "this state was saved before any of
	   it ran", which a slot left at its initial 0 cannot distinguish. */
	g_chainDiag[1] = 100u;

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

	g_chainDiag[1] = 1u;

	if (!image.Exists())
	{
		printf("chainload: 1 open failed\n");
		chainload_hold();
		chainload_restore();
		return;
	}

	int bytes = (int)image.Size.Bytes;

	g_chainDiag[1] = 2u;
	g_chainDiag[2] = (uint32_t)bytes;

	printf("chainload: b%d s%d ss%d\n",
		(int)image.Size.Bytes, (int)image.Size.Sectors, (int)image.Size.SectorSize);

	if (bytes <= 0 ||
		(unsigned long)bytes > CHAINLOAD_MAX_BYTES ||
		image.Size.SectorSize <= 0 ||
		image.Size.SectorSize > DISC_MAX_SECTOR_BYTES)
	{
		printf("chainload: 2 bad size\n");
		chainload_hold();
		chainload_restore();
		return;
	}

	unsigned long longwords = ((unsigned long)bytes + 3ul) / 4ul;
	unsigned long staging = (((unsigned long)bytes + (DISC_MAX_SECTOR_BYTES - 1ul)) /
	                         DISC_MAX_SECTOR_BYTES) * DISC_MAX_SECTOR_BYTES;

	g_chainDiag[1] = 3u;
	g_chainDiag[5] = (uint32_t)staging;

	void *staged = SRL::Memory::LowWorkRam::Malloc((size_t)staging);

	g_chainDiag[4] = (uint32_t)staged;

	if (staged == 0)
	{
		printf("chainload: 3 no LWRAM for %d\n", (int)staging);
		chainload_hold();
		chainload_restore();
		return;
	}

	g_chainDiag[1] = 4u;

	void *tramp = SRL::Memory::LowWorkRam::Malloc(sizeof(g_trampoline));

	if (tramp == 0)
	{
		printf("chainload: 4 no LWRAM for trampoline\n");
		SRL::Memory::LowWorkRam::Free(staged);
		chainload_hold();
		chainload_restore();
		return;
	}

	video_set_fade(0);
	disc_stop_track();

	g_chainDiag[1] = 5u;

	int loaded = image.LoadBytes(0, bytes, staged);

	g_chainDiag[1] = 6u;
	g_chainDiag[3] = (uint32_t)loaded;

	if (loaded != bytes)
	{
		printf("chainload: 5 read %d of %d\n", loaded, bytes);
		SRL::Memory::LowWorkRam::Free(tramp);
		SRL::Memory::LowWorkRam::Free(staged);
		chainload_hold();
		chainload_restore();
		return;
	}

	printf("chainload: staged, jumping\n");

	for (unsigned int i = 0; i < sizeof(g_trampoline) / sizeof(g_trampoline[0]); i++)
	{
		((unsigned short *)tramp)[i] = g_trampoline[i];
	}

	if (!chainload_slave_off())
	{
		printf("chainload: 6 slave halt refused\n");
		SRL::Memory::LowWorkRam::Free(tramp);
		SRL::Memory::LowWorkRam::Free(staged);
		chainload_hold();
		chainload_restore();
		return;
	}

	g_chainDiag[1] = 7u;

	GFS_Reset();
	sound_flush_cache();
	slSoundOffWait();
	SYS_SETSCUIM(0xffffffffu);
	__asm__ __volatile__("ldc %0, sr" :: "r"(0x000000f0u) : "memory");

	g_chainDiag[1] = 8u;

	printf("chainload: quiesced, jumping\n");

	chainload_fn go = (chainload_fn)((unsigned long)tramp | CHAINLOAD_UNCACHED);

	*reinterpret_cast<volatile uint16_t *>(SRL::SCU::DSP::RegisterMap::CacheControlRegister) |=
		SRL::SCU::DSP::CachePurgeBit;

	go((const void *)((unsigned long)staged | CHAINLOAD_UNCACHED),
	   (void *)(CHAINLOAD_ENTRY | CHAINLOAD_UNCACHED),
	   longwords,
	   (void *)CHAINLOAD_ENTRY);
}

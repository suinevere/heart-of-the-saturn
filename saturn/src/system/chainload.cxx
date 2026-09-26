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

#define CHAINLOAD_ENTRY 0x06004000u

#define CHAINLOAD_UNCACHED 0x20000000u

#define CHAINLOAD_MAX_BYTES 0x000fc000u

#define SMPC_COMREG (*(volatile uint8_t *)0x2010001Fu)
#define SMPC_SF     (*(volatile uint8_t *)0x20100063u)
#define SMPC_SSHOFF 0x03u
#define SMPC_TRIES  100000u

#define CHAINLOAD_UINT_FIRST 0x40u
#define CHAINLOAD_UINT_LAST  0x5fu

static const unsigned short g_trampoline[7] = {
	0x6046, 0x2502, 0x7504, 0x4610, 0x8bfa, 0x472b, 0x0009
};

typedef void (*chainload_fn)(const void *src, void *dst,
                             unsigned long longwords, void *entry);

static void chainload_restore(void)
{
	boot_art_fade(FADECALC_LEVEL_NORMAL);
	video_set_fade(FADECALC_LEVEL_NORMAL);
}

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

int chainload_available(void)
{
	SRL::Cd::File image(CHAINLOAD_IMAGE);

	return image.Exists() ? 1 : 0;
}

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

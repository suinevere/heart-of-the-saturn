#include <srl.hpp>

#include "sound.h"
#include "sfxconv.h"
#include "saturn_compat.h"

#define SFX_CHANNELS 4

#define SFX_CACHE_SLOTS 256

#define SFX_PITCH_8KHZ 0x69CE

static signed char *g_sampleData[SFX_CACHE_SLOTS];
static unsigned long g_sampleSize[SFX_CACHE_SLOTS];

static int g_warned;

static PCM g_pcm[SFX_CHANNELS] = {
	{ _Mono | _PCM8Bit, 0, 127, 0, SFX_PITCH_8KHZ, 0, 0, 0, 0 },
	{ _Mono | _PCM8Bit, 2, 127, 0, SFX_PITCH_8KHZ, 0, 0, 0, 0 },
	{ _Mono | _PCM8Bit, 4, 127, 0, SFX_PITCH_8KHZ, 0, 0, 0, 0 },
	{ _Mono | _PCM8Bit, 6, 127, 0, SFX_PITCH_8KHZ, 0, 0, 0, 0 }
};

static int sfx_level_for_volume(int volume)
{
	int e;
	int level;

	if (volume <= 0)
	{
		return 0;
	}

	if (volume > 255)
	{
		volume = 255;
	}

	e = 0;
	while ((volume >> (e + 1)) != 0)
	{
		e++;
	}

	level = 63 + 8 * e + ((volume - (1 << e)) * 8) / (1 << e);

	if (level > 127)
	{
		level = 127;
	}

	return level;
}

extern "C" {

void play_sample(int index, int volume, int channel)
{
	int offset;
	int length;
	int padded;
	int i;
	signed char *buffer;

	if (index == 0)
	{
		for (i = 0; i < SFX_CHANNELS; i++)
		{
			if (slPCMStat(&g_pcm[i]))
			{
				slPCMOff(&g_pcm[i]);
			}
		}

		return;
	}

	if (channel < 0 || channel >= SFX_CHANNELS)
	{
		return;
	}

	index = index - 1;
	if (index < 0 || index >= SFX_CACHE_SLOTS)
	{
		return;
	}

	if (g_sampleData[index] == 0)
	{
		if (!sfxconv_locate(index, &offset, &length))
		{
			return;
		}

		padded = sfxconv_padded_size(length);
		buffer = (signed char *)saturn_lwram_alloc((unsigned long)padded);
		if (buffer == 0)
		{
			if (!g_warned)
			{
				g_warned = 1;
				printf("SFX: lwram full at %d", index + 1);
			}

			return;
		}

		sfxconv_decode_into(offset, length, buffer, padded);
		g_sampleData[index] = buffer;
		g_sampleSize[index] = (unsigned long)padded;
	}

	if (slPCMStat(&g_pcm[channel]))
	{
		slPCMOff(&g_pcm[channel]);
	}

	g_pcm[channel].mode      = _Mono | _PCM8Bit;
	g_pcm[channel].channel   = (uint8_t)(channel * 2);
	g_pcm[channel].level     = (uint8_t)sfx_level_for_volume(volume);
	g_pcm[channel].pan       = 0;
	g_pcm[channel].pitch     = SFX_PITCH_8KHZ;
	g_pcm[channel].eflevelR  = 0;
	g_pcm[channel].efselectR = 0;
	g_pcm[channel].eflevelL  = 0;
	g_pcm[channel].efselectL = 0;

	slPCMOn(&g_pcm[channel], g_sampleData[index], g_sampleSize[index]);
}

void sound_flush_cache()
{
	int i;

	for (i = 0; i < SFX_CHANNELS; i++)
	{
		if (slPCMStat(&g_pcm[i]))
		{
			slPCMOff(&g_pcm[i]);
		}
	}

	for (i = 0; i < SFX_CACHE_SLOTS; i++)
	{
		if (g_sampleData[i] != 0)
		{
			saturn_lwram_free(g_sampleData[i]);
			g_sampleData[i] = 0;
			g_sampleSize[i] = 0;
		}
	}

	g_warned = 0;
}

}

#include <stdio.h>
#include <SDL.h>
#include <SDL_mixer.h>

#include "platform.h"
#include "client.h"
#include "common.h"
#include "sound.h"

int platform_init(void)
{
	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) < 0)
	{
		return 0;
	}

	if (cls.nosound == 0)
	{
		int spec_freq;
		Uint16 spec_format;
		int spec_channels;

#if (SDL_VERSIONNUM(SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL) >= SDL_VERSIONNUM(2, 0, 2))
		const SDL_version *link_version = Mix_Linked_Version();
		if (SDL_VERSIONNUM(link_version->major, link_version->minor, link_version->patch) >= SDL_VERSIONNUM(2,0,2))
		{
			if (Mix_OpenAudioDevice(44100, AUDIO_S16, 2, 4096, NULL, 0) < 0)
			{
				panic("Mix_OpenAudio failed\n");
			}
		}
		else
#endif
		if (Mix_OpenAudio(44100, AUDIO_S16, 2, 4096) < 0)
		{
			panic("Mix_OpenAudio failed\n");
		}

		spec_freq = 0;
		spec_format = 0;
		spec_channels = 0;
		Mix_QuerySpec(&spec_freq, &spec_format, &spec_channels);
		if (spec_freq != 44100 || spec_format != AUDIO_S16 || spec_channels != 2)
		{
			fprintf(stderr, "WARNING: audio device negotiated freq=%d format=0x%x channels=%d, "
			                "expected 44100/AUDIO_S16(0x%x)/2 -- CD-DA music will NOT play "
			                "(would be pitched/timed wrong)\n",
			        spec_freq, (unsigned)spec_format, spec_channels, (unsigned)AUDIO_S16);
		}
		else
		{
			printf("audio device negotiated freq=%d format=0x%x channels=%d (matches CD-DA, music enabled)\n",
			       spec_freq, (unsigned)spec_format, spec_channels);
		}
		fflush(stdout);

		sound_init();
	}

	return 1;
}

void platform_quit(void)
{
	SDL_Quit();
}

unsigned int platform_ticks(void)
{
	return SDL_GetTicks();
}

void platform_delay(unsigned int ms)
{
	SDL_Delay(ms);
}

void platform_frame(void)
{
}

/*----------------------
 | platform_sdl.c
 | Description: SDL host implementation of platform.h. platform_init below is
 |   moved verbatim from src/main.c's initialize(), where the SDL_Init call
 |   and the Mix_OpenAudioDevice/Mix_QuerySpec block lived inline before the
 |   platform seam existed -- disc.h's banner already documents that block as
 |   host-backend policy a Saturn backend will not reuse.
 | Author: suinevere
 | Dependencies: SDL.h, SDL_mixer.h, platform.h, common.h (panic), sound.h,
 |   client.h (cls.nosound), stdio.h
 ----------------------*/
#include <stdio.h>
#include <SDL.h>
#include <SDL_mixer.h>

#include "platform.h"
#include "client.h"
#include "common.h"
#include "sound.h"

/*----------------------
 | platform_init
 | Description: SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO), then the audio
 |   device negotiation the CD-DA path depends on, moved verbatim from
 |   main.c including its comments -- they explain why allowed_changes is 0
 |   and are the reason the CD-DA path is correct. Returns 0 if either the
 |   SDL init or the audio device open fails.
 | Author: suinevere
 ----------------------*/
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

		/* SDL_AUDIO_ALLOW_ANY_CHANGE deliberately removed (flags arg is 0
		   below): it let SDL hand back a device at a different frequency,
		   format or channel count than requested, and SDL_mixer would then
		   mix at that spec silently. disc_cue.c's Mix_HookMusic callback
		   streams CD-DA straight off the disc with a plain fread -- no
		   resample -- so correct pitch/speed depends on the device really
		   being 44100 Hz/AUDIO_S16/stereo. A 48000 Hz WASAPI device (common
		   on Windows) would otherwise play every track about 8.8% fast:
		   plausible enough to survive casual listening. Do not restore this
		   flag without also giving disc_cue.c a resampler. */
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

		/* Prove the negotiated spec rather than assume it matched the
		   request. A mismatch here means disc_cue.c's unconverted CD-DA
		   stream would play at the wrong pitch/speed -- worse than silence,
		   since it sounds plausible and gets missed. disc_play_track
		   independently re-checks this before every track and refuses to
		   play on a mismatch; this printout is so the mismatch (or the
		   match) is visible at startup instead of only discovered later. */
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

/*----------------------
 | platform_quit
 | Description: SDL_Quit(), moved out of main.c's atexit_callback.
 | Author: suinevere
 ----------------------*/
void platform_quit(void)
{
	SDL_Quit();
}

/*----------------------
 | platform_ticks
 | Description: SDL_GetTicks() -- milliseconds since SDL_Init, monotonic for
 |   the life of the process.
 | Author: suinevere
 ----------------------*/
unsigned int platform_ticks(void)
{
	return SDL_GetTicks();
}

/*----------------------
 | platform_delay
 | Description: SDL_Delay(ms).
 | Author: suinevere
 ----------------------*/
void platform_delay(unsigned int ms)
{
	SDL_Delay(ms);
}

/*----------------------
 | platform_frame
 | Description: Empty -- the host game loop is free-running. Exists so the
 |   loop can call it unconditionally; the Saturn backend's version will be
 |   SRL::Core::Synchronize().
 | Author: suinevere
 ----------------------*/
void platform_frame(void)
{
}

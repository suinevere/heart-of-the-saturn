/*----------------------
 | sound_srl.cxx
 | Description: TEMPORARY SPIKE. Answers one question before the sound-effects
 |   design commits to it: can slPCMOn stream from Low Work RAM? Plays a
 |   generated square wave out of a saturn_lwram_alloc block on the first
 |   play_sample call. Replaced wholesale by the real backend.
 | Author: suinevere
 | Dependencies: srl.hpp, sound.h, saturn_compat.h
 ----------------------*/

#include <srl.hpp>

#include "sound.h"
#include "saturn_compat.h"

namespace
{
	/*----------------------
	 | SpikeTone
	 | Description: Non-owning IPcmFile over a caller-supplied buffer, so the
	 |   spike can hand slPCMOn a pointer of its own choosing. IPcmFile's
	 |   members are protected and its destructor is empty, which is what makes
	 |   a non-owning subclass correct here.
	 | Author: suinevere
	 ----------------------*/
	class SpikeTone : public SRL::Sound::Pcm::IPcmFile
	{
	public:
		SpikeTone(signed char *buffer, unsigned long bytes)
		{
			this->data = (int8_t *)buffer;
			this->dataSize = bytes;
			this->mode = _Mono;
			this->depth = _PCM8Bit;
			this->sampleRate = 8000;
		}
	};
}

extern "C" {

/*----------------------
 | play_sample
 | Description: Spike body. Allocates 8,000 bytes of LWRAM on first call, fills
 |   it with a 250 Hz square wave and plays it on channel 0. Every argument is
 |   discarded -- the question is whether any sound comes out of an LWRAM
 |   buffer at all, not whether the right sound does.
 | Author: suinevere
 | Globals: N/A
 | Params: index -- ignored; volume -- ignored; channel -- ignored
 | Returns: N/A
 ----------------------*/
void play_sample(int index, int volume, int channel)
{
	static signed char *tone = 0;
	int i;

	(void)index;
	(void)volume;
	(void)channel;

	if (tone == 0)
	{
		tone = (signed char *)saturn_lwram_alloc(8000);
		if (tone == 0)
		{
			printf("SPIKE: lwram alloc failed");
			return;
		}

		for (i = 0; i < 8000; i++)
		{
			tone[i] = ((i / 16) & 1) ? 100 : -100;
		}
	}

	printf("SPIKE: play from lwram %p", (void *)tone);
	SRL::Sound::Pcm::StopSound(0);
	SpikeTone(tone, 8000).PlayOnChannel(0, 100);
}

/*----------------------
 | sound_flush_cache
 | Description: Spike body. Nothing to flush.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_flush_cache()
{
}

}

#include <srl.hpp>

#include "platform.h"
#include "saturn_reset.h"

static volatile uint32_t g_frames = 0;

static void onVblank()
{
	g_frames = g_frames + 1;
}

extern "C" {

int platform_init(void)
{
	__asm__ __volatile__("ldc %0, sr" :: "r"(0x00000000u) : "memory");

	SRL::Core::Initialize(SRL::Types::HighColor(0, 0, 0));
	SRL::Core::OnVblank += onVblank;

	g_frames = 0;

	return 1;
}

void platform_quit(void)
{
}

unsigned int platform_ticks(void)
{
	return (unsigned int)((g_frames * 50u) / 3u);
}

void platform_delay(unsigned int ms)
{
	const unsigned int until = platform_ticks() + ms;

	while (platform_ticks() < until)
	{
		SRL::Core::Synchronize();
	}
}

void platform_frame(void)
{
	SRL::Core::Synchronize();
	saturn_reset_poll();
}

}

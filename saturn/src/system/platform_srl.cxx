/*----------------------
 | platform_srl.cxx
 | Description: Saturn implementation of platform.h -- SRL bring-up, the
 |   millisecond clock and the frame pump. Sibling of host/platform_sdl.c.
 |
 |   This file owns the entry point into SRL. platform_init's
 |   SRL::Core::Initialize() is the first line of the port that runs: malloc
 |   and operator new both route to an SRL arena (saturn_compat.cxx) that does
 |   not exist until it returns, so every allocation in the engine -- the
 |   emulated 68000 map, the GAME2.BIN buffer, VDP2 VRAM -- depends on it
 |   having happened. main.c calls platform_init ahead of disc_open for that
 |   reason; Core::Initialize also performs the CD, VDP2, sound-driver and
 |   timer bring-up, so nothing else in the port may touch SRL before it.
 |
 |   The clock is a vblank count, not SRL::Timer. Timer's Tickstamp reads the
 |   FRT at PHI_128 and its ToMilliseconds() saturates at about 32.8 seconds
 |   (srl_timer.hpp), which is useless for a clock the engine expects to run
 |   monotonically for the whole session; a vblank counter is exact, free, and
 |   already quantised to the same 60 Hz the engine paces itself against.
 |
 |   Audio device setup is absent by design, as platform.h's banner sets out:
 |   the host backend's Mix_OpenAudioDevice block exists to guarantee CD-DA's
 |   negotiated spec, and there is no such negotiation here.
 | Author: suinevere
 | Dependencies: srl.hpp, platform.h
 ----------------------*/
#include <srl.hpp>

// platform.h carries no extern "C" guard of its own. Included plain from a
// .cxx its five declarations pick up C++ linkage, which then conflicts with
// the extern "C" definitions below and with the C objects that call them.
extern "C" {
#include "platform.h"
}

/*----------------------
 | g_frames
 | Description: Vblank count since platform_init, the only clock this backend
 |   has. volatile because onVblank writes it from the interrupt SRL installs
 |   with slIntFunction while platform_delay reads it in a loop, and a
 |   non-volatile read would be hoisted out of that loop and spin forever.
 | Author: suinevere
 ----------------------*/
static volatile uint32_t g_frames = 0;

/*----------------------
 | onVblank
 | Description: Advances the frame counter. Registered on SRL::Core::OnVblank
 |   by platform_init, so it ticks whatever the engine is doing -- including
 |   inside a disc read, which is why the clock does not stall over a load.
 |   Written as an explicit load-add-store because C++20 deprecates ++ on a
 |   volatile lvalue and the toolchain warns on it.
 | Author: suinevere
 | Globals: g_frames
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void onVblank()
{
	g_frames = g_frames + 1;
}

extern "C" {

/*----------------------
 | platform_init
 | Description: Brings SRL up and starts the clock. SRL::Core::Initialize must
 |   be the first thing the port does for the reason the file banner gives --
 |   the heap does not exist before it -- so this function has nothing to do
 |   ahead of it and cannot fail: Initialize returns void and every step it
 |   performs is fatal-or-nothing inside SGL. The backdrop is black so the
 |   16 columns and 32 rows video_srl.cxx does not cover read as letterboxing.
 | Author: suinevere
 | Globals: g_frames
 | Params: N/A
 | Returns: 1 always
 ----------------------*/
int platform_init(void)
{
	SRL::Core::Initialize(SRL::Types::HighColor(0, 0, 0));
	SRL::Core::OnVblank += onVblank;

	g_frames = 0;

	return 1;
}

/*----------------------
 | platform_quit
 | Description: No-op. There is nothing to return to on a console and nothing
 |   SRL asks to be released; the disc and the VDP2 layer outlive the game
 |   loop either way. Exists so main.c's atexit_callback links unchanged.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void platform_quit(void)
{
}

/*----------------------
 | platform_ticks
 | Description: Milliseconds since platform_init, from the vblank count. NTSC
 |   is 60 fields per second, so a field is 50/3 ms and the conversion is one
 |   multiply and one divide with no float and no drift beyond the integer
 |   truncation. Monotonic for about 16 days of continuous play before the
 |   multiply overflows 32 bits. The unit matters: main.c's rest() busy-waits
 |   on `current_tick - last_tick < diff` with diff in milliseconds, so a
 |   coarser or finer unit here does not break the loop, it changes how fast
 |   the game runs.
 | Author: suinevere
 | Globals: g_frames
 | Params: N/A
 | Returns: elapsed milliseconds
 ----------------------*/
unsigned int platform_ticks(void)
{
	return (unsigned int)((g_frames * 50u) / 3u);
}

/*----------------------
 | platform_delay
 | Description: Waits about ms milliseconds by spinning on
 |   SRL::Core::Synchronize() rather than blocking. Synchronize is what
 |   presents the VDP2 layer and refreshes the peripherals, so a delay that
 |   blocked instead would freeze the display for its duration -- and it is
 |   also the only thing that lets the vblank handler advance the counter this
 |   loop is waiting on, so blocking would hang outright. Resolution is one
 |   field: a delay shorter than 17 ms still costs a whole frame.
 | Author: suinevere
 | Globals: N/A
 | Params: ms -- milliseconds to wait
 | Returns: N/A
 ----------------------*/
void platform_delay(unsigned int ms)
{
	const unsigned int until = platform_ticks() + ms;

	while (platform_ticks() < until)
	{
		SRL::Core::Synchronize();
	}
}

/*----------------------
 | platform_frame
 | Description: SRL::Core::Synchronize(), called once per game-loop iteration.
 |   Without it the VDP2 layer video_srl.cxx wrote is never presented and the
 |   screen stays black, which is the whole reason platform.h declares this
 |   seam at all.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void platform_frame(void)
{
	SRL::Core::Synchronize();
}

}

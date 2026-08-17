/*----------------------
 | input_srl.cxx
 | Description: Saturn implementation of input.h, over SRL::Input::Digital.
 |   Sibling of host/input_sdl.c, and far smaller than it: that backend drains
 |   an SDL event queue and also carries quit, quicksave, a speed throttle, a
 |   debug toggle and input recording, all of which are host development
 |   conveniences reached by keyboard. A console has no quit, saturn_filestub.c
 |   has no filesystem to quicksave into, and the rest have no pad equivalent
 |   worth inventing, so this file reads port 0 into one raw PAD_BIT_* mask
 |   and leaves which physical button means what to keymap_apply, in
 |   keymap.c.
 |
 |   Reads only. SRL::Core::Synchronize() already calls
 |   Input::Management::RefreshPeripherals() (srl_core.hpp), and
 |   platform_frame() already calls Synchronize(), so the pad state is
 |   refreshed once per presented frame by machinery that already exists.
 |   Refreshing again here would collapse current and previous state and
 |   silently break WasPressed/WasReleased for anything that later wants an
 |   edge -- this file wants none, because the engine's key model is level:
 |   main.c sets a key and reads it, and nothing anywhere reads a transition.
 |
 |   That leaves one wrinkle worth knowing: animation.c's post_render calls
 |   platform_frame before check_events, so the animation loop reads pad state
 |   refreshed by the same sync, while run()'s loop reads at main.c:594 and
 |   refreshes at main.c:652 -- one frame, about 16 ms, older. Accepted rather
 |   than fixed: refreshing here breaks edges for good, and reordering run()
 |   perturbs the present/frame ordering verified on hardware in the boot
 |   sub-project. 16 ms is imperceptible in a game this deliberate.
 |
 |   No extern "C" block wraps the include of input.h below, unlike the four
 |   sibling backends (platform_srl.cxx, video_srl.cxx, sound_srl.cxx,
 |   disc_srl.cxx): input.h guards its own declarations with #ifdef
 |   __cplusplus, so check_events() already gets C linkage without one here.
 |
 |   Design: docs/superpowers/specs/2026-08-05-hota-saturn-input-design.md
 | Author: suinevere
 | Dependencies: srl.hpp, input.h, keymap.h
 ----------------------*/

#include <srl.hpp>

#include "input.h"
#include "keymap.h"

/*----------------------
 | check_events
 | Description: Copies port 0's raw pad state into the key globals the game
 |   loop reads. The four directions are copied straight out of
 |   input_raw_buttons' mask; key_a, key_b and key_c go through keymap_apply
 |   instead, so whichever buttons the player has bound to run, whip and jump
 |   land in those three globals rather than always physical A, B and C.
 |
 |   key_select and key_reset_record are deliberately not written: both are
 |   host input-recording state, not gameplay state. key_select is written and
 |   read only by read_keys_from_record/add_keys_to_record (main.c);
 |   key_reset_record is host-only recording control set by input_sdl.c.
 |   input_sdl.c never writes key_select, and update_keys (main.c:285) reads
 |   neither, so gameplay never observes them. cls.quit is likewise never set,
 |   because run()'s while (cls.quit == 0) running forever is correct on a
 |   console.
 | Author: suinevere
 | Dependencies: input.h, keymap.h
 | Globals: key_up, key_down, key_left, key_right, key_a, key_b, key_c
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void check_events(void)
{
	unsigned int raw = input_raw_buttons();

	key_up    = (raw & PAD_BIT_UP)    ? 1 : 0;
	key_down  = (raw & PAD_BIT_DOWN)  ? 1 : 0;
	key_left  = (raw & PAD_BIT_LEFT)  ? 1 : 0;
	key_right = (raw & PAD_BIT_RIGHT) ? 1 : 0;

	keymap_apply(keymap_active(), raw, &key_a, &key_b, &key_c);
}

/*----------------------
 | input_raw_buttons
 | Description: Port 0's physical button state this frame, before any
 |   mapping, as a PAD_BIT_* mask. Saturn side of the seam documented in
 |   input.h; this banner covers only what is specific to SRL::Input::Digital.
 |
 |   Port 0 plus the IsConnected check below is the only safe pairing, not an
 |   arbitrary choice: SRL::Input::Management::Peripherals[] (srl_input.hpp)
 |   sets only port 0's id to NotConnected; ports 1-11 default to id 0, which
 |   reads as PeripheralFamily::Digital -- connected -- with data 0, and the
 |   pad is active-low, so every button would read held. SRL::Core::
 |   Initialize() never calls RefreshPeripherals(), only Synchronize() does,
 |   so an input_raw_buttons() call on any other port, or without this guard,
 |   would degrade a pre-Synchronize call to "everything pressed" instead of
 |   "nothing pressed".
 |
 |   The Digital handle is a local rather than a file-static on purpose. Its
 |   constructor is trivial, but a file-static C++ object with a constructor
 |   runs at static-init time, before SRL::Core::Initialize() -- a local
 |   costs nothing and cannot be ordered wrong.
 | Author: suinevere
 | Dependencies: srl.hpp, keymap.h
 | Globals: N/A
 | Params: N/A
 | Returns: the PAD_BIT_* mask of everything held, or 0 if no pad is connected
 ----------------------*/
extern "C" unsigned int input_raw_buttons(void)
{
	SRL::Input::Digital pad(0);
	unsigned int raw = 0;

	if (!pad.IsConnected())
	{
		return 0;
	}

	if (pad.IsHeld(SRL::Input::Digital::Button::A))     raw |= PAD_BIT_A;
	if (pad.IsHeld(SRL::Input::Digital::Button::B))     raw |= PAD_BIT_B;
	if (pad.IsHeld(SRL::Input::Digital::Button::C))     raw |= PAD_BIT_C;
	if (pad.IsHeld(SRL::Input::Digital::Button::X))     raw |= PAD_BIT_X;
	if (pad.IsHeld(SRL::Input::Digital::Button::Y))     raw |= PAD_BIT_Y;
	if (pad.IsHeld(SRL::Input::Digital::Button::Z))     raw |= PAD_BIT_Z;
	if (pad.IsHeld(SRL::Input::Digital::Button::L))     raw |= PAD_BIT_L;
	if (pad.IsHeld(SRL::Input::Digital::Button::R))     raw |= PAD_BIT_R;
	if (pad.IsHeld(SRL::Input::Digital::Button::Up))    raw |= PAD_BIT_UP;
	if (pad.IsHeld(SRL::Input::Digital::Button::Down))  raw |= PAD_BIT_DOWN;
	if (pad.IsHeld(SRL::Input::Digital::Button::Left))  raw |= PAD_BIT_LEFT;
	if (pad.IsHeld(SRL::Input::Digital::Button::Right)) raw |= PAD_BIT_RIGHT;
	if (pad.IsHeld(SRL::Input::Digital::Button::START)) raw |= PAD_BIT_START;

	return raw;
}

/*----------------------
 | input_srl.cxx
 | Description: Saturn implementation of input.h, over SRL::Input::Digital.
 |   Sibling of host/input_sdl.c, and far smaller than it: that backend drains
 |   an SDL event queue and also carries quit, quicksave, a speed throttle, a
 |   debug toggle and input recording, all of which are host development
 |   conveniences reached by keyboard. A console has no quit, saturn_filestub.c
 |   has no filesystem to quicksave into, and the rest have no pad equivalent
 |   worth inventing, so this file maps seven buttons and nothing else.
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
 | Dependencies: srl.hpp, input.h
 ----------------------*/

#include <srl.hpp>

#include "input.h"

/*----------------------
 | check_events
 | Description: Copies port 0's pad into the seven key globals the game loop
 |   reads. Port 0 only: this is a single-player game and port 0 is player 1,
 |   so a pad in another port reads as nothing pressed -- the same thing the
 |   stub did before this file had a body, which makes a mis-plugged pad
 |   degrade to known-good behaviour rather than to something new.
 |
 |   Port 0 plus the IsConnected check below is the only safe pairing, not an
 |   arbitrary choice: SRL::Input::Management::Peripherals[] (srl_input.hpp)
 |   sets only port 0's id to NotConnected; ports 1-11 default to id 0, which
 |   reads as PeripheralFamily::Digital -- connected -- with data 0, and the
 |   pad is active-low, so every button would read held. SRL::Core::
 |   Initialize() never calls RefreshPeripherals(), only Synchronize() does,
 |   so a check_events() on any other port, or without this guard, would
 |   degrade a pre-Synchronize call to "everything pressed" instead of
 |   "nothing pressed".
 |
 |   A and B and C map to key_a and key_b and key_c by label rather than by
 |   function. The Sega CD original ran on a Genesis pad whose A/B/C sit in
 |   the same bottom row as the Saturn pad's, so muscle memory transfers; any
 |   other assignment would be a guess about what each button does in play,
 |   and update_keys (main.c:285) is not clear enough to redesign a control
 |   scheme around.
 |
 |   The Digital handle is a local rather than a file-static on purpose. Its
 |   constructor is trivial, but a file-static C++ object with a constructor
 |   runs at static-init time, before SRL::Core::Initialize() -- a local
 |   costs nothing and cannot be ordered wrong.
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
 | Globals: key_up, key_down, key_left, key_right, key_a, key_b, key_c
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void check_events(void)
{
	SRL::Input::Digital pad(0);

	if (!pad.IsConnected())
	{
		key_up = 0;
		key_down = 0;
		key_left = 0;
		key_right = 0;
		key_a = 0;
		key_b = 0;
		key_c = 0;
		return;
	}

	key_up = pad.IsHeld(SRL::Input::Digital::Button::Up) ? 1 : 0;
	key_down = pad.IsHeld(SRL::Input::Digital::Button::Down) ? 1 : 0;
	key_left = pad.IsHeld(SRL::Input::Digital::Button::Left) ? 1 : 0;
	key_right = pad.IsHeld(SRL::Input::Digital::Button::Right) ? 1 : 0;
	key_a = pad.IsHeld(SRL::Input::Digital::Button::A) ? 1 : 0;
	key_b = pad.IsHeld(SRL::Input::Digital::Button::B) ? 1 : 0;
	key_c = pad.IsHeld(SRL::Input::Digital::Button::C) ? 1 : 0;
}

/*----------------------
 | s_prevChord
 | Description: Last frame's chord, so a held combination fires once.
 | Author: suinevere
 ----------------------*/
static int s_prevChord = 0;

/*----------------------
 | input_debug_chord
 | Description: Reads Start plus A, Start plus B or Start plus C as an edge.
 | Author: suinevere
 | Dependencies: srl.hpp
 | Globals: s_prevChord
 | Params: N/A
 | Returns: 0 for nothing, 1 to save, 2 to load, 3 to toggle the target
 |          backup device
 ----------------------*/
extern "C" int input_debug_chord(void)
{
	SRL::Input::Digital pad(0);
	int chord = 0;

	if (pad.IsConnected() && pad.IsHeld(SRL::Input::Digital::Button::Start))
	{
		if (pad.IsHeld(SRL::Input::Digital::Button::A))
		{
			chord = 1;
		}
		else if (pad.IsHeld(SRL::Input::Digital::Button::C))
		{
			chord = 2;
		}
		else if (pad.IsHeld(SRL::Input::Digital::Button::B))
		{
			chord = 3;
		}
	}

	if (chord != 0 && chord == s_prevChord)
	{
		return 0;
	}
	s_prevChord = chord;
	return chord;
}

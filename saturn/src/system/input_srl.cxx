#include <srl.hpp>

#include "input.h"
#include "keymap.h"

static unsigned int g_padLatch = 0;

void check_events(void)
{
	unsigned int raw = input_raw_buttons() | g_padLatch;

	g_padLatch = 0;

	key_up    = (raw & PAD_BIT_UP)    ? 1 : 0;
	key_down  = (raw & PAD_BIT_DOWN)  ? 1 : 0;
	key_left  = (raw & PAD_BIT_LEFT)  ? 1 : 0;
	key_right = (raw & PAD_BIT_RIGHT) ? 1 : 0;

	keymap_apply(keymap_active(), raw, &key_a, &key_b, &key_c);
}

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

extern "C" void input_latch(void)
{
	g_padLatch |= input_raw_buttons();
}

extern "C" void input_latch_clear(void)
{
	g_padLatch = 0;
}

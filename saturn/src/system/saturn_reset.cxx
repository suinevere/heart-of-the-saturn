extern "C" {
#include "saturn_reset.h"
#include "disc.h"
#include "input.h"
#include "keymap.h"
}

#include <srl.hpp>
#include <sgl.h>
#include <sega_sys.h>

#define SMPC_COMREG (*(volatile uint8_t *)0x2010001Fu)
#define SMPC_SF     (*(volatile uint8_t *)0x20100063u)
#define SMPC_SYSRES 0x0Au
#define SMPC_TRIES  100000u

#define RESET_WAIT_FRAMES 120

extern "C" int saturn_system_reset(void)
{
    unsigned int spin;

    disc_stop_track();

    for (spin = 0; spin < SMPC_TRIES && (SMPC_SF & 1u) != 0u; spin++)
    {
    }

    if ((SMPC_SF & 1u) == 0u)
    {
        SMPC_SF = 1u;
        SMPC_COMREG = SMPC_SYSRES;
    }

    for (int frame = 0; frame < RESET_WAIT_FRAMES; frame++)
    {
        SRL::Core::Synchronize();
    }

    SYS_EXECDMP();

    for (int frame = 0; frame < RESET_WAIT_FRAMES; frame++)
    {
        SRL::Core::Synchronize();
    }

    return 0;
}

static int g_chordHeld = 0;
static int g_requested = 0;

extern "C" void saturn_reset_poll(void)
{
    int held = (input_raw_buttons() & SATURN_RESET_CHORD) == SATURN_RESET_CHORD;

    if (held && !g_chordHeld)
    {
        g_requested = 1;
    }

    g_chordHeld = held;
}

extern "C" int saturn_reset_taken(void)
{
    int taken = g_requested;

    g_requested = 0;
    return taken;
}

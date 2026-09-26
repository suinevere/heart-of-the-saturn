extern "C" {
#include "saturn_bootmark.h"
}

#if BOOTMARK_DIAG

#include <srl.hpp>

extern "C" void boot_mark(int step)
{
    SRL::Types::HighColor colour = SRL::Types::HighColor::Colors::Black;

    switch (step)
    {
    case BOOTMARK_PLATFORM:   colour = SRL::Types::HighColor::Colors::Blue;    break;
    case BOOTMARK_DISC:       colour = SRL::Types::HighColor::Colors::Green;   break;
    case BOOTMARK_INIT:       colour = SRL::Types::HighColor::Colors::Yellow;  break;
    case BOOTMARK_BOOTART:    colour = SRL::Types::HighColor::Colors::Magenta; break;
    case BOOTMARK_FIRSTFRAME: colour = SRL::Types::HighColor::Colors::White;   break;
    case 0: break;
    default: return;
    }

    SRL::VDP2::SetBackColor(colour);
}

#endif

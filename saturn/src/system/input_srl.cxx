/*----------------------
 | input_srl.cxx
 | Description: Saturn implementation of input.h. Sibling of host/input_sdl.c,
 |   and deliberately empty: pad mapping is a later sub-project, and the intro
 |   this sub-project boots to plays on a timer with no input at all. Leaving
 |   the key state at the zero main.c initialises it to makes the engine see
 |   nothing pressed every frame, which is exactly what an untouched pad would
 |   report -- so the game loop needs no branch for "no input backend yet".
 |   input.h names an empty check_events as the supported way to defer input
 |   rather than refusing to link.
 | Author: suinevere
 | Dependencies: input.h
 ----------------------*/

#include "input.h"

extern "C" {

/*----------------------
 | check_events
 | Description: No-op. There is no event queue to drain and no pad read yet;
 |   see the file banner. Costs nothing per frame and keeps the key state at
 |   its initial zero.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void check_events(void)
{
}

}

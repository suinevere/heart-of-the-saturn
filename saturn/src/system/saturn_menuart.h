/*----------------------
 | saturn_menuart.h
 | Description: The C face of the menu layer's artwork. saturn_menuart.cxx
 |   implements it against SRL's VDP1, CRAM and Scene2D, so callers never
 |   include <srl.hpp> -- the engine's headers wrap SGL's C headers in
 |   extern "C" and mixing that with SRL's C++ headers in one translation unit
 |   is fragile. This is the same seam saturn_bootart.h and saturn_platform.h
 |   draw.
 |
 |   Two modes, and the difference is what owns the screen. menu_art_begin(1)
 |   is exclusive: NBG0 is switched off and the boot art's title card is drawn
 |   behind the items, which is what the sub-title menu and the gate's load
 |   screen want. menu_art_begin(0) is an overlay: NBG0 is left alone, so the
 |   menu composites over the game's last presented frame, which is what the
 |   pause menu wants. VDP1 sprites draw in front of NBG0, which is what makes
 |   the overlay case free -- nothing is copied, no freeze buffer exists, and
 |   no engine state is touched.
 |
 |   Present nothing through video_present between menu_art_begin and
 |   menu_art_end; menu_art_present is the only thing that should reach the
 |   screen while the menu is up.
 | Author: suinevere
 | Dependencies: menu_layout.h
 ----------------------*/
#ifndef SATURN_MENUART_H
#define SATURN_MENUART_H

#include "menu_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | menu_art_load
 | Description: Loads MENUFONT.ART and the three panel files into VDP1
 |   textures and builds the two text ramps as CRAM banks. Loads everything up
 |   front, and never releases it, because VDP1's texture allocator is a bump
 |   allocator with no free -- a menu that reloaded on each open would exhaust
 |   sprite VRAM.
 | Author: suinevere
 | Params: N/A
 | Returns: 1 if every texture is resident, 0 if any file, header, bank or
 |          texture allocation failed
 ----------------------*/
int  menu_art_load(void);

/*----------------------
 | menu_art_begin
 | Description: Opens a menu. Exclusive mode hides NBG0 and draws the title
 |   card behind the items; overlay mode leaves NBG0 showing the game's last
 |   presented frame and composites the menu's sprites in front of it.
 | Author: suinevere
 | Params: exclusive -- non-zero for the sub-title menu and the gate's load
 |         screen, zero for the pause menu
 | Returns: N/A
 ----------------------*/
void menu_art_begin(int exclusive);

/*----------------------
 | menu_art_draw
 | Description: Queues one frame's sprites from a menu_layout_build list.
 | Author: suinevere
 | Params: items -- the draw list; count -- how many entries it holds
 | Returns: N/A
 ----------------------*/
void menu_art_draw(const MenuItem *items, int count);

/*----------------------
 | menu_art_fade
 | Description: Dims the menu's text ramps toward black, and the title card
 |   behind them with it.
 |
 |   Exists for the same reason boot_art_fade does: these are VDP1 sprites with
 |   their own CRAM banks, and video_set_fade reaches only the VDP2 palette the
 |   engine's bitmap layer draws through. A menu fading out has to call this;
 |   one fading out over a live game frame has to call video_set_fade as well,
 |   since the two layers are both on screen and only this one is a sprite.
 |
 |   Forwards to boot_art_fade rather than dimming the title card itself,
 |   because saturn_bootart.cxx owns that texture and its bank. That call is
 |   unconditional: exclusive mode is the only mode that draws the card, but the
 |   card's palette is shared with the boot screens, and leaving it undimmed
 |   here would mean a mode change mid-menu could reveal it at full brightness.
 |
 |   Repeating a level is free, so a caller may write it every frame.
 | Author: suinevere
 | Params: level -- 0 (black) to FADECALC_LEVEL_NORMAL (the ramps as built)
 | Returns: N/A
 ----------------------*/
void menu_art_fade(int level);

/*----------------------
 | menu_art_present
 | Description: Puts the queued sprites on screen and waits one vblank, which
 |   is what paces a menu loop.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_art_present(void);

/*----------------------
 | menu_art_end
 | Description: Closes a menu: turns the flash off and shows NBG0 again. The
 |   caller must present one more empty frame after this or the last menu
 |   frame's sprites stay composited over the game.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_art_end(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_MENUART_H */

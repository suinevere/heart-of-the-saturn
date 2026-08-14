/*----------------------
 | saturn_bootart.h
 | Description: The C face of the boot sequence's artwork. saturn_bootart.cxx
 |   implements it against SRL's TGA loader and VDP1, so callers never include
 |   <srl.hpp> -- the engine's headers wrap SGL's C headers in extern "C" and
 |   mixing that with SRL's C++ headers in one translation unit is fragile.
 |   This is the same seam saturn_platform.h draws for video, input and time.
 |
 |   The boot art owns the screen while it is up: NBG0 is switched off by
 |   boot_art_load and back on by boot_art_release, so the engine's bitmap
 |   layer cannot sit in front of the VDP1 sprites the screens are drawn as.
 |   Present nothing through video_present between the two.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SATURN_BOOTART_H
#define SATURN_BOOTART_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | boot_art_load
 | Description: Loads all six TGAs into VDP1 textures and hides NBG0. Loads
 |   everything up front rather than on demand because VDP1's texture
 |   allocator is a bump allocator with no free: an attract loop that
 |   reloaded on each replay would exhaust 512 KB of sprite VRAM in a few
 |   passes.
 | Author: suinevere
 | Params: N/A
 | Returns: 1 if every texture is resident, 0 if any file or allocation
 |          failed -- in which case NBG0 is left alone and boot_art_release
 |          need not be called
 ----------------------*/
int boot_art_load(void);

/*----------------------
 | boot_art_draw
 | Description: Queues this frame's sprites. Draws one for an opening still,
 |   two for the menu: the both-dim background and the lit logo band over it.
 | Author: suinevere
 | Params: screen -- a boot_screen value; highlight -- a boot_entry value,
 |         read only when screen is the menu
 | Returns: N/A
 ----------------------*/
void boot_art_draw(int screen, int highlight);

/*----------------------
 | boot_art_present
 | Description: Puts the queued sprites on screen and waits one vblank.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void boot_art_present(void);

/*----------------------
 | boot_art_release
 | Description: Shows NBG0 again. The textures are deliberately kept -- see
 |   the note in saturn_bootart.cxx. Safe to call without a successful load.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void boot_art_release(void);

/*----------------------
 | boot_art_fade
 | Description: Dims every boot screen's palette toward black, the way
 |   video_set_fade dims the engine's.
 |
 |   It has to exist separately because the two live on different hardware. The
 |   boot screens are VDP1 sprites drawn through their own CRAM banks;
 |   video_set_fade reaches only the VDP2 palette the engine's bitmap layer uses,
 |   which is switched off for the whole of boot_art_load's tenure. A fade
 |   between the game-select menu and the intro therefore has to call both, and
 |   the two share nothing but fadecalc's ladder and its level scale.
 |
 |   Applies to every screen rather than the one being shown, because the caller
 |   would otherwise have to say which, and a screen left undimmed is invisible
 |   until the frame it appears on. Seven banks of sixteen entries is one DMA
 |   each and no arithmetic worth counting.
 |
 |   Repeating a level is free -- the last one written is remembered and a
 |   second call at the same level returns at once -- so a caller may write it
 |   every frame.
 |
 |   Nothing puts the palettes back when a fade out finishes, and nothing may:
 |   VDP1 does not display an emptied command list until the next vblank, so the
 |   sprites the fade dimmed are still on screen and a CRAM write lands on them
 |   immediately -- a full brightness flash one frame before they disappear.
 |   Whoever next draws this art sets the level for itself first.
 | Author: suinevere
 | Params: level -- 0 (black) to FADECALC_LEVEL_NORMAL (the palettes as
 |         authored)
 | Returns: N/A
 ----------------------*/
void boot_art_fade(int level);

/*----------------------
 | boot_art_title_texture
 | Description: The BOOTTITL.ART texture id, so the sub-title menu can draw
 |   the title card without loading a second copy. VDP1's allocator is a bump
 |   allocator with no free, so a duplicate would cost 35 KB of sprite VRAM
 |   permanently and a second one per attract replay.
 | Author: suinevere
 | Params: N/A
 | Returns: the texture id, or -1 if the boot art never loaded
 ----------------------*/
int boot_art_title_texture(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BOOTART_H */

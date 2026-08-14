/*----------------------
 | video.h
 | Description: The single platform boundary between the engine and the screen.
 |   The engine renders into its own 304x192 8-bit paletted buffers with its
 |   own software rasterizer and hands a finished one across this line; nothing
 |   above it knows what a window, a texture or a VDP2 layer is.
 |   saturn/host/video_sdl.c is the SDL implementation, retained as the
 |   reference a wrong Saturn frame is compared against.
 |   src/system/video_srl.cxx is the Saturn implementation, on a VDP2 NBG0
 |   Paletted256 bitmap.
 |
 |   Call-ordering contract: video_init before anything else here, and
 |   video_create_surface before the first video_render. A palette must be set
 |   before the first video_render or the frame comes out black -- the engine
 |   already does this, but a backend must not assume a palette exists at
 |   video_create_surface time. video_toggle_fullscreen is a documented no-op
 |   rather than an error on backends with no window, because the engine calls
 |   it from a key handler that exists on both platforms.
 |
 |   Design: docs/superpowers/specs/2026-08-01-hota-saturn-boot-and-video-design.md
 | Author: suinevere
 | Dependencies: none (opaque parameters only; each backend pulls in its own)
 ----------------------*/
#ifndef VIDEO_H
#define VIDEO_H

/* The Saturn backend is C++; without this its definitions would get C++
   linkage and fail to satisfy the C callers. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | video_init
 | Description: Module initializer, called before anything else in this
 |   seam. Returns zero on success; the host backend has nothing to
 |   initialize and always succeeds, but the contract exists because a
 |   Saturn backend may need to set up VDP2 state before video_create_surface.
 | Author: suinevere
 ----------------------*/
int  video_init(void);

/*----------------------
 | video_create_surface
 | Description: Allocates the backend's drawable -- an SDL window, renderer,
 |   texture and palette on the host; a VDP2 NBG0 Paletted256 bitmap in VRAM
 |   on Saturn. Must run after video_init and before the first video_render.
 |   Returns zero on success, a negative failure code otherwise.
 | Author: suinevere
 ----------------------*/
int  video_create_surface(void);

/*----------------------
 | video_render
 | Description: Blits the engine's finished 304x192 8-bit paletted buffer
 |   to the screen. On the host this means scaling and packing into the
 |   SDL texture's pixel format; on Saturn the format already matches
 |   VDP2 Paletted256, so it is a straight copy, one memcpy per source
 |   line because the destination stride does not match the source pitch.
 |   It does not present the frame. Presentation is platform_frame's job,
 |   because the loop that owns the frame boundary is not always the caller
 |   here -- screen.c's update_screen renders inside run()'s iteration, and
 |   syncing here would sync twice for that frame.
 | Author: suinevere
 ----------------------*/
void video_render(char *src);

/*----------------------
 | video_set_palette
 | Description: Selects palette `which` out of game2.bin and applies it,
 |   converting its 4-bit-per-channel RGB444 entries to the backend's
 |   native color depth. A palette must be set before the first
 |   video_render or the frame comes out black.
 | Author: suinevere
 ----------------------*/
void video_set_palette(int which);

/*----------------------
 | video_set_palette_rgb12
 | Description: Applies a raw 16-entry RGB444 palette (the Sega CD's
 |   native format) directly, bypassing game2.bin lookup. Each 4-bit channel
 |   widens to the backend's native depth by bit replication, not by a left
 |   shift: the host does r | (r >> 4) to reach 8 bits and the Saturn backend
 |   does (v << 1) | (v >> 3) to reach the 5 bits of CRAM RGB555. A bare shift
 |   would leave the low bits zero, so full-intensity 15 would land at 30 of 31
 |   instead of 31 and every step of the ramp would sit low -- the whole image
 |   comes out slightly dark rather than visibly wrong, which is why this is
 |   stated here instead of left to the backends.
 | Author: suinevere
 ----------------------*/
void video_set_palette_rgb12(unsigned char *rgb12);

/*----------------------
 | video_set_fade
 | Description: Dims the whole displayed palette toward black without
 |   disturbing the palette itself. Level 0 is black and
 |   FADECALC_LEVEL_NORMAL (255) is the picture unchanged; the backends keep
 |   the raw RGB444 the engine last set and re-derive their native colours
 |   from it on every call, so a fade out followed by a fade in restores the
 |   original entries exactly rather than accumulating rounding.
 |
 |   Setting a palette does not clear the fade. That is deliberate: a scene
 |   change under a black screen sets its palette first and fades in after,
 |   and a video_set_palette that reset the level to normal would flash the
 |   new scene at full brightness for one frame before the fade in started.
 |   Callers that mean "stop fading" say so, with
 |   video_set_fade(FADECALC_LEVEL_NORMAL).
 |
 |   This is a palette operation, not an animation: it applies one level and
 |   returns. Walking the ladder and holding each step belongs to the caller,
 |   which is the only place that knows what it is waiting for -- see
 |   fadecalc.h for the ladder and for why a Sega CD fade is eight held
 |   pictures rather than a per-frame ramp.
 | Author: suinevere
 ----------------------*/
void video_set_fade(int level);

/*----------------------
 | video_get_fade
 | Description: Returns the level video_set_fade was last called with, or
 |   FADECALC_LEVEL_NORMAL if it never was.
 |
 |   Exists so a caller can ask whether the screen is already black before
 |   spending a fade on it. Every write of the level goes through
 |   video_set_fade, so the backend is the only place the answer is actually
 |   known -- three modules now black the screen and any shadow copy kept
 |   above this line would go stale the first time one of the others wrote.
 | Author: suinevere
 ----------------------*/
int  video_get_fade(void);

/*----------------------
 | video_get_current_palette
 | Description: Returns the index last passed to video_set_palette, so
 |   callers that only track palette state (not raw RGB12) can query it
 |   back.
 | Author: suinevere
 ----------------------*/
int  video_get_current_palette(void);

/*----------------------
 | video_set_scroll
 | Description: Sets the vertical scroll offset applied at the next
 |   video_render. On the host backend this is a shadow value consumed by
 |   row-shifting copy branches in video_render -- there is no hardware
 |   scroll, so the SDL backend fakes it by blitting each row to a shifted
 |   destination. On the Saturn backend this instead becomes a write to
 |   the VDP2 layer's scroll position register: the pixels themselves
 |   never move, and the row-shifting branches do not exist there at all.
 | Author: suinevere
 ----------------------*/
void video_set_scroll(int scroll);

/*----------------------
 | video_get_scroll_register
 | Description: Returns the value last passed to video_set_scroll. It
 |   exists as its own call, separate from video_set_scroll, because the
 |   engine reads the scroll value back rather than tracking it itself.
 | Author: suinevere
 ----------------------*/
int  video_get_scroll_register(void);

/*----------------------
 | video_toggle_fullscreen
 | Description: Flips the host window between windowed and fullscreen
 |   mode. A documented no-op on backends with no window -- Saturn has no
 |   concept of fullscreen -- rather than an error, because the engine
 |   calls it unconditionally from a key handler that exists on both
 |   platforms.
 | Author: suinevere
 ----------------------*/
void video_toggle_fullscreen(void);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_H */

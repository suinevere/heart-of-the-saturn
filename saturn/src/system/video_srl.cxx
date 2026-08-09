/*----------------------
 | video_srl.cxx
 | Description: Saturn implementation of video.h, on a VDP2 NBG0 Paletted256
 |   bitmap. Sibling of host/video_sdl.c: same nine functions, no window, no
 |   texture, no scalers. The engine's screens are already 8-bit paletted and
 |   VDP2 reads exactly that, so video_render is a copy and never a conversion
 |   -- the whole per-pixel packing loop the SDL backend needs to reach a
 |   32-bit texture has no counterpart here.
 |
 |   Two shapes differ from the host and drive everything below. The engine's
 |   buffer is 304x192 but VDP2 bitmap containers are fixed, so the image
 |   lives at the top-left of a 512x256 container: 192 line copies of 304
 |   bytes, source pitch 304, destination stride 512. And the 304x192 image is
 |   centred in NTSC's 320x224 by moving the layer, not by scaling -- VDP2
 |   cannot scale a bitmap layer without eating a second set of VRAM access
 |   cycles, and the host's 2x/3x pixel doublers are a desktop-window comfort
 |   with nothing to do on a fixed-resolution display.
 |
 |   That same layer position carries the vertical scroll, which is the point
 |   of this seam: video_sdl.c has three row-shifting branches that re-blit
 |   every line to a shifted destination because SDL has no scroll register,
 |   and here the pixels do not move at all.
 |
 |   NBG0 is set up by hand (AutoAllocateBmp, a CRAM bank, Init) rather than
 |   through LoadBitmap, because LoadBitmap exists to push an image into VRAM
 |   and there is no first image to push -- taking it would mean allocating
 |   and freeing a 58 KB blank page at startup purely to feed a copy whose
 |   result is overwritten immediately below.
 |
 |   Design: docs/superpowers/specs/2026-08-01-hota-saturn-boot-and-video-design.md
 | Author: suinevere
 | Dependencies: srl.hpp, saturn_compat.h, video.h, game2bin.h
 ----------------------*/
#include <srl.hpp>
#include "saturn_compat.h"

#include "video.h"
#include "game2bin.h"

/*----------------------
 | SCREEN_W / SCREEN_H / VRAM_PITCH / VRAM_ROWS / OFFSET_X / OFFSET_Y
 | Description: The engine's frame is 304x192 at 8bpp, so a source line is 304
 |   bytes. The smallest VDP2 bitmap container that holds it is 512x256, whose
 |   line stride is 512 bytes -- source and destination pitches differ, which
 |   is why the blit is per-line rather than one copy. OFFSET_X/OFFSET_Y are
 |   the margins that centre 304x192 in NTSC's 320x224; they are applied as a
 |   negative layer position, never as a scale.
 | Author: suinevere
 ----------------------*/
#define SCREEN_W    304
#define SCREEN_H    192
#define VRAM_PITCH  512
#define VRAM_ROWS   256
#define OFFSET_X    ((320 - SCREEN_W) / 2)
#define OFFSET_Y    ((224 - SCREEN_H) / 2)

/*----------------------
 | BORDER_INDEX / BORDER_WORD
 | Description: The container is wider and taller than the image, and both the
 |   margins and the rows a scrolled layer wraps in from show whatever those
 |   spare pixels hold. The engine only ever writes indices 0-15 and 255, so
 |   16 is free: the padding is filled with it once and CRAM entry 16 is held
 |   at black, which makes the border read as letterboxing rather than as
 |   whatever colour the current palette happens to put in entry 0.
 |   BORDER_WORD is the same index four times over, because VDP2 VRAM wants
 |   word or longword access and a byte-wise memset is not guaranteed to land.
 | Author: suinevere
 ----------------------*/
#define BORDER_INDEX 16
#define BORDER_WORD  0x10101010u

/*----------------------
 | PALETTE_BASE
 | Description: Offset of the palette table inside GAME2.BIN, as video_sdl.c
 |   reads it. Sixteen RGB444 entries of two bytes each per palette.
 | Author: suinevere
 ----------------------*/
#define PALETTE_BASE 0x5cb8

/*----------------------
 | g_colors
 | Description: The 256 CRAM entries in work RAM, flushed to the hardware bank
 |   whenever they change. Only 0-15 come from the game, 16 is the border and
 |   255 is video_sdl.c's magenta out-of-range marker; the rest stay black.
 |   Kept here rather than written straight to CRAM so a palette set before
 |   video_create_surface -- which video.h's contract explicitly allows -- has
 |   somewhere to land.
 | Author: suinevere
 ----------------------*/
static SRL::Types::HighColor g_colors[256];

/*----------------------
 | g_vram
 | Description: Where NBG0's bitmap container lives in VDP2 VRAM, captured by
 |   video_create_surface so each frame can be written straight there. Null
 |   until then, which is what makes a video_render before it a no-op instead
 |   of a write into unallocated VRAM.
 | Author: suinevere
 ----------------------*/
static uint8_t *g_vram = nullptr;

/*----------------------
 | g_currentPalette
 | Description: The index last passed to video_set_palette, handed back by
 |   video_get_current_palette.
 | Author: suinevere
 ----------------------*/
static int g_currentPalette = 0;

/*----------------------
 | g_scrollShadow
 | Description: The value last passed to video_set_scroll, returned by
 |   video_get_scroll_register. It is a shadow of the layer's vertical
 |   position rather than the source of it, and video_render clears it after
 |   applying it, because video_sdl.c's scroll_reg is one-shot: it is consumed
 |   by one blit and reset to zero.
 | Author: suinevere
 ----------------------*/
static int g_scrollShadow = 0;

/*----------------------
 | g_offsetRegistered
 | Description: Whether NBG0 has been pointed at VDP2 colour offset A yet.
 |   Registration is deferred to the first video_set_brightness call so a
 |   build that never fades never claims the register, and latched here so
 |   every later call is a single write to the offset itself.
 | Author: suinevere
 ----------------------*/
static int g_offsetRegistered = 0;

/*----------------------
 | g_fadeTotal / g_fadeLeft
 | Description: A fade-in measured in rendered frames. g_fadeTotal is the
 |   length video_fade_in was asked for and g_fadeLeft counts down once per
 |   video_render, so the ramp advances only when the viewer is actually being
 |   shown something. Both zero means no fade is running, which is the state
 |   every path outside a seam is in.
 | Author: suinevere
 ----------------------*/
static int g_fadeTotal = 0;
static int g_fadeLeft = 0;

/*----------------------
 | g_stage
 | Description: One aligned source line, used only when the engine hands over
 |   a buffer that is not longword-aligned. slDMACopy moves the frame into
 |   VDP2 VRAM and neither end of an SCU transfer tolerates a misaligned
 |   address, so an odd source is staged through here rather than dropped to a
 |   byte-wise copy -- byte writes into VDP2 VRAM are not reliable.
 | Author: suinevere
 ----------------------*/
static uint8_t g_stage[SCREEN_W] __attribute__((aligned(4)));

/*----------------------
 | palette_flush
 | Description: Pushes the 256 work-RAM colours into the layer's CRAM bank.
 |   Guarded on the bank existing, so the palette calls the engine makes
 |   before video_create_surface update g_colors and are picked up when the
 |   bank is allocated instead of writing to a null CRAM address.
 | Author: suinevere
 | Globals: g_colors
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void palette_flush(void)
{
	if (SRL::VDP2::NBG0::TilePalette.GetData() != nullptr)
	{
		SRL::VDP2::NBG0::TilePalette.Load(g_colors, 256);
	}
}

/*----------------------
 | apply_position
 | Description: Writes the layer's screen position: the centring margins plus
 |   whatever vertical scroll is pending. Negative, because moving the layer's
 |   origin up and left is what puts the image down and right on screen.
 |   Positive scroll shifts the image down, matching video_sdl.c's "scroll
 |   from top" branch, which fills the top p lines and pushes the image down
 |   by p.
 | Author: suinevere
 | Globals: g_scrollShadow
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void apply_position(void)
{
	SRL::Math::Types::Vector2D pos(SRL::Math::Types::Fxp((int16_t)(-OFFSET_X)),
	                               SRL::Math::Types::Fxp((int16_t)(-(OFFSET_Y + g_scrollShadow))));

	SRL::VDP2::NBG0::SetPosition(pos);
}

extern "C" {

/*----------------------
 | video_init
 | Description: Brings the palette to a known state before anything can read
 |   it. Every entry starts black, which makes an unset palette a black frame
 |   rather than a CRAM bank full of whatever was left there, and holds entry
 |   16 at the black the border is filled with. Allocates nothing -- VDP2 VRAM
 |   and the CRAM bank are video_create_surface's job -- so it cannot fail.
 | Author: suinevere
 | Globals: g_colors
 | Params: N/A
 | Returns: zero
 ----------------------*/
int video_init(void)
{
	for (int32_t i = 0; i < 256; i++)
	{
		g_colors[i] = SRL::Types::HighColor::FromRGB555(0, 0, 0);
	}

	return 0;
}

/*----------------------
 | video_create_surface
 | Description: Allocates the drawable: a 512x256 Paletted256 bitmap container
 |   in VDP2 VRAM and a 256-colour CRAM bank for it, then configures NBG0 to
 |   display it centred. The container is filled with BORDER_INDEX rather than
 |   left blank so the margins and the wrap-in rows are black from the first
 |   frame. Idempotent: a second call returns success without allocating a
 |   second container.
 | Author: suinevere
 | Globals: g_vram
 | Params: N/A
 | Returns: 0 on success, negative on failure
 ----------------------*/
int video_create_surface(void)
{
	if (g_vram != nullptr)
	{
		return 0;
	}

	SRL::Bitmap::BitmapInfo info(SCREEN_W, SCREEN_H);
	info.ColorMode = SRL::CRAM::TextureColorMode::Paletted256;

	int allocSize = 0;
	void *cell = SRL::VDP2::VRAM::AutoAllocateBmp(info, SRL::VDP2::NBG0::ScreenID, &allocSize);

	if (cell == nullptr)
	{
		printf("video_create_surface: no VDP2 VRAM for a 512x256 8bpp bitmap\n");
		return -1;
	}

	int32_t bank = SRL::CRAM::GetFreeBank(SRL::CRAM::TextureColorMode::Paletted256);

	if (bank < 0)
	{
		printf("video_create_surface: no free 256-colour CRAM bank\n");
		return -2;
	}

	SRL::VDP2::NBG0::CellAddress = cell;
	SRL::VDP2::NBG0::CellAllocSize = allocSize;

	SRL::CRAM::SetBankUsedState((uint16_t)bank, SRL::CRAM::TextureColorMode::Paletted256, true);
	SRL::VDP2::NBG0::TilePalette = SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted256, (uint16_t)bank);

	uint32_t *fill = (uint32_t *)cell;

	for (int32_t i = 0; i < (VRAM_PITCH * VRAM_ROWS) / 4; i++)
	{
		fill[i] = BORDER_WORD;
	}

	g_vram = (uint8_t *)cell;

	palette_flush();

	SRL::VDP2::NBG0::Init(info);
	SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer2);
	apply_position();
	SRL::VDP2::NBG0::ScrollEnable();

	return 0;
}

/*----------------------
 | video_render
 | Description: Copies the engine's finished 304x192 frame into the bitmap
 |   container, one DMA per source line because the 304-byte source pitch and
 |   the 512-byte container stride do not match. No conversion happens: the
 |   engine's bytes are already VDP2 palette indices. The pending scroll is
 |   applied to the layer here and then cleared, which is what makes it
 |   one-shot the way video_sdl.c's scroll_reg is -- a frame with no
 |   video_set_scroll before it renders unscrolled.
 | Author: suinevere
 | Globals: g_vram, g_scrollShadow, g_stage
 | Params: src -- the engine's 304x192 8-bit paletted buffer
 | Returns: N/A
 ----------------------*/
void video_render(char *src)
{
	if (src == nullptr || g_vram == nullptr)
	{
		return;
	}

	uint8_t *s = (uint8_t *)src;
	uint8_t *d = g_vram;

	if ((((uint32_t)s) & 3u) == 0u)
	{
		for (int32_t line = 0; line < SCREEN_H; line++)
		{
			slDMACopy((void *)s, (void *)d, SCREEN_W);
			s += SCREEN_W;
			d += VRAM_PITCH;
		}

		slDMAWait();
	}
	else
	{
		for (int32_t line = 0; line < SCREEN_H; line++)
		{
			memcpy(g_stage, s, SCREEN_W);
			slDMACopy((void *)g_stage, (void *)d, SCREEN_W);
			slDMAWait();
			s += SCREEN_W;
			d += VRAM_PITCH;
		}
	}

	apply_position();
	g_scrollShadow = 0;

	if (g_fadeLeft > 0)
	{
		g_fadeLeft--;
		video_set_brightness((255 * (g_fadeTotal - g_fadeLeft)) / g_fadeTotal);
	}
}

/*----------------------
 | video_set_palette
 | Description: Reads palette `which` out of GAME2.BIN and applies it, then
 |   overwrites entry 255 with magenta exactly as video_sdl.c does -- it is
 |   the marker for pixels outside the game's 16 colours, and keeping it makes
 |   a wrong Saturn frame comparable against a host one.
 | Author: suinevere
 | Globals: g_currentPalette, g_colors
 | Params: which -- palette index
 | Returns: N/A
 ----------------------*/
void video_set_palette(int which)
{
	unsigned char rgb12[16 * 2];

	copy_from_game2bin(rgb12, PALETTE_BASE + (which * 16 * 2), sizeof(rgb12));

	g_currentPalette = which;
	video_set_palette_rgb12(rgb12);

	g_colors[255] = SRL::Types::HighColor::FromRGB555(31, 0, 31);
	palette_flush();
}

/*----------------------
 | video_set_palette_rgb12
 | Description: Converts a raw 16-entry Sega CD palette into CRAM. Each entry
 |   is a big-endian word whose nibbles run 0BGR, which is what video_sdl.c
 |   reads (r = c & 0xf, g = (c >> 4) & 0xf, b = (c >> 8) & 0xf) rather than
 |   what the RGB444 name suggests. Widening 4 bits to 5 is (v << 1) | (v >> 3)
 |   and not a bare shift, for the same reason video_sdl.c widens with
 |   r | (r >> 4) going to 8 bits: replicating the top bit into the vacated
 |   low bit is what makes 15 reach full intensity instead of stopping one
 |   step short, and keeps the ramp even.
 | Author: suinevere
 | Globals: g_colors
 | Params: rgb12 -- 16 entries of two bytes each
 | Returns: N/A
 ----------------------*/
void video_set_palette_rgb12(unsigned char *rgb12)
{
	if (rgb12 == nullptr)
	{
		return;
	}

	for (int32_t i = 0; i < 16; i++)
	{
		const uint32_t c = ((uint32_t)rgb12[i * 2] << 8) | (uint32_t)rgb12[i * 2 + 1];
		const uint8_t r = (uint8_t)(c & 0xf);
		const uint8_t g = (uint8_t)((c >> 4) & 0xf);
		const uint8_t b = (uint8_t)((c >> 8) & 0xf);

		g_colors[i] = SRL::Types::HighColor::FromRGB555((uint8_t)((r << 1) | (r >> 3)),
		                                                (uint8_t)((g << 1) | (g >> 3)),
		                                                (uint8_t)((b << 1) | (b >> 3)));
	}

	palette_flush();
}

/*----------------------
 | video_get_current_palette
 | Description: Hands back the index last passed to video_set_palette, for
 |   callers that track palette state without holding the raw RGB12.
 | Author: suinevere
 | Globals: g_currentPalette
 | Params: N/A
 | Returns: the current palette index
 ----------------------*/
int video_get_current_palette(void)
{
	return g_currentPalette;
}

/*----------------------
 | video_set_scroll
 | Description: Stores the pending vertical scroll and writes it to the
 |   layer's position straight away, so the value is live even if the caller
 |   never reaches a video_render. Nothing in VRAM is touched: unlike the SDL
 |   backend, which has to re-blit every row to a shifted destination, the
 |   pixels here do not move -- the hardware reads them from a different
 |   starting line.
 | Author: suinevere
 | Globals: g_scrollShadow
 | Params: scroll -- lines to shift the image down; negative shifts it up
 | Returns: N/A
 ----------------------*/
void video_set_scroll(int scroll)
{
	g_scrollShadow = scroll;
	apply_position();
}

/*----------------------
 | video_get_scroll_register
 | Description: Returns the shadow of the last video_set_scroll, because the
 |   engine reads the value back rather than tracking it itself. It reads zero
 |   after a video_render, which is the same one-shot lifetime video_sdl.c
 |   gives scroll_reg.
 | Author: suinevere
 | Globals: g_scrollShadow
 | Params: N/A
 | Returns: the pending scroll in lines
 ----------------------*/
int video_get_scroll_register(void)
{
	return g_scrollShadow;
}

/*----------------------
 | video_toggle_fullscreen
 | Description: Empty on purpose. The Saturn has one display mode and no
 |   window, so there is nothing to flip; video.h makes this a documented
 |   no-op rather than an error because the engine calls it unconditionally
 |   from a key handler shared with the host.
 | Author: suinevere
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void video_toggle_fullscreen(void)
{
}

/*----------------------
 | video_set_brightness
 | Description: Dims NBG0 toward black through VDP2 colour offset A. The
 |   offset is a signed per-channel value added after every other colour
 |   calculation, so a negative offset subtracts brightness from the whole
 |   layer without touching a single pixel the engine drew -- which is the
 |   only reason this is affordable during a disc read, when nothing is
 |   redrawing anyway.
 |
 |   NBG0 is registered onto offset A on the first call rather than in
 |   video_init, so a build that never fades never spends the register. The
 |   registration is idempotent and the flag makes it a single write
 |   afterwards.
 |
 |   Level is clamped, not trusted: video.h documents 0..255 and a caller
 |   computing one from a 0..7 CD-DA level can land outside it by rounding.
 | Author: suinevere
 | Globals: g_offsetRegistered
 | Params: level -- 0 (black) to 255 (normal)
 | Returns: N/A
 ----------------------*/
void video_set_brightness(int level)
{
	int drop;

	if (level < 0)
	{
		level = 0;
	}

	if (level > 255)
	{
		level = 255;
	}

	if (!g_offsetRegistered)
	{
		SRL::VDP2::NBG0::UseColorOffset(SRL::VDP2::OffsetChannel::OffsetA);
		g_offsetRegistered = 1;
	}

	drop = level - 255;

	SRL::VDP2::ColorOffset offset((int16_t)drop, (int16_t)drop, (int16_t)drop);
	SRL::VDP2::SetColorOffsetA(offset);
}

/*----------------------
 | video_fade_in
 | Description: Arms a ramp back to full brightness, advanced one step per
 |   video_render. The picture is blacked here rather than left where it was,
 |   so a caller that armed the fade after the screen had already been
 |   restored still gets a fade rather than a jump.
 | Author: suinevere
 | Globals: g_fadeTotal, g_fadeLeft
 | Params: frames -- rendered frames to ramp over; 0 or less restores at once
 | Returns: N/A
 ----------------------*/
void video_fade_in(int frames)
{
	if (frames <= 0)
	{
		g_fadeTotal = 0;
		g_fadeLeft = 0;
		video_set_brightness(255);
		return;
	}

	g_fadeTotal = frames;
	g_fadeLeft = frames;
	video_set_brightness(0);
}

}

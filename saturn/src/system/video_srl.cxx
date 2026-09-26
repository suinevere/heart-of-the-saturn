#include <srl.hpp>
#include "saturn_compat.h"

#include "video.h"
#include "game2bin.h"
#include "fadecalc.h"

#define SCREEN_W    304
#define SCREEN_H    192
#define VRAM_PITCH  512
#define VRAM_ROWS   256
#define OFFSET_X    ((320 - SCREEN_W) / 2)
#define OFFSET_Y    ((224 - SCREEN_H) / 2)

#define BORDER_INDEX 16
#define BORDER_WORD  0x10101010u

#define PALETTE_BASE 0x5cb8

static SRL::Types::HighColor g_colors[256];

static uint8_t *g_vram = nullptr;

static int g_currentPalette = 0;

static unsigned char g_srcRgb12[16 * 2];
static int g_fadeLevel = FADECALC_LEVEL_NORMAL;

static int g_scrollShadow = 0;

static uint8_t g_stage[SCREEN_W] __attribute__((aligned(4)));

static void palette_flush(void)
{
	if (SRL::VDP2::NBG0::TilePalette.GetData() != nullptr)
	{
		SRL::VDP2::NBG0::TilePalette.Load(g_colors, 256);
	}
}

static void apply_position(void)
{
	SRL::Math::Types::Vector2D pos(SRL::Math::Types::Fxp((int16_t)(-OFFSET_X)),
	                               SRL::Math::Types::Fxp((int16_t)(-(OFFSET_Y + g_scrollShadow))));

	SRL::VDP2::NBG0::SetPosition(pos);
}

extern "C" {

int video_init(void)
{
	for (int32_t i = 0; i < 256; i++)
	{
		g_colors[i] = SRL::Types::HighColor::FromRGB555(0, 0, 0);
	}

	return 0;
}

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
}

void video_set_palette(int which)
{
	unsigned char rgb12[16 * 2];

	copy_from_game2bin(rgb12, PALETTE_BASE + (which * 16 * 2), sizeof(rgb12));

	g_currentPalette = which;
	video_set_palette_rgb12(rgb12);

	g_colors[255] = SRL::Types::HighColor::FromRGB555(31, 0, 31);
	palette_flush();
}

static void palette_rebuild(void)
{
	for (int32_t i = 0; i < 16; i++)
	{
		const uint32_t c = ((uint32_t)g_srcRgb12[i * 2] << 8) | (uint32_t)g_srcRgb12[i * 2 + 1];
		const int r = (int)(c & 0xf);
		const int g = (int)((c >> 4) & 0xf);
		const int b = (int)((c >> 8) & 0xf);

		const int r5 = fadecalc_scale((r << 1) | (r >> 3), g_fadeLevel);
		const int g5 = fadecalc_scale((g << 1) | (g >> 3), g_fadeLevel);
		const int b5 = fadecalc_scale((b << 1) | (b >> 3), g_fadeLevel);

		g_colors[i] = SRL::Types::HighColor::FromRGB555((uint8_t)r5, (uint8_t)g5, (uint8_t)b5);
	}

	palette_flush();
}

void video_set_palette_rgb12(unsigned char *rgb12)
{
	if (rgb12 == nullptr)
	{
		return;
	}

	memcpy(g_srcRgb12, rgb12, sizeof(g_srcRgb12));
	palette_rebuild();
}

void video_set_fade(int level)
{
	g_fadeLevel = level;
	palette_rebuild();
}

int video_get_fade(void)
{
	return g_fadeLevel;
}

int video_get_current_palette(void)
{
	return g_currentPalette;
}

void video_set_scroll(int scroll)
{
	g_scrollShadow = scroll;
	apply_position();
}

int video_get_scroll_register(void)
{
	return g_scrollShadow;
}

void video_toggle_fullscreen(void)
{
}

}

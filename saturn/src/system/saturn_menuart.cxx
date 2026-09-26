#include <srl.hpp>
#include <stdio.h>
#include "saturn_menuart.h"
#include "disc.h"
#include "saturn_bootart.h"
#include "saturn_compat.h"
#include "fadecalc.h"
#include "saturn_reset.h"

using namespace SRL::Math::Types;

#define MENU_ART_Z_BACK   480
#define MENU_ART_Z_PANEL  470
#define MENU_ART_Z_GLYPH  460

#define MENU_ART_STAGE_BYTES 2600

#define MENU_ART_MAGIC 0x4241

#define MENU_ART_PAL_ENTRIES 16
#define MENU_ART_PIXELS_AT   (8 + 2 * MENU_ART_PAL_ENTRIES)

#define MENU_ART_GLYPH_W     8
#define MENU_ART_GLYPH_H     10
#define MENU_ART_GLYPH_BYTES ((MENU_ART_GLYPH_W * MENU_ART_GLYPH_H) / 2)
#define MENU_ART_GLYPH_COUNT 64

#define MENU_ART_IDX_FILL         1
#define MENU_ART_IDX_PANEL_FILL   2
#define MENU_ART_IDX_PANEL_BORDER 3
#define MENU_ART_IDX_SEL          4
#define MENU_ART_IDX_OUTLINE      5
#define MENU_ART_IDX_TITLE_DIM    6
#define MENU_ART_IDX_TITLE_SEL    7

#define MENU_ART_SCREEN_W 320
#define MENU_ART_SCREEN_H 224

static int32_t g_glyph[MENU_ART_GLYPH_COUNT] =
{
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1
};

static SRL::Types::HighColor g_boxFill;
static SRL::Types::HighColor g_boxBorder;
#define MENU_ART_BANK_COUNT 4

static int32_t g_bank[MENU_ART_BANK_COUNT] = { -1, -1, -1, -1 };
static SRL::Types::HighColor g_pal[MENU_ART_BANK_COUNT][MENU_ART_PAL_ENTRIES];

static int g_artLevel = FADECALC_LEVEL_NORMAL;

static int g_loaded;

static int g_exclusive;

static void menu_art_sprite(int32_t texture, int16_t x, int16_t y, int16_t w,
                            int16_t h, int16_t z, SRL::CRAM::Palette *ramp)
{
    int16_t cx;
    int16_t cy;

    if (texture < 0)
    {
        return;
    }

    cx = (int16_t)(x + (w / 2) - (MENU_ART_SCREEN_W / 2));
    cy = (int16_t)(y + (h / 2) - (MENU_ART_SCREEN_H / 2));

    SRL::Scene2D::DrawSprite((uint16_t)texture, ramp, Vector3D(cx, cy, z));
}

static int32_t menu_art_bank(const uint8_t *file, int fillIndex,
                             int outlineIndex, SRL::Types::HighColor *keep)
{
    int32_t bank;
    int i;

    bank = SRL::CRAM::GetFreeBank(SRL::CRAM::TextureColorMode::Paletted16);

    if (bank < 0)
    {
        printf("menu_art_load: no CRAM bank\n");
        return -1;
    }

    for (i = 0; i < MENU_ART_PAL_ENTRIES; i++)
    {
        keep[i] = ((SRL::Types::HighColor *)(file + 8))[i];
    }
    keep[MENU_ART_IDX_FILL] = keep[fillIndex];
    keep[MENU_ART_IDX_OUTLINE] = keep[outlineIndex];

    SRL::CRAM::SetBankUsedState((uint16_t)bank,
        SRL::CRAM::TextureColorMode::Paletted16, true);
    SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16, (uint16_t)bank)
        .Load(keep, (int16_t)MENU_ART_PAL_ENTRIES);

    return bank;
}

static int menu_art_release_banks(void)
{
    int i;

    for (i = 0; i < MENU_ART_BANK_COUNT; i++)
    {
        if (g_bank[i] >= 0)
        {
            SRL::CRAM::SetBankUsedState((uint16_t)g_bank[i],
                SRL::CRAM::TextureColorMode::Paletted16, false);
            g_bank[i] = -1;
        }
    }
    return 0;
}

extern "C" int menu_art_load(void)
{
    uint8_t *stage;
    int i;

    if (g_loaded)
    {
        return 1;
    }

    stage = (uint8_t *)saturn_lwram_alloc(MENU_ART_STAGE_BYTES);

    if (stage == 0)
    {
        printf("menu_art_load: no LWRAM for the staging buffer\n");
        return 0;
    }

    if (disc_read_file("MENUFONT.ART", stage, MENU_ART_STAGE_BYTES) < 0)
    {
        printf("menu_art_load: MENUFONT.ART did not read\n");
        saturn_lwram_free(stage);
        return 0;
    }

    if (((stage[0] << 8) | stage[1]) != MENU_ART_MAGIC)
    {
        printf("menu_art_load: MENUFONT.ART has the wrong magic\n");
        menu_art_release_banks();
        saturn_lwram_free(stage);
        return 0;
    }

    if (((stage[2] << 8) | stage[3]) != MENU_ART_GLYPH_W ||
        ((stage[4] << 8) | stage[5]) != MENU_ART_GLYPH_H * MENU_ART_GLYPH_COUNT ||
        ((stage[6] << 8) | stage[7]) != MENU_ART_PAL_ENTRIES)
    {
        printf("menu_art_load: MENUFONT.ART is %ux%u with %u palette entries\n",
               (unsigned)((stage[2] << 8) | stage[3]),
               (unsigned)((stage[4] << 8) | stage[5]),
               (unsigned)((stage[6] << 8) | stage[7]));
        menu_art_release_banks();
        saturn_lwram_free(stage);
        return 0;
    }

    g_bank[MENU_RAMP_DIM] = menu_art_bank(stage, MENU_ART_IDX_FILL,
        MENU_ART_IDX_PANEL_FILL, g_pal[MENU_RAMP_DIM]);
    g_bank[MENU_RAMP_SEL] = menu_art_bank(stage, MENU_ART_IDX_SEL,
        MENU_ART_IDX_PANEL_FILL, g_pal[MENU_RAMP_SEL]);
    g_bank[MENU_RAMP_TITLE_DIM] = menu_art_bank(stage, MENU_ART_IDX_TITLE_DIM,
        MENU_ART_IDX_OUTLINE, g_pal[MENU_RAMP_TITLE_DIM]);
    g_bank[MENU_RAMP_TITLE_SEL] = menu_art_bank(stage, MENU_ART_IDX_TITLE_SEL,
        MENU_ART_IDX_OUTLINE, g_pal[MENU_RAMP_TITLE_SEL]);
    g_artLevel = FADECALC_LEVEL_NORMAL;

    for (i = 0; i < MENU_ART_BANK_COUNT; i++)
    {
        if (g_bank[i] < 0)
        {
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }
    }

    for (i = 0; i < MENU_ART_GLYPH_COUNT; i++)
    {
        if (g_glyph[i] >= 0)
        {
            continue;
        }

        g_glyph[i] = SRL::VDP1::TryLoadTexture(MENU_ART_GLYPH_W,
            MENU_ART_GLYPH_H, SRL::CRAM::TextureColorMode::Paletted16,
            (uint16_t)g_bank[MENU_RAMP_DIM],
            stage + MENU_ART_PIXELS_AT + i * MENU_ART_GLYPH_BYTES);

        if (g_glyph[i] < 0)
        {
            printf("menu_art_load: glyph %d got no VDP1 texture\n", i);
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }
    }

    g_boxFill = ((SRL::Types::HighColor *)(stage + 8))[MENU_ART_IDX_PANEL_FILL];
    g_boxBorder =
        ((SRL::Types::HighColor *)(stage + 8))[MENU_ART_IDX_PANEL_BORDER];

    saturn_lwram_free(stage);
    g_loaded = 1;
    return 1;
}

static SRL::Types::HighColor menu_art_dim(SRL::Types::HighColor c)
{
    c.Red = (uint16_t)fadecalc_scale((int)c.Red, g_artLevel);
    c.Green = (uint16_t)fadecalc_scale((int)c.Green, g_artLevel);
    c.Blue = (uint16_t)fadecalc_scale((int)c.Blue, g_artLevel);
    return c;
}

static void menu_art_rect(int id, int16_t x, int16_t y, int16_t w, int16_t h)
{
    SRL::Math::Types::Vector2D points[4];
    int16_t left = (int16_t)(x - (MENU_ART_SCREEN_W / 2));
    int16_t top = (int16_t)(y - (MENU_ART_SCREEN_H / 2));
    int16_t right = (int16_t)(left + w - 1);
    int16_t bottom = (int16_t)(top + h - 1);

    if (w <= 0 || h <= 0)
    {
        return;
    }

    points[0] = SRL::Math::Types::Vector2D(left, top);
    points[1] = SRL::Math::Types::Vector2D(right, top);
    points[2] = SRL::Math::Types::Vector2D(right, bottom);
    points[3] = SRL::Math::Types::Vector2D(left, bottom);

    SRL::Scene2D::DrawPolygon(points, true,
        menu_art_dim(id == MENU_RECT_BORDER ? g_boxBorder : g_boxFill),
        (int16_t)MENU_ART_Z_PANEL);
}

extern "C" void menu_art_begin(int exclusive)
{
    g_exclusive = exclusive;

    if (exclusive)
    {
        SRL::VDP2::NBG0::ScrollDisable();
    }
}

extern "C" void menu_art_draw(const MenuItem *items, int count)
{
    int i;

    if (!g_loaded)
    {
        return;
    }

    SRL::CRAM::Palette ramps[MENU_ART_BANK_COUNT] = {
        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bank[0]),
        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bank[1]),
        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bank[2]),
        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bank[3])
    };

    if (g_exclusive)
    {
        menu_art_sprite(boot_art_title_texture(), 0, 0, MENU_ART_SCREEN_W,
                        MENU_ART_SCREEN_H, MENU_ART_Z_BACK, 0);
    }

    for (i = 0; i < count; i++)
    {
        if (items[i].kind == MENU_ITEM_RECT)
        {
            menu_art_rect(items[i].id, items[i].x, items[i].y, items[i].w,
                          items[i].h);
        }
        else if (items[i].kind == MENU_ITEM_TITLE_ROW)
        {
            menu_art_sprite(boot_art_title_row_texture(items[i].id,
                                items[i].ramp == MENU_RAMP_TITLE_SEL),
                            items[i].x, items[i].y, items[i].w, items[i].h,
                            MENU_ART_Z_GLYPH, 0);
        }
        else if (items[i].id >= 0x20u && items[i].id <= 0x5Fu)
        {
            int ramp = (items[i].ramp < MENU_ART_BANK_COUNT)
                     ? (int)items[i].ramp : MENU_RAMP_DIM;

            menu_art_sprite(g_glyph[items[i].id - 0x20u], items[i].x,
                            items[i].y, MENU_ART_GLYPH_W, MENU_ART_GLYPH_H,
                            MENU_ART_Z_GLYPH, &ramps[ramp]);
        }
    }
}

extern "C" void menu_art_fade(int level)
{
    SRL::Types::HighColor dimmed[MENU_ART_PAL_ENTRIES];
    int b;

    if (level > FADECALC_LEVEL_NORMAL)
    {
        level = FADECALC_LEVEL_NORMAL;
    }
    if (level < 0)
    {
        level = 0;
    }

    boot_art_fade(level);

    if (level == g_artLevel)
    {
        return;
    }

    g_artLevel = level;

    for (b = 0; b < MENU_ART_BANK_COUNT; b++)
    {
        int i;

        if (g_bank[b] < 0)
        {
            continue;
        }

        for (i = 0; i < MENU_ART_PAL_ENTRIES; i++)
        {
            SRL::Types::HighColor c = g_pal[b][i];

            c.Red = (uint16_t)fadecalc_scale((int)c.Red, level);
            c.Green = (uint16_t)fadecalc_scale((int)c.Green, level);
            c.Blue = (uint16_t)fadecalc_scale((int)c.Blue, level);
            dimmed[i] = c;
        }

        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bank[b])
            .Load(dimmed, (int16_t)MENU_ART_PAL_ENTRIES);
    }
}

extern "C" void menu_art_present(void)
{
    SRL::Core::Synchronize();
    saturn_reset_poll();
}

extern "C" void menu_art_end(void)
{
    if (g_exclusive)
    {
        SRL::VDP2::NBG0::ScrollEnable();
        g_exclusive = 0;
    }
}

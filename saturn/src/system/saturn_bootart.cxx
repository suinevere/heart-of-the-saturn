#include <srl.hpp>
#include <stdio.h>
#include "saturn_bootart.h"
#include "saturn_reset.h"
#include "disc.h"
#include "saturn_compat.h"
#include "fadecalc.h"

using namespace SRL::Math::Types;

#define BOOT_ART_Z 500

#define BOOT_ART_OOTW_DIM 0
#define BOOT_ART_OOTW_LIT 1
#define BOOT_ART_HOTA_DIM 2
#define BOOT_ART_HOTA_LIT 3
#define BOOT_ART_TITLE    4
#define BOOT_ART_ROW0_DIM 5
#define BOOT_ART_COUNT    11

static const char *BOOT_ART_FILES[BOOT_ART_COUNT] =
{
    "SELOOTW0.ART",
    "SELOOTW1.ART",
    "SELHOTA0.ART",
    "SELHOTA1.ART",
    "HOTATITL.ART",
    "TROW0D.ART",
    "TROW0L.ART",
    "TROW1D.ART",
    "TROW1L.ART",
    "TROW2D.ART",
    "TROW2L.ART"
};

#define BOOT_ART_MAGIC 0x4241

#define BOOT_ART_STAGE_BYTES 36864

#define BOOT_ART_OOTW_X (-80)
#define BOOT_ART_HOTA_X 80

static int32_t g_texture[BOOT_ART_COUNT] =
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

static bool g_loaded = false;

static int32_t g_bank[BOOT_ART_COUNT] =
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static SRL::Types::HighColor g_pal[BOOT_ART_COUNT][16];
static int16_t g_palCount[BOOT_ART_COUNT];

static int g_artLevel = FADECALC_LEVEL_NORMAL;

static void bootArtSprite(int index, int16_t x, int16_t y, int16_t z)
{
    if (g_texture[index] < 0)
    {
        return;
    }

    SRL::Scene2D::DrawSprite((uint16_t)g_texture[index], Vector3D(x, y, z));
}

extern "C" int boot_art_load(void)
{
    uint8_t *stage;
    int i;

    if (g_loaded)
    {
        SRL::VDP2::NBG0::ScrollDisable();
        return 1;
    }

    stage = (uint8_t *)saturn_lwram_alloc(BOOT_ART_STAGE_BYTES);

    if (stage == nullptr)
    {
        printf("boot_art_load: no LWRAM for the staging buffer\n");
        return 0;
    }

    for (i = 0; i < BOOT_ART_COUNT; i++)
    {
        uint16_t width;
        uint16_t height;
        uint16_t count;
        int32_t bank;

        if (g_texture[i] >= 0)
        {
            continue;
        }

        if (disc_read_file(BOOT_ART_FILES[i], stage, BOOT_ART_STAGE_BYTES) < 0)
        {
            printf("boot_art_load: %s is not on the disc\n", BOOT_ART_FILES[i]);
            saturn_lwram_free(stage);
            return 0;
        }

        if (*(uint16_t *)stage != BOOT_ART_MAGIC)
        {
            printf("boot_art_load: %s has the wrong magic\n", BOOT_ART_FILES[i]);
            saturn_lwram_free(stage);
            return 0;
        }

        width = *(uint16_t *)(stage + 2);
        height = *(uint16_t *)(stage + 4);
        count = *(uint16_t *)(stage + 6);

        if (count < 1 || count > 16)
        {
            printf("boot_art_load: %s has %u palette entries\n", BOOT_ART_FILES[i],
                   (unsigned)count);
            saturn_lwram_free(stage);
            return 0;
        }

        bank = SRL::CRAM::GetFreeBank(SRL::CRAM::TextureColorMode::Paletted16);

        if (bank < 0)
        {
            printf("boot_art_load: %s got no CRAM bank\n", BOOT_ART_FILES[i]);
            saturn_lwram_free(stage);
            return 0;
        }

        SRL::CRAM::SetBankUsedState((uint16_t)bank, SRL::CRAM::TextureColorMode::Paletted16, true);

        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16, (uint16_t)bank)
            .Load((SRL::Types::HighColor *)(stage + 8), (int16_t)count);

        g_texture[i] = SRL::VDP1::TryLoadTexture(width, height,
            SRL::CRAM::TextureColorMode::Paletted16, (uint16_t)bank,
            stage + 8 + count * 2);

        if (g_texture[i] < 0)
        {
            printf("boot_art_load: %s got no VDP1 texture\n", BOOT_ART_FILES[i]);
            SRL::CRAM::SetBankUsedState((uint16_t)bank, SRL::CRAM::TextureColorMode::Paletted16, false);
            saturn_lwram_free(stage);
            return 0;
        }

        {
            int16_t e;

            g_bank[i] = bank;
            g_palCount[i] = (int16_t)count;

            for (e = 0; e < (int16_t)count && e < 16; e++)
            {
                g_pal[i][e] = ((SRL::Types::HighColor *)(stage + 8))[e];
            }
        }
    }

    saturn_lwram_free(stage);
    g_loaded = true;

    g_artLevel = FADECALC_LEVEL_NORMAL;

    SRL::VDP2::NBG0::ScrollDisable();
    return 1;
}

extern "C" void boot_art_draw(int highlight)
{
    if (!g_loaded)
    {
        return;
    }

    bootArtSprite(BOOT_ART_OOTW_DIM + (highlight == 0), BOOT_ART_OOTW_X, 0,
                  BOOT_ART_Z);
    bootArtSprite(BOOT_ART_HOTA_DIM + (highlight != 0), BOOT_ART_HOTA_X, 0,
                  BOOT_ART_Z);
}

extern "C" void boot_art_present(void)
{
    SRL::Core::Synchronize();

    saturn_reset_poll();
}

extern "C" void boot_art_release(void)
{
    SRL::VDP2::NBG0::ScrollEnable();
}

extern "C" int boot_art_title_texture(void)
{
    return (int)g_texture[BOOT_ART_TITLE];
}

extern "C" int boot_art_title_row_texture(int row, int lit)
{
    int index = BOOT_ART_ROW0_DIM + row * 2 + (lit != 0);

    if (row < 0 || index >= BOOT_ART_COUNT)
    {
        return -1;
    }

    return (int)g_texture[index];
}

extern "C" void boot_art_fade(int level)
{
    SRL::Types::HighColor dimmed[16];
    int i;

    if (level > FADECALC_LEVEL_NORMAL)
    {
        level = FADECALC_LEVEL_NORMAL;
    }
    if (level < 0)
    {
        level = 0;
    }

    if (level == g_artLevel)
    {
        return;
    }

    g_artLevel = level;

    for (i = 0; i < BOOT_ART_COUNT; i++)
    {
        int16_t e;

        if (g_bank[i] < 0)
        {
            continue;
        }

        for (e = 0; e < g_palCount[i] && e < 16; e++)
        {
            SRL::Types::HighColor c = g_pal[i][e];

            c.Red = (uint16_t)fadecalc_scale((int)c.Red, level);
            c.Green = (uint16_t)fadecalc_scale((int)c.Green, level);
            c.Blue = (uint16_t)fadecalc_scale((int)c.Blue, level);
            dimmed[e] = c;
        }

        SRL::CRAM::Palette(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bank[i])
            .Load(dimmed, g_palCount[i]);
    }
}

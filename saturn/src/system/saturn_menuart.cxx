/*----------------------
 | saturn_menuart.cxx
 | Description: The SRL side of the menu layer: four pre-packed .ART files
 |   loaded into VDP1 textures, a MenuItem list drawn as sprites, and the NBG0
 |   hand-off around it.
 |
 |   The whole design turns on VDP1 sprites drawing in front of NBG0. The
 |   engine rasterizes in software into an NBG0 bitmap and never draws a
 |   sprite, so the pause menu can composite over the game's last presented
 |   frame without copying it, freezing it, or touching any engine state --
 |   there is no freeze buffer here because none is needed. The sub-title menu
 |   and the gate's load screen want the screen to themselves instead, so they
 |   ask for exclusive mode, which switches NBG0 off and draws the boot art's
 |   title card as the backdrop.
 |
 |   The title card is borrowed from saturn_bootart.cxx rather than loaded
 |   again: VDP1::TryAllocateTexture is a bump allocator with no free, so a
 |   second copy would cost 35 KB of sprite VRAM permanently. That file owns the
 |   texture and its CRAM bank; this one only says where to draw it.
 |
 |   The .ART format and the read path are the boot art's, for the reason
 |   recorded there: SRL::Bitmap::TGA sized a heap allocation off the file and
 |   handed GFS_Load the resulting NULL, which hung the console. Files come in
 |   through disc_read_file into one LWRAM staging buffer and go straight to
 |   VDP1, so nothing here decodes anything.
 | Author: suinevere
 | Dependencies: saturn_menuart.h, menu_layout.h, disc.h, saturn_bootart.h,
 |   saturn_compat.h, fadecalc.h, SRL (VDP1, VDP2, CRAM, Scene2D, Core)
 | Globals: g_glyph, g_panel, g_panelW, g_panelH, g_bank, g_pal, g_artLevel,
 |   g_loaded, g_exclusive
 ----------------------*/
#include <srl.hpp>
#include <stdio.h>
#include "saturn_menuart.h"
#include "disc.h"
#include "saturn_bootart.h"
#include "saturn_compat.h"
#include "fadecalc.h"

using namespace SRL::Math::Types;

/*----------------------
 | MENU_ART_Z_BACK / MENU_ART_Z_PANEL / MENU_ART_Z_GLYPH
 | Description: Sprite sort values. slSetSprite orders far to near, so the
 |   smaller value draws on top: the backdrop is furthest, the panel sits over
 |   it and the glyphs over that. The absolute values are arbitrary; only their
 |   order matters, because these and the boot art's are the only sprites this
 |   port draws.
 |
 |   This ordering is unverified until the emulator run, the same way
 |   BOOT_ART_Z_BACK's was. If glyphs disappear behind the panel, swap
 |   MENU_ART_Z_PANEL and MENU_ART_Z_GLYPH.
 |
 |   Integers, not floats, for the reason given on menu_art_sprite: every
 |   coordinate in this file reaches Fxp through a function parameter.
 | Author: suinevere
 ----------------------*/
#define MENU_ART_Z_BACK   480
#define MENU_ART_Z_PANEL  470
#define MENU_ART_Z_GLYPH  460

/*----------------------
 | MENU_ART_STAGE_BYTES
 | Description: Size of the one LWRAM staging buffer disc_read_file fills and
 |   VDP1::TryLoadTexture reads from. disc_read_file_body's only size check is
 |   `file.Size.Bytes > max_size`, so this need only hold the largest file's
 |   real byte count -- the partial-final-sector bounce buffer is a separate
 |   allocation internal to disc_srl.cxx. 23,040 clears MENUPANS.ART, the
 |   largest of the four at 22,888 bytes (40-byte header and palette, then
 |   272x168 4bpp pixels).
 | Author: suinevere
 ----------------------*/
#define MENU_ART_STAGE_BYTES 23040

/*----------------------
 | MENU_ART_MAGIC
 | Description: The .ART header's first two bytes, "BA". Checked before any
 |   other field is read, because everything downstream -- the palette at byte
 |   8, the dimensions, MENU_ART_PIXELS_AT -- is an offset into a layout this
 |   is the only evidence for. A file that is not an .ART would otherwise be
 |   handed to VDP1 as pixels.
 | Author: suinevere
 ----------------------*/
#define MENU_ART_MAGIC 0x4241

/*----------------------
 | MENU_ART_PAL_ENTRIES / MENU_ART_PIXELS_AT
 | Description: Every file mkmenuart.py writes carries exactly 16 palette
 |   entries, so pixel data always begins at 8 + 2 * 16 = 40. The count is
 |   checked against each file's own header before this offset is trusted.
 | Author: suinevere
 ----------------------*/
#define MENU_ART_PAL_ENTRIES 16
#define MENU_ART_PIXELS_AT   (8 + 2 * MENU_ART_PAL_ENTRIES)

/*----------------------
 | MENU_ART_GLYPH_W .. MENU_ART_GLYPH_COUNT
 | Description: MENUFONT.ART is one 8x640 column of 64 stacked 8x10 cells, so a
 |   glyph is a fixed 40-byte stride into it and needs no lookup table. 64
 |   cells covers ASCII 0x20 through 0x5F, which is every character
 |   menu_layout.h emits.
 |
 |   Ten rows rather than eight because the glyphs are outlined: the 8x8 source
 |   bitmap sits one pixel in from the cell's top-left so its halo has somewhere
 |   to go, and the halo below the last ink row needs the tenth. Width stays 8
 |   because VDP1 counts sprite width in 8-dot units and the halo fits across.
 |   These must agree with mkmenuart.py's GLYPH_W and GLYPH_H; menu_art_load
 |   checks the file's header against them and refuses the load if they part.
 | Author: suinevere
 ----------------------*/
#define MENU_ART_GLYPH_W     8
#define MENU_ART_GLYPH_H     10
#define MENU_ART_GLYPH_BYTES ((MENU_ART_GLYPH_W * MENU_ART_GLYPH_H) / 2)
#define MENU_ART_GLYPH_COUNT 64

/*----------------------
 | MENU_ART_IDX_FILL .. MENU_ART_IDX_TITLE_SEL
 | Description: Palette indices in the shared 16-entry palette mkmenuart.py
 |   writes. Only two of them are ever a glyph pixel: 1 is the fill and 5 is the
 |   outline. The rest are colours menu_art_bank substitutes into those two when
 |   it builds a bank, which is how one set of glyph textures draws in four
 |   styles -- a second set would be 64 more VDP1 slots out of 128.
 |
 |   The four styles are two ramps, not one. Every screen with a panel keeps the
 |   blue ramp the menus shipped with, and takes its outline from the panel's own
 |   fill so the halo lands invisibly on the panel behind the text. The sub-title
 |   card is the one screen whose text sits on artwork instead, and it takes the
 |   game-select menu's white outline over red.
 |
 |   Index 0 is transparent and must stay that way. The slot list's widest row
 |   is 29 characters from x=60, and outlining the font spent two of the three
 |   blank columns that used to keep its last cell inside the panel border --
 |   which is why that row starts four pixels further left than it did.
 | Author: suinevere
 ----------------------*/
#define MENU_ART_IDX_FILL       1
#define MENU_ART_IDX_PANEL_FILL 2
#define MENU_ART_IDX_SEL        4
#define MENU_ART_IDX_OUTLINE    5
#define MENU_ART_IDX_TITLE_DIM  6
#define MENU_ART_IDX_TITLE_SEL  7

/*----------------------
 | MENU_ART_PANEL_COUNT
 | Description: How many panel files there are, matching MENU_PANEL_PAUSE,
 |   MENU_PANEL_SLOTS and MENU_PANEL_CONFIRM in menu_layout.h.
 | Author: suinevere
 ----------------------*/
#define MENU_ART_PANEL_COUNT 3

/*----------------------
 | MENU_ART_SCREEN_W / MENU_ART_SCREEN_H
 | Description: The display, which menu_layout.h lays out against and which is
 |   wider and taller than the 304x192 the engine renders into. Sprites are
 |   placed from the top-left in this frame; menu_art_sprite converts.
 | Author: suinevere
 ----------------------*/
#define MENU_ART_SCREEN_W 320
#define MENU_ART_SCREEN_H 224

/*----------------------
 | MENU_ART_PANEL_FILES
 | Description: The three panel files on the disc, 8.3 and upper case, in
 |   MENU_PANEL_* order.
 | Author: suinevere
 ----------------------*/
static const char *MENU_ART_PANEL_FILES[MENU_ART_PANEL_COUNT] =
{
    "MENUPANP.ART",
    "MENUPANS.ART",
    "MENUPANC.ART"
};

/*----------------------
 | g_glyph / g_panel / g_panelW / g_panelH
 | Description: The VDP1 textures and the panels' measured sizes.
 |
 |   Allocated once and never released, for the reason boot_art_load records:
 |   VDP1::TryAllocateTexture is a bump allocator with no free, so a menu that
 |   reloaded on every open would run the attract loop dry in a few passes.
 |   menu_art_load returns early once they are resident.
 |
 |   Spelled out as -1 rather than left to zero-initialisation, because 0 is a
 |   valid VDP1 texture id: menu_art_load's resume guard tests these for >= 0,
 |   and a zeroed table would read as 67 textures that are already loaded.
 |
 |   The title card's palette is deliberately not held here. saturn_bootart.cxx
 |   owns that texture and its bank, so it owns the flash too.
 | Author: suinevere
 ----------------------*/
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
static int32_t g_panel[MENU_ART_PANEL_COUNT] = { -1, -1, -1 };
static int16_t g_panelW[MENU_ART_PANEL_COUNT];
static int16_t g_panelH[MENU_ART_PANEL_COUNT];
/*----------------------
 | MENU_ART_BANK_COUNT / g_bank / g_pal
 | Description: One CRAM bank per ramp, indexed by MENU_RAMP_*, and the ramp
 |   each one was built with.
 |
 |   The ramps are kept rather than rebuilt on demand because the staging buffer
 |   they were derived from is freed at the end of menu_art_load, and a fade
 |   needs the undimmed originals back at every step -- scaling a bank that is
 |   already scaled compounds, and eight compounded steps reach black long
 |   before the eighth.
 | Author: suinevere
 ----------------------*/
#define MENU_ART_BANK_COUNT 4

static int32_t g_bank[MENU_ART_BANK_COUNT] = { -1, -1, -1, -1 };
static SRL::Types::HighColor g_pal[MENU_ART_BANK_COUNT][MENU_ART_PAL_ENTRIES];

/*----------------------
 | g_artLevel
 | Description: The fade level every bank currently holds, so a repeated call
 |   costs nothing.
 | Author: suinevere
 ----------------------*/
static int g_artLevel = FADECALC_LEVEL_NORMAL;

/*----------------------
 | g_loaded
 | Description: True once every texture is resident, so a second load is a
 |   no-op and a failed one cannot be drawn from.
 | Author: suinevere
 ----------------------*/
static int g_loaded;

/*----------------------
 | g_exclusive
 | Description: Whether the open menu owns the screen. Held rather than passed
 |   to each call so menu_art_end can undo exactly what menu_art_begin did,
 |   even from a caller that closes the menu on an error path.
 | Author: suinevere
 ----------------------*/
static int g_exclusive;

/*----------------------
 | menu_art_sprite
 | Description: Queues one texture with its top-left corner at (x, y) in the
 |   320x224 frame, converting to the centre-relative coordinate
 |   Scene2D::DrawSprite wants so no caller has to know where the origin is --
 |   menu_layout.h works entirely in top-left pixels.
 |
 |   Takes int16_t rather than a floating type because SRL's Fxp constructor
 |   for floating-point values is consteval, and a function parameter is never
 |   a constant expression -- a double here does not compile at all. Every
 |   coordinate this file passes is a whole number, so nothing is lost.
 |
 |   Silently does nothing for a texture that never loaded, so a caller that
 |   ignored menu_art_load's return draws nothing rather than reading a dead
 |   texture.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: texture -- a VDP1 texture id, or negative for none; x, y -- top-left
 |         corner in the display frame; w, h -- the texture's size; z -- sort
 |         value; ramp -- CRAM palette override, or null for the texture's own
 |
 |         Not named `pal`: sgl.h defines that as a macro for COL_32K, so any
 |         identifier called pal in a translation unit that includes <srl.hpp>
 |         expands to (2 - 1) and fails to compile.
 | Returns: N/A
 ----------------------*/
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

/*----------------------
 | menu_art_bank
 | Description: Claims a CRAM bank and fills it with one of the four ramps, all
 |   built from the same file's palette.
 |
 |   Two entries are substituted: the fill at index 1 and the outline at index 5,
 |   which are the only two indices a glyph pixel ever holds. mkmenuart.py writes
 |   the alternatives into entries no pixel uses precisely so the runtime does
 |   not have to invent a colour. Substituting rather than authoring four
 |   palettes is what makes a ramp a palette swap at draw time instead of a
 |   second set of glyph textures.
 |
 |   A self-copy is a legitimate argument -- the panel screens' dim ramp passes
 |   index 1 for its own fill -- and costs one assignment.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: file -- a loaded .ART image, palette at byte 8; fillIndex,
 |         outlineIndex -- which entries become the glyph fill and its outline;
 |         keep -- filled in with the ramp it built, which menu_art_fade needs
 |         undimmed at every step
 | Returns: the bank index, or -1 if no bank was free
 ----------------------*/
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

/*----------------------
 | menu_art_release_banks
 | Description: Gives both CRAM banks back and forgets them.
 |
 |   This exists because SRL does not unmark a bank when a later
 |   TryLoadTexture against it fails -- a pre-existing gap. Unlike a VDP1
 |   texture slot a bank can be released, so every failure path here does,
 |   otherwise a retry would burn a fresh bank each time the same file failed
 |   to reach VRAM. Idempotent, so a path that already released is safe.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_bank
 | Params: N/A
 | Returns: 0 always, so a failure path can spell itself
 |          `return menu_art_release_banks();`
 ----------------------*/
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

/*----------------------
 | menu_art_load
 | Description: Loads the font and the three panels into VDP1 textures and
 |   builds both text ramps.
 |
 |   Returns 1 immediately when already loaded, for the reason boot_art_load
 |   records: every menu open re-enters this, and VDP1's allocator cannot
 |   free.
 |
 |   Textures already resident are skipped for the same reason, so a retry
 |   after a partial failure continues rather than bump-allocating a second
 |   slot for each of the 67. That matters more here than in boot_art_load:
 |   a transient disc failure on the third panel would otherwise strand 65
 |   slots, and with SRL_MAX_TEXTURES at 128 the second retry would exhaust
 |   the table and the menu could never load again for the rest of the
 |   session.
 |
 |   A retry re-reads the font and takes fresh CRAM banks, which need not be
 |   the two the surviving textures were loaded against. That is harmless
 |   because every sprite this file draws passes an explicit Palette override
 |   -- see menu_art_draw and menu_art_panel -- so a texture's own baked
 |   palette id is never the one VDP1 uses.
 |
 |   The staging buffer is freed on every exit path including success. It is a
 |   real allocator, not a bump allocator, and holding 22 KB of LWRAM for the
 |   whole game would come out of the same pool savedata_probe's scratch draws
 |   from.
 |
 |   Panel width and height come out of each file's own header, never from a
 |   constant here, so a resize in mkmenuart.py cannot desynchronise the two.
 |   The palette count is checked for the same reason: MENU_ART_PIXELS_AT
 |   assumes 16 entries, and a file with any other count would be read from
 |   the wrong offset rather than refused.
 |
 |   The font cannot take its geometry from its header the way the panels do,
 |   because the glyph loop slices it on a fixed 8x8 stride, so it is checked
 |   against that stride instead -- magic, then 8 x 512 with 16 entries. It is
 |   the one file where a silent mismatch is unrecoverable: its palette is
 |   what both CRAM banks are built from, and every panel then draws with it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_glyph, g_panel, g_panelW, g_panelH, g_bank, g_pal, g_loaded
 | Params: N/A
 | Returns: 1 if every texture is resident, 0 if any file, header, bank or
 |          texture allocation failed
 ----------------------*/
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

    for (i = 0; i < MENU_ART_PANEL_COUNT; i++)
    {
        uint16_t width;
        uint16_t height;
        uint16_t count;

        if (g_panel[i] >= 0)
        {
            continue;
        }

        if (disc_read_file(MENU_ART_PANEL_FILES[i], stage,
                           MENU_ART_STAGE_BYTES) < 0)
        {
            printf("menu_art_load: %s did not read\n",
                   MENU_ART_PANEL_FILES[i]);
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }

        width = (uint16_t)((stage[2] << 8) | stage[3]);
        height = (uint16_t)((stage[4] << 8) | stage[5]);
        count = (uint16_t)((stage[6] << 8) | stage[7]);

        if (count != MENU_ART_PAL_ENTRIES)
        {
            printf("menu_art_load: %s has %u palette entries\n",
                   MENU_ART_PANEL_FILES[i], (unsigned)count);
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }

        g_panelW[i] = (int16_t)width;
        g_panelH[i] = (int16_t)height;
        g_panel[i] = SRL::VDP1::TryLoadTexture(width, height,
            SRL::CRAM::TextureColorMode::Paletted16, (uint16_t)g_bank[MENU_RAMP_DIM],
            stage + MENU_ART_PIXELS_AT);

        if (g_panel[i] < 0)
        {
            printf("menu_art_load: %s got no VDP1 texture\n",
                   MENU_ART_PANEL_FILES[i]);
            menu_art_release_banks();
            saturn_lwram_free(stage);
            return 0;
        }
    }

    saturn_lwram_free(stage);
    g_loaded = 1;
    return 1;
}

/*----------------------
 | menu_art_panel
 | Description: Queues one backdrop panel at its measured size. Always the dim
 |   ramp: the panel art carries its own fill and border colours, and a
 |   selected row is marked by its glyphs, not by the panel behind them.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_panel, g_panelW, g_panelH, g_bank
 | Params: id -- a MENU_PANEL_* value, already range-checked; x, y -- top-left
 |         corner in the display frame
 | Returns: N/A
 ----------------------*/
static void menu_art_panel(int id, int16_t x, int16_t y)
{
    SRL::CRAM::Palette dim(SRL::CRAM::TextureColorMode::Paletted16,
                           (uint16_t)g_bank[MENU_RAMP_DIM]);

    menu_art_sprite(g_panel[id], x, y, g_panelW[id], g_panelH[id],
                    MENU_ART_Z_PANEL, &dim);
}

/*----------------------
 | menu_art_begin
 | Description: Opens a menu in one of the two modes.
 |
 |   Exclusive switches NBG0 off, which is what the sub-title menu and the
 |   gate's load screen want -- there is nothing behind them worth showing and
 |   a VDP2 layer can sit in front of a sprite depending on priority. Overlay
 |   leaves NBG0 exactly as the engine left it, so the pause menu composites
 |   over the game's last presented frame at no cost.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_exclusive
 | Params: exclusive -- non-zero to take the screen, zero to overlay
 | Returns: N/A
 ----------------------*/
extern "C" void menu_art_begin(int exclusive)
{
    g_exclusive = exclusive;

    if (exclusive)
    {
        SRL::VDP2::NBG0::ScrollDisable();
    }
}

/*----------------------
 | menu_art_draw
 | Description: Queues this frame's sprites from a menu_layout_build list.
 |
 |   The backdrop is exclusive-mode only, and that is the whole point of the
 |   mode. In overlay mode NBG0 is still showing the game's last presented
 |   frame, and painting a backdrop over it would be exactly the freeze buffer
 |   this design exists to avoid.
 |
 |   The Palette locals are built per call rather than held as statics because
 |   Palette is two members and a constructor, this codebase has already lost a
 |   day to a static-initialiser hypothesis, and constructing them here costs
 |   nothing and cannot be ordered wrongly. They are built after the g_loaded
 |   guard, not before it: with nothing loaded every bank index is -1, and the
 |   constructor would take that as bank 0xFFFF and compute a pointer megabytes
 |   past CRAM. Nothing dereferences it, but there is no reason to form it.
 |
 |   An item's ramp indexes the array directly, and is range-checked because it
 |   arrives from a pure module this file cannot see the internals of -- an
 |   out-of-range value would form a Palette on whatever int followed the array.
 |
 |   Silently does nothing when no art is resident, so a caller that ignored
 |   menu_art_load's return draws nothing rather than reading dead textures.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_loaded, g_exclusive, g_glyph, g_bank
 | Params: items -- the draw list; count -- how many entries it holds
 | Returns: N/A
 ----------------------*/
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
        if (items[i].kind == MENU_ITEM_PANEL)
        {
            if (items[i].id < MENU_ART_PANEL_COUNT)
            {
                menu_art_panel(items[i].id, items[i].x, items[i].y);
            }
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

/*----------------------
 | menu_art_fade
 | Description: See saturn_menuart.h.
 |
 |   Scales through fadecalc_scale at 5 bits, which is CRAM's own depth, so this
 |   layer, the boot art and the engine's bitmap all dim by the same ratio at
 |   the same step of the ladder.
 |
 |   Every bank is written from the ramp menu_art_bank built it with rather than
 |   from whatever it currently holds, because scaling an already-scaled palette
 |   compounds and eight compounded steps reach black several steps early.
 |
 |   Each entry keeps its opacity bit, which is why this scales a copy rather
 |   than rebuilding through FromRGB555. Entry 0 is the one that matters -- see
 |   boot_art_fade's banner.
 |
 |   Has no g_loaded guard and needs none: with nothing loaded every bank index
 |   is -1 and the loop skips them, so this is safe to call from a path that
 |   gave up on menu_art_load.
 |
 |   boot_art_fade is called ahead of this function's own no-change return
 |   rather than after it, so the two levels cannot drift apart -- they are
 |   separately remembered, and a call that changed only one of them would leave
 |   the title card and the text disagreeing about how dark the menu is.
 | Author: suinevere
 | Dependencies: fadecalc.h, saturn_bootart.h
 | Globals: g_bank, g_pal, g_artLevel
 | Params: level -- 0 (black) to FADECALC_LEVEL_NORMAL
 | Returns: N/A
 ----------------------*/
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

/*----------------------
 | menu_art_present
 | Description: Puts the queued sprites on screen and waits one vblank, which
 |   is what paces a menu loop -- nothing else in the menu path blocks.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void menu_art_present(void)
{
    SRL::Core::Synchronize();
}

/*----------------------
 | menu_art_end
 | Description: Closes a menu and undoes exactly what menu_art_begin did.
 |
 |   The caller must present one more empty frame after this, or the last menu
 |   frame's sprites stay composited over the game -- nothing else in this
 |   port draws sprites, so nothing would otherwise replace them.
 |
 |   Idempotent, and a no-op after an overlay menu, so it is safe on any exit
 |   path.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_exclusive
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void menu_art_end(void)
{
    if (g_exclusive)
    {
        SRL::VDP2::NBG0::ScrollEnable();
        g_exclusive = 0;
    }
}

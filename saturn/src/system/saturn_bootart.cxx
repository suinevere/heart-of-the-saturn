/*----------------------
 | saturn_bootart.cxx
 | Description: The SRL side of the boot sequence's artwork: seven pre-packed
 |   .ART files loaded into VDP1 textures, drawn as sprites, and the NBG0
 |   hand-off around them.
 |
 |   This and saturn_movie are the only things in the port that use VDP1. The
 |   engine rasterizes in software to an NBG0 bitmap (see video_srl.cxx) and
 |   never draws a sprite, so the two never contend -- but a VDP2 layer can sit
 |   in front of a sprite depending on priority, so NBG0 is switched off for
 |   the duration rather than trusting an ordering nobody has verified on
 |   hardware.
 |
 |   SRL::Bitmap::TGA cannot be used here: LoadData allocates the whole file
 |   against the 16 KB HWRAM heap before decoding it, which is smaller than
 |   any of these six files, and the resulting NULL reaches GFS_Load unchecked.
 |   That hung the console on real hardware. tools/mkbootart.py now writes a
 |   pre-packed .ART format instead -- magic, dimensions, a HighColor palette,
 |   4bpp pixels -- read whole through disc_read_file and handed to VDP1
 |   directly, so no decoder and no TGA-sized allocation exist on this path.
 | Author: suinevere
 | Dependencies: saturn_bootart.h, disc.h, saturn_compat.h, SRL (VDP1, VDP2,
 |   CRAM, Scene2D, Core)
 | Globals: g_texture, g_loaded
 ----------------------*/
#include <srl.hpp>
#include <stdio.h>
#include "saturn_bootart.h"
#include "disc.h"
#include "saturn_compat.h"

using namespace SRL::Math::Types;

/*----------------------
 | BOOT_ART_Z_BACK / BOOT_ART_Z_FRONT
 | Description: Sprite sort values. slSetSprite orders far to near, so the
 |   smaller value draws on top -- the lit logo band must be BOOT_ART_Z_FRONT
 |   or the background hides it. The absolute values are arbitrary; only their
 |   order matters, because these are the only sprites drawn.
 |
 |   Integers, not floats, for the reason given on bootArtSprite: every
 |   coordinate in this file reaches Fxp through a function parameter, which
 |   cannot carry a fraction.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_Z_BACK  500
#define BOOT_ART_Z_FRONT 490

/*----------------------
 | BOOT_ART_LEGAL .. BOOT_ART_COUNT
 | Description: Indices into g_texture, matching BOOT_ART_FILES.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_LEGAL     0
#define BOOT_ART_VIRGIN    1
#define BOOT_ART_INTERPLAY 2
#define BOOT_ART_TITLE     3
#define BOOT_ART_MENUBG    4
#define BOOT_ART_OOTW      5
#define BOOT_ART_HOTA      6
#define BOOT_ART_COUNT     7

/*----------------------
 | BOOT_ART_FILES
 | Description: The six files on the disc, 8.3 and upper case, in g_texture
 |   order.
 | Author: suinevere
 ----------------------*/
static const char *BOOT_ART_FILES[BOOT_ART_COUNT] =
{
    "BOOTLEGL.ART",
    "BOOTVIRG.ART",
    "BOOTPLAY.ART",
    "BOOTTITL.ART",
    "MENUBG.ART",
    "MENUOOTW.ART",
    "MENUHOTA.ART"
};

/*----------------------
 | BOOT_ART_MAGIC
 | Description: The .ART header's first two bytes, big-endian "BA". Read as a
 |   native uint16_t with no byte-swap because the SH-2 is itself big-endian.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_MAGIC 0x4241

/*----------------------
 | BOOT_ART_STAGE_BYTES
 | Description: Size of the one work-RAM staging buffer disc_read_file fills
 |   and VDP1::TryLoadTexture reads from. disc_read_file_body's only size
 |   check is `file.Size.Bytes > max_size` (disc_srl.cxx), so the destination
 |   need only hold the file's real byte count -- the partial-final-sector
 |   bounce buffer is a separate LWRAM allocation internal to disc_srl.cxx
 |   and never writes through the caller's destination. 36,864 is the
 |   largest .ART file (BOOTTITL, 35,880 bytes: an 8-byte header, a 16-entry
 |   palette, 320x224 4bpp pixels) with headroom to spare.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_STAGE_BYTES 36864

/*----------------------
 | BOOT_ART_OOTW_X .. BOOT_ART_HOTA_Y
 | Description: Where the lit logo bands sit, as offsets from screen centre.
 |   Derived from tools/mkbootart.py's printed crop rectangles by
 |   offset_x = crop_x + width / 2 - 160 and offset_y = crop_y + height / 2 -
 |   112, against a 320x224 display. OOTW crops at x13 y45 280x34; HOTA at
 |   x5 y86 304x34. Regenerate these if the tool ever prints different
 |   rectangles -- it measures them rather than storing them.
 |
 |   Whole numbers, and they must stay whole. mkbootart.py pads crop widths to
 |   a multiple of eight and heights to a multiple of two precisely so these
 |   land on integers; a half-pixel offset would be truncated silently by
 |   bootArtSprite's int16_t parameters rather than refused. If a future band
 |   produces a fractional centre, fix the padding, do not write a fraction
 |   here.
 | Author: suinevere
 ----------------------*/
#define BOOT_ART_OOTW_X (-7)
#define BOOT_ART_OOTW_Y (-50)
#define BOOT_ART_HOTA_X (-3)
#define BOOT_ART_HOTA_Y (-9)

/*----------------------
 | g_texture
 | Description: The VDP1 textures, or -1 for one that never loaded.
 |
 |   Allocated on the first load and never released, because
 |   VDP1::TryAllocateTexture is a bump allocator with no free: every attract
 |   replay would take another 185 KB of the 512 KB sprite VRAM and the loop
 |   would run it dry in two passes. boot_art_load returns early once they are
 |   resident.
 | Author: suinevere
 ----------------------*/
static int32_t g_texture[BOOT_ART_COUNT] = { -1, -1, -1, -1, -1, -1, -1 };

/*----------------------
 | g_loaded
 | Description: True once every texture is resident, so a second load is a
 |   no-op and a failed one cannot be drawn from.
 | Author: suinevere
 ----------------------*/
static bool g_loaded = false;

/*----------------------
 | bootArtSprite
 | Description: Queues one texture at an offset from screen centre.
 |
 |   Takes int16_t rather than a floating type because SRL's Fxp constructor
 |   for floating-point values is consteval, and a function parameter is never
 |   a constant expression -- a double here does not compile at all. Every
 |   coordinate this file passes is a whole number, so nothing is lost, but
 |   note the consequence: a fractional offset would be truncated at the call
 |   site instead of rejected. See BOOT_ART_OOTW_X.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_texture
 | Params: index -- into g_texture; x, y -- offset from centre; z -- sort value
 | Returns: N/A
 ----------------------*/
static void bootArtSprite(int index, int16_t x, int16_t y, int16_t z)
{
    if (g_texture[index] < 0)
    {
        return;
    }

    SRL::Scene2D::DrawSprite((uint16_t)g_texture[index], Vector3D(x, y, z));
}

/*----------------------
 | boot_art_load
 | Description: Loads all six .ART files into VDP1 textures and hides NBG0.
 |
 |   Reads through disc_read_file rather than any SRL loader, into one LWRAM
 |   staging buffer rather than malloc, because the file this replaced --
 |   SRL::Bitmap::TGA over Cd::File::LoadBytes -- allocated the whole file
 |   against the 16 KB HWRAM heap and handed GFS_Load the resulting NULL
 |   unchecked, DMAing 36 sectors to address 0 and hanging the console. LWRAM
 |   has the room this needs (see saturn_compat.h), and disc_read_file reads
 |   whole sectors into a caller-owned destination with its own bounce buffer
 |   for the partial final sector, so nothing here decodes a file or sizes a
 |   heap allocation off it.
 |
 |   The buffer is one allocation reused across all six files and freed
 |   before every return, including failure -- there is no second exit path
 |   that could leak it.
 |
 |   Textures already loaded are skipped, so a retry after a partial failure
 |   continues rather than bump-allocating a second slot for every screen that
 |   already had one. VDP1's allocator cannot free, so a retry that restarted
 |   from zero would strand the first attempt's slots permanently.
 |
 |   A CRAM bank, unlike a VDP1 texture slot, can be released, so the
 |   TryLoadTexture failure path does exactly that -- otherwise a retry would
 |   burn a fresh bank every time the same screen failed to reach VRAM.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_texture, g_loaded
 | Params: N/A
 | Returns: 1 if every texture is resident, 0 if any file, header or
 |          allocation failed
 ----------------------*/
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
    }

    saturn_lwram_free(stage);
    g_loaded = true;
    SRL::VDP2::NBG0::ScrollDisable();
    return 1;
}

/*----------------------
 | boot_art_draw
 | Description: Queues this frame's sprites. One for an opening still, two for
 |   the menu: the both-dim background, then the lit logo band nearer the
 |   viewer. Silently does nothing when no art is resident, so a caller that
 |   ignored boot_art_load's return draws nothing rather than reading a dead
 |   texture.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_loaded
 | Params: screen -- a boot_screen value; highlight -- a boot_entry value, read
 |         only for the menu
 | Returns: N/A
 ----------------------*/
extern "C" void boot_art_draw(int screen, int highlight)
{
    if (!g_loaded)
    {
        return;
    }

    switch (screen)
    {
    case 0:
        bootArtSprite(BOOT_ART_LEGAL, 0, 0, BOOT_ART_Z_BACK);
        break;

    case 1:
        bootArtSprite(BOOT_ART_VIRGIN, 0, 0, BOOT_ART_Z_BACK);
        break;

    case 2:
        bootArtSprite(BOOT_ART_INTERPLAY, 0, 0, BOOT_ART_Z_BACK);
        break;

    case 3:
        bootArtSprite(BOOT_ART_TITLE, 0, 0, BOOT_ART_Z_BACK);
        break;

    case 4:
        bootArtSprite(BOOT_ART_MENUBG, 0, 0, BOOT_ART_Z_BACK);

        if (highlight == 0)
        {
            bootArtSprite(BOOT_ART_OOTW, BOOT_ART_OOTW_X, BOOT_ART_OOTW_Y,
                          BOOT_ART_Z_FRONT);
        }
        else
        {
            bootArtSprite(BOOT_ART_HOTA, BOOT_ART_HOTA_X, BOOT_ART_HOTA_Y,
                          BOOT_ART_Z_FRONT);
        }
        break;

    default:
        break;
    }
}

/*----------------------
 | boot_art_present
 | Description: Puts the queued sprites on screen and waits one vblank, which
 |   is what paces the whole boot sequence -- bootmenu.c reads elapsed
 |   milliseconds and never counts frames, so this is the only thing keeping
 |   the loop from spinning.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void boot_art_present(void)
{
    SRL::Core::Synchronize();
}

/*----------------------
 | boot_art_release
 | Description: Shows NBG0 again so the engine's own bitmap layer is visible.
 |   The textures are deliberately kept -- see g_texture. Unconditional and
 |   idempotent, so it is safe after a load that failed before ever hiding
 |   NBG0.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
extern "C" void boot_art_release(void)
{
    SRL::VDP2::NBG0::ScrollEnable();
}

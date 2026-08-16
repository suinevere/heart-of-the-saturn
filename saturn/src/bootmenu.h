/*----------------------
 | bootmenu.h
 | Description: The boot sequence's state machine: which screen is showing,
 |   which menu entry is lit, what the CD-DA volume should be, and when the
 |   game takes over. Pure arithmetic over an elapsed millisecond count and an
 |   edge-triggered key mask.
 |
 |   Deliberately free of SRL, stdio and every engine header, for the same
 |   reason cdda_classify.h and discfmt.h are: compiled into the engine and by
 |   saturn/tests/run_tests.sh with the host gcc. Nothing here draws anything
 |   or touches the disc; saturn_bootart.h and disc.h do that, driven by what
 |   this returns.
 |
 |   Design: docs/superpowers/specs/2026-08-13-hota-saturn-boot-sequence-design.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef BOOTMENU_H
#define BOOTMENU_H

#include <stdint.h>

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link, the way the six seam headers did before 7f66fe3. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | boot_screen
 | Description: What should be on screen this frame. Four opening stills, one
 |   texture each, held BOOT_STILL_MS apiece, so the screen index stays
 |   phase_ms / BOOT_STILL_MS.
 | Author: suinevere
 ----------------------*/
typedef enum
{
    BOOT_SCREEN_LEGAL     = 0,
    BOOT_SCREEN_VIRGIN    = 1,
    BOOT_SCREEN_INTERPLAY = 2,
    BOOT_SCREEN_TITLE     = 3,
    BOOT_SCREEN_MENU      = 4
} boot_screen;

/*----------------------
 | boot_entry
 | Description: Which menu entry is lit. Either entry can be confirmed only
 |   when its own game is available -- the entry exists because the
 |   original's menu does, regardless of what this engine can currently run.
 | Author: suinevere
 ----------------------*/
typedef enum
{
    BOOT_ENTRY_OUT_OF_THIS_WORLD  = 0,
    BOOT_ENTRY_HEART_OF_THE_ALIEN = 1
} boot_entry;

/*----------------------
 | BOOT_KEY_*
 | Description: Bits in the pressed mask, one per key input.h exports. The
 |   caller passes edges, not levels, so a button held from a previous screen
 |   cannot skip twice or move the cursor every frame.
 | Author: suinevere
 ----------------------*/
#define BOOT_KEY_UP      0x01u
#define BOOT_KEY_DOWN    0x02u
#define BOOT_KEY_LEFT    0x04u
#define BOOT_KEY_RIGHT   0x08u
#define BOOT_KEY_A       0x10u
#define BOOT_KEY_B       0x20u
#define BOOT_KEY_C       0x40u
#define BOOT_KEY_SELECT  0x80u
#define BOOT_KEY_MOVE    (BOOT_KEY_UP | BOOT_KEY_DOWN)
#define BOOT_KEY_CONFIRM (BOOT_KEY_A | BOOT_KEY_B | BOOT_KEY_C)

/*----------------------
 | BOOT_STILL_MS / BOOT_OPENING_MS
 | Description: How long each opening still holds, and the four together. The
 |   capture measures 5100/5100/5117/5133 ms; the variation is frame
 |   quantisation at 60 fps rather than intent, so all four are 5100 here.
 | Author: suinevere
 ----------------------*/
#define BOOT_STILL_MS    5100u
#define BOOT_OPENING_MS  (BOOT_STILL_MS * 4u)

/*----------------------
 | BOOT_MENU_IDLE_MS / BOOT_FADE_MS / BOOT_MUSIC_CAP_MS
 | Description: How long the menu waits before replaying the opening, how long
 |   the music takes to fade before that replay, and the hard ceiling on how
 |   much of the track ever plays.
 |
 |   The cap is mostly emergent -- 20400 of stills plus 19000 of menu is 39400,
 |   so the fade lands just inside it -- and exists so a later timing change
 |   cannot let a 2:46 track run on into its second minute.
 | Author: suinevere
 ----------------------*/
#define BOOT_MENU_IDLE_MS  19000u
#define BOOT_FADE_MS        1000u
#define BOOT_MUSIC_CAP_MS  40000u

/*----------------------
 | BOOT_VOLUME_MAX
 | Description: Full CD-DA volume. SND_SetCdDaLev takes 0..7, so a fade has
 |   eight steps and is a staircase rather than a ramp; lengthening it adds no
 |   resolution, only time to notice each step.
 | Author: suinevere
 ----------------------*/
#define BOOT_VOLUME_MAX 7u

/*----------------------
 | BOOT_MUSIC_INDEX
 | Description: The engine music index the boot sequence plays.
 |   discfmt_cue_track_for_music maps it to cue track 3, which is track03.wav
 |   at 2:46.17 -- the disc numbers its 41 audio tracks 02..42, so this is the
 |   second audio track despite being disc track three. test_bootmenu.c
 |   asserts the mapping, because the wrong index is inaudible as a bug and
 |   merely sounds like different music.
 | Author: suinevere
 ----------------------*/
#define BOOT_MUSIC_INDEX 2

/*----------------------
 | bootmenu_state
 | Description: Everything the sequence remembers between frames. Opaque to
 |   callers by convention; only bootmenu.c reads the fields.
 | Author: suinevere
 ----------------------*/
typedef struct
{
    uint32_t phase_start_ms;
    uint32_t music_start_ms;
    uint32_t idle_start_ms;
    int      in_menu;
    int      highlight;
    int      music_started;
    int      part1_available;
    int      part2_available;
} bootmenu_state;

/*----------------------
 | boot_frame
 | Description: What the caller should do this frame. start_game and
 |   start_part1 are mutually exclusive: one confirm can only choose one game.
 | Author: suinevere
 ----------------------*/
typedef struct
{
    boot_screen screen;
    boot_entry  highlight;
    uint8_t     music_volume;
    int         music_restart;
    int         start_game;
    int         start_part1;
} boot_frame;

/*----------------------
 | bootmenu_init
 | Description: Starts the sequence at the first opening still, with the
 |   cursor on OUT OF THIS WORLD to match the capture's first menu frame.
 |
 |   An unavailable game's entry still lights and still moves the cursor; only
 |   confirming it is refused. The original's menu has no greyed-out state and
 |   the capture the art is cropped from cannot supply one.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise; now_ms -- the clock's current reading,
 |         which need not be zero; part1_available -- non-zero if Part I's
 |         program is on the disc; part2_available -- non-zero if Part II's
 |         blobs are
 | Returns: N/A
 ----------------------*/
void bootmenu_init(bootmenu_state *st, uint32_t now_ms,
                   int part1_available, int part2_available);

/*----------------------
 | bootmenu_step
 | Description: Advances the sequence one frame and reports what to draw and
 |   play. All arithmetic is on unsigned differences, so it is correct across
 |   the millisecond counter's wrap.
 |
 |   A frame carrying both a move and a confirm confirms the entry that was
 |   lit BEFORE the move. The pad is sampled once per frame, so pressing Down
 |   and A together arrives as one mask; resolving the confirm against the
 |   post-move highlight would let a single simultaneous press start a game the
 |   player never saw selected.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; now_ms -- clock reading; pressed -- edge-triggered
 |         BOOT_KEY_* mask; out -- filled in with this frame's decisions
 | Returns: N/A
 ----------------------*/
void bootmenu_step(bootmenu_state *st, uint32_t now_ms, uint32_t pressed,
                   boot_frame *out);

#ifdef __cplusplus
}
#endif

#endif /* BOOTMENU_H */

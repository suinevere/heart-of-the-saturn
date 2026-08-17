/*----------------------
 | menu.c
 | Description: The menu layer's driver: the only file in menus/ that knows the
 |   engine exists. It owns the frame loop the four pure modules are stepped
 |   from, the backup RAM probing they refuse to do themselves, and the two
 |   entry points main.c calls.
 |
 |   Never includes <srl.hpp>. Everything that needs VDP1, CRAM or the pad's
 |   Start button is reached through a C seam -- saturn_menuart.h and
 |   input_raw_buttons -- for the same reason saturn_bootart.h exists: the
 |   engine's headers wrap SGL's C headers in extern "C", and mixing that with
 |   SRL's C++ headers in one translation unit is fragile.
 | Author: suinevere
 | Dependencies: menu.h, menu_state.h, menu_layout.h, menu_clock.h,
 |   saturn_menuart.h, saturn_saveslot.h, saturn_backup.h, savedata.h,
 |   saturn_compat.h, disc.h, input.h, platform.h, client.h, main.h, screen.h,
 |   video.h, fadecalc.h, vm.h
 ----------------------*/

#include <string.h>
#include <stdio.h>

#include "menu.h"
#include "menu_state.h"
#include "menu_layout.h"
#include "menu_clock.h"
#include "saturn_menuart.h"
#include "saturn_saveslot.h"
#include "saturn_backup.h"
#include "savedata.h"
#include "saturn_compat.h"
#include "disc.h"
#include "input.h"
#include "platform.h"
#include "client.h"
#include "main.h"
#include "screen.h"
#include "video.h"
#include "fadecalc.h"
#include "vm.h"

/*----------------------
 | next_script / ending_played / death_played
 | Description: The three engine globals this file writes and reads.
 |
 |   Setting next_script to MENU_PASSWORD_ROOM is how Return to Title re-enters
 |   the gate: it is the one value run()'s loop routes through menu_gate, so
 |   assigning it is the whole of that command.
 |
 |   ending_played is decode.c's hint that the credits have just finished, and
 |   death_played animation.c's that a death has. Neither arrival can be
 |   inferred here: they reach this room by the same route and look identical
 |   from it, and a death does not change room at all -- decode.c only asks for
 |   this one because death_played told it to. All three are defined in main.c
 |   and declared here rather than reached through a new header, because one int
 |   each is not an interface.
 | Author: suinevere
 ----------------------*/
extern int next_script;
extern int ending_played;
extern int death_played;

/*----------------------
 | rest
 | Description: main.c's frame pacer, declared here rather than included
 |   because it is in no header -- animation.c:39 declares it the same way for
 |   the same reason, and adding it to main.h would mean deciding what main.h
 |   is for on a task that has no business deciding that.
 |
 |   Only rest(0) is called from this file. rest(12) paces the game by waiting
 |   until platform_ticks() has advanced past last_tick, then advancing
 |   last_tick by the frame's worth; a menu that blocks inside the frame for
 |   thirty seconds leaves last_tick thirty seconds stale, and the throttle
 |   then waits zero for the ~360 frames it takes to catch up -- several
 |   seconds of the VM running at vsync rate instead of 12 fps, which in this
 |   game can kill the player. rest(0) sets last_tick to now and clears the
 |   fractional carry, which is the baseline any long block owes on the way
 |   out. Same rule the fades and the animation player already obey
 |   (main.c:546, animation.c:1055).
 | Author: suinevere
 | Params: fps -- 0 to re-baseline, otherwise the frame rate to pace at
 | Returns: N/A
 ----------------------*/
void rest(int fps);

/*----------------------
 | MENU_GATE_SUBTITLE / MENU_GATE_LOAD
 | Description: Which screen the next arrival at the gate opens on. The
 |   sub-title menu belongs to a fresh start or a just-finished ending; every
 |   other arrival is a death or a Return to Title in a playthrough that has
 |   already begun, where a list of saves is what the player wants.
 | Author: suinevere
 ----------------------*/
#define MENU_GATE_SUBTITLE 0
#define MENU_GATE_LOAD     1

/*----------------------
 | MENU_FADE_HOLD_MS
 | Description: How long one step of a menu fade stays on screen. The same 60 ms
 |   main.c's FADE_HOLD_MS gives the engine's own fades, spelled again here
 |   rather than shared because that one is private to main.c and one integer is
 |   not worth a header. If either moves, move both: a menu that faded at a
 |   different rate from the cutscene either side of it would read as a stutter
 |   rather than a style.
 | Author: suinevere
 ----------------------*/
#define MENU_FADE_HOLD_MS 60

/*----------------------
 | s_gateMode
 | Description: Which of the two screens the next menu_gate call opens. Starts
 |   at MENU_GATE_SUBTITLE so the first arrival, straight off the intro, is the
 |   sub-title menu.
 | Author: suinevere
 ----------------------*/
static int s_gateMode = MENU_GATE_SUBTITLE;

/*----------------------
 | s_pausePrev
 | Description: Last frame's button mask as menu_pause_poll saw it, so Start
 |   held across frames opens the pause menu once. Re-primed from the live pad
 |   after the menu closes, because the player is very likely still holding the
 |   button that dismissed it.
 | Author: suinevere
 ----------------------*/
static int s_pausePrev;

/*----------------------
 | MENU_BIT_*
 | Description: One bit per button the menu reads, so a frame of input is a
 |   single int and an edge is one and-not. Private to this file; the state
 |   machine takes a MenuInput of separate flags.
 | Author: suinevere
 ----------------------*/
#define MENU_BIT_UP      0x01
#define MENU_BIT_DOWN    0x02
#define MENU_BIT_LEFT    0x04
#define MENU_BIT_RIGHT   0x08
#define MENU_BIT_CONFIRM 0x10
#define MENU_BIT_CANCEL  0x20
#define MENU_BIT_PAUSE   0x40

/*----------------------
 | menu_key_mask
 | Description: This frame's held buttons as a bit mask, read straight from
 |   port 0's raw state rather than the key globals check_events writes.
 |   Confirm and cancel are physical A and B, and stay physical A and B
 |   however the player remaps the game -- keymap_apply only ever touches
 |   key_a, key_b and key_c, so a menu that read those globals would move
 |   with the mapping and could hide the very screen that would undo it.
 | Author: suinevere
 | Dependencies: input.h
 | Globals: N/A
 | Params: N/A
 | Returns: the MENU_BIT_* mask of everything held
 ----------------------*/
static int menu_key_mask(void)
{
    unsigned int raw = input_raw_buttons();
    int mask = 0;

    if (raw & PAD_BIT_UP)    mask |= MENU_BIT_UP;
    if (raw & PAD_BIT_DOWN)  mask |= MENU_BIT_DOWN;
    if (raw & PAD_BIT_LEFT)  mask |= MENU_BIT_LEFT;
    if (raw & PAD_BIT_RIGHT) mask |= MENU_BIT_RIGHT;
    if (raw & PAD_BIT_A)     mask |= MENU_BIT_CONFIRM;
    if (raw & PAD_BIT_B)     mask |= MENU_BIT_CANCEL;
    if (raw & PAD_BIT_START) mask |= MENU_BIT_PAUSE;

    return mask;
}

/*----------------------
 | menu_edges
 | Description: Spreads a mask of newly pressed buttons into the MenuInput the
 |   state machine takes.
 | Author: suinevere
 | Dependencies: menu_state.h
 | Globals: N/A
 | Params: pressed -- the MENU_BIT_* mask of buttons that became held this
 |         frame; in -- filled in, every field cleared first
 | Returns: N/A
 ----------------------*/
static void menu_edges(int pressed, MenuInput *in)
{
    memset(in, 0, sizeof(*in));
    in->up      = (pressed & MENU_BIT_UP) != 0;
    in->down    = (pressed & MENU_BIT_DOWN) != 0;
    in->left    = (pressed & MENU_BIT_LEFT) != 0;
    in->right   = (pressed & MENU_BIT_RIGHT) != 0;
    in->confirm = (pressed & MENU_BIT_CONFIRM) != 0;
    in->cancel  = (pressed & MENU_BIT_CANCEL) != 0;
    in->pause   = (pressed & MENU_BIT_PAUSE) != 0;
}

/*----------------------
 | menu_slots_used
 | Description: How many of a device's slots hold something.
 |
 |   Exists rather than passing zeros because savedata_pick_default_device
 |   decides between a cart and internal memory on whether each holds saves --
 |   hand it two zeros and it returns internal unconditionally, turning a real
 |   decision into dead code. It costs three probes per device and runs at most
 |   twice, once per menu open.
 | Author: suinevere
 | Dependencies: savedata.h
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; scratch -- a
 |         SAVE_MAX_BYTES working buffer
 | Returns: the number of slots that probed as anything but empty
 ----------------------*/
static int menu_slots_used(unsigned long device, unsigned char *scratch)
{
    SlotInfo info;
    int i;
    int used = 0;

    for (i = 0; i < SAVE_NUM_SLOTS; i++)
    {
        if (savedata_probe(device, i, &info, scratch, SAVE_MAX_BYTES)
            != SLOT_EMPTY)
        {
            used++;
        }
    }
    return used;
}

/*----------------------
 | menu_rescan
 | Description: Re-probes both devices and refills st->slots, which is the work
 |   MENU_ACT_RESCAN_SLOTS asks for.
 |
 |   The default device is chosen only when st->device holds neither valid id.
 |   The entry points leave it zero, so the first rescan picks; every rescan
 |   after that respects what the player selected with left and right. Without
 |   that guard the device toggle would be undone by the very rescan it
 |   triggers.
 | Author: suinevere
 | Dependencies: saturn_backup.h, savedata.h, menu_state.h
 | Globals: N/A
 | Params: st -- state whose device, cartPresent and slots are refreshed;
 |         scratch -- a SAVE_MAX_BYTES working buffer
 | Returns: N/A
 ----------------------*/
static void menu_rescan(MenuState *st, unsigned char *scratch)
{
    SatBupDev internal;
    SatBupDev cart;
    int i;

    memset(&internal, 0, sizeof(internal));
    memset(&cart, 0, sizeof(cart));
    sat_bup_probe(SAT_BUP_INTERNAL, &internal);
    sat_bup_probe(SAT_BUP_CART, &cart);
    st->cartPresent = cart.present && cart.formatted;

    if (st->device != SAT_BUP_INTERNAL && st->device != SAT_BUP_CART)
    {
        st->device = savedata_pick_default_device(&internal, &cart,
            menu_slots_used(SAT_BUP_INTERNAL, scratch),
            st->cartPresent ? menu_slots_used(SAT_BUP_CART, scratch) : 0);
    }
    if (st->device == SAT_BUP_CART && !st->cartPresent)
    {
        st->device = SAT_BUP_INTERNAL;
    }

    for (i = 0; i < SAVE_NUM_SLOTS; i++)
    {
        savedata_probe(st->device, i, &st->slots[i], scratch, SAVE_MAX_BYTES);
    }
}

/*----------------------
 | menu_fade_out
 | Description: Walks the menu and the game layer down to black together,
 |   holding each step for MENU_FADE_HOLD_MS and redrawing the same frame the
 |   whole way.
 |
 |   Both layers, because in overlay mode both are on screen: the menu is VDP1
 |   sprites and whatever it is composited over is the VDP2 bitmap, and dimming
 |   only one of them would fade the load card off a death frame that stayed
 |   lit. In exclusive mode NBG0 is switched off and the video_set_fade call is
 |   invisible, but it is still the right write -- it leaves the level at black
 |   for the room that comes next, which is what screen_arm_fade_in expects to
 |   find.
 |
 |   The redraw inside the wait is not optional. VDP1's command list is rebuilt
 |   from scratch every present, so a frame that queues nothing shows nothing --
 |   skipping the draw would make the fade a flicker rather than a dim.
 |
 |   The CD-DA level walks down with the picture when the caller owns the music.
 |   SND_SetCdDaLev takes 0..7, so the eight fade steps are exactly the eight
 |   the volume has, and the track is silent on the same step the screen reaches
 |   black. The pause menu never touches the volume and so never asks for this.
 |
 |   rest(0) on the way out, for the reason this file's rest banner gives: half
 |   a second spent here would otherwise be sprinted through afterwards.
 | Author: suinevere
 | Dependencies: saturn_menuart.h, video.h, fadecalc.h, platform.h, disc.h
 | Globals: N/A
 | Params: items -- the draw list to keep showing; count -- how many entries it
 |         holds; music -- non-zero to take the CD-DA down with it
 | Returns: N/A
 ----------------------*/
static void menu_fade_out(const MenuItem *items, int count, int music)
{
    int step;

    for (step = 1; step <= FADECALC_SEGA_CD_STEPS; step++)
    {
        unsigned int start = platform_ticks();
        int level = fadecalc_step_level(step, FADECALC_SEGA_CD_STEPS);

        menu_art_fade(level);
        video_set_fade(level);

        if (music)
        {
            disc_set_music_volume((unsigned char)(MENU_VOLUME_MAX
                - (step * MENU_VOLUME_MAX) / FADECALC_SEGA_CD_STEPS));
        }

        do
        {
            menu_art_draw(items, count);
            menu_art_present();
        }
        while (platform_ticks() - start < MENU_FADE_HOLD_MS);
    }

    rest(0);
}

/*----------------------
 | menu_run
 | Description: The frame loop both entry points share: sample, step, act,
 |   draw, present, until an action ends the menu.
 |
 |   One frame is drawn and presented before the pad is ever sampled, and that
 |   ordering is the whole defence against a button held on entry reading as a
 |   press. check_events() only reads SRL's peripheral array; the thing that
 |   refreshes it is Core::Synchronize, reached solely through
 |   menu_art_present. Until that first refresh port 0 holds its static
 |   initialiser and reads as not-connected, so check_events zeroes every key
 |   and priming from it would capture a synthetic zero and report every held
 |   button as newly pressed on the next frame. This is boot_sequence's rule
 |   and it is load-bearing here for the same reason.
 |
 |   The final menu_art_present after menu_art_end submits an empty command
 |   list, so the last menu frame's sprites are not left composited over the
 |   game.
 |
 |   The exit hands three things back, and every one of them is something
 |   nothing else in the port would restore. rest(0) re-baselines the frame
 |   pacer -- see rest's banner above for why a blocking menu without it makes
 |   the game sprint. The CD-DA level goes back to MENU_VOLUME_MAX because
 |   menu_clock_step drives it to 0 on the first frame and staircases up over
 |   MENU_FADE_MS, and a player who confirms START GAME inside that first
 |   second would otherwise play the whole session at level 1-3, with the next
 |   arrival inheriting it. And the menu's own track is stopped so it does not
 |   play on over the first room. All three are guarded on useClock: the pause
 |   menu never touched the volume and must not stop the game's music.
 |
 |   disc_stop_track is skipped when the exit was a successful load, because
 |   saturn_saveslot_load restores the track the save recorded as part of the
 |   load. Stopping it here would undo that on every load from the sub-title
 |   screen -- the volume restore still applies, since the restored track
 |   should play at full level. boot_sequence has the same shape at
 |   main.c:1181 without the load case, which cannot arise there.
 |
 |   items is static rather than automatic: 200 MenuItem is 1,600 bytes, and
 |   this function sits on the stack under run() with the whole VM below it.
 |
 |   The attract block's order is fixed. play_intro needs NBG0 and the drive,
 |   so the overlay comes down and the music stops before it runs; the volume
 |   goes back to full before it starts, or the cinematic's own tracks would
 |   play at the menu's faded-out level; and previous is re-primed from the
 |   live pad afterwards, because the button that skipped the cinematic is very
 |   likely still held.
 |
 |   The clock is stepped on every frame useClock is set, but the attract is
 |   armed only while st->screen == MENU_TITLE. A player reading three slot
 |   rows is not idle, and tearing the screen down under them to play a
 |   cinematic would be a bug that looks like a feature -- but that argument is
 |   about the cinematic, not the music, and gating the whole clock on the
 |   screen froze the volume ramp on the slot and confirm screens and never ran
 |   it at all on a MENU_GATE_LOAD arrival, which opens straight onto
 |   MENU_SLOTS. Track 03 then started at whatever level the last exit left,
 |   never faded, never restarted at MENU_MUSIC_CYCLE_MS and stopped for good
 |   after one play. launch_attract is level-triggered, so an idle spent on a
 |   sub-screen simply holds it set and unread; the cancel that returns to
 |   MENU_TITLE is itself input and re-baselines the idle timer in the same
 |   frame, so the cinematic cannot ambush the player on arrival.
 |
 |   onNoMemory exists because the two callers want different fallbacks when
 |   the probe scratch cannot be allocated: the gate must still start a game,
 |   the pause poll must still resume.
 |
 |   `exclusive` is the mode the menu opens in, not the mode it keeps. The
 |   sub-title screen is exclusive by definition -- it is the title card with
 |   items over it -- so reaching MENU_TITLE promotes, and the promotion is one
 |   way. That is what lets the gate open the load list over a held death frame
 |   and still put the player on a proper title screen when they close the box:
 |   the overlay is a property of the arrival, the title card is a property of
 |   the screen. The game layer is blacked at the moment of promotion, because
 |   NBG0 goes dark under a card the player is about to see and must not come
 |   back holding the frame that was behind it.
 |
 |   `fade` is what separates the gate from the pause menu. The gate is a
 |   transition -- something faded out to reach it and something fades in after
 |   it -- so it comes up from black and goes back down to it. Pausing is not a
 |   transition; a resume that spent half a second fading would be a bug.
 |
 |   The fade in is a ramp pumped from the loop rather than a blocking walk, so
 |   it costs the player no time: it runs over the menu's own opening frames and
 |   the pad is live throughout. The fade out has to block, since there is
 |   nothing left to run it over.
 |
 |   The fade out takes the music with it everywhere except a successful load,
 |   for the same reason that exit skips disc_stop_track below: the load has
 |   already commanded the track the save recorded, and dimming it to nothing
 |   here only to have the volume restore at the bottom of this function put it
 |   straight back would be an audible dip over the room the player just asked
 |   for.
 |
 |   count is initialised because a state machine that acts on its very first
 |   step breaks out before menu_layout_build has ever run, and the fade out
 |   redraws whatever list was last built.
 |
 |   Nothing puts the palettes back on the way out, and nothing may. menu_art_end
 |   submits an empty command list but VDP1 does not display it until the next
 |   vblank, so the sprites the fade just dimmed are still on screen when this
 |   returns and a CRAM write lands on them immediately -- which is a full
 |   brightness flash of the menu one frame before it disappears. This function
 |   sets the level for itself before its first frame instead, on both branches,
 |   so a menu never inherits one.
 | Author: suinevere
 | Dependencies: menu_state.h, menu_layout.h, menu_clock.h, saturn_menuart.h,
 |   saturn_saveslot.h, saturn_compat.h, disc.h, input.h, platform.h, client.h,
 |   main.h, video.h, fadecalc.h
 | Globals: N/A
 | Params: st -- state, already positioned on its opening screen; exclusive --
 |         non-zero to hide NBG0 and draw the title card behind the menu;
 |         useClock -- non-zero to run the music cycle and the attract timer;
 |         fade -- non-zero to fade the menu in on open and out on close;
 |         onNoMemory -- what to return if the scratch buffer cannot be had
 | Returns: the action that ended the loop, or onNoMemory
 ----------------------*/
static MenuAction menu_run(MenuState *st, int exclusive, int useClock,
                           int fade, MenuAction onNoMemory)
{
    static MenuItem items[MENU_LAYOUT_MAX_ITEMS];
    menu_clock_state clk;
    menu_clock_frame frame;
    MenuInput in;
    MenuAction action = MENU_ACT_NONE;
    const char *status = 0;
    unsigned char *scratch;
    int previous;
    int current;
    int pressed;
    int count = 0;
    int err;
    int exclusiveNow = exclusive;
    int fadeInStep = 0;
    unsigned int fadeInStart = 0;

    scratch = (unsigned char *)saturn_lwram_alloc(SAVE_MAX_BYTES);

    if (scratch == 0)
    {
        printf("menu_run: no LWRAM for the probe scratch\n");
        rest(0);
        return onNoMemory;
    }

    menu_art_begin(exclusiveNow);

    if (fade)
    {
        menu_art_fade(0);
        fadeInStep = FADECALC_SEGA_CD_STEPS;
        fadeInStart = platform_ticks();
    }
    else
    {
        menu_art_fade(FADECALC_LEVEL_NORMAL);
    }

    menu_art_draw(items, 0);
    menu_art_present();

    check_events();
    previous = menu_key_mask();

    menu_rescan(st, scratch);

    if (useClock)
    {
        menu_clock_enter(&clk, platform_ticks());
        disc_play_track(MENU_MUSIC_INDEX, 0);
    }

    while (cls.quit == 0)
    {
        check_events();
        current = menu_key_mask();
        pressed = current & ~previous;
        previous = current;

        menu_edges(pressed, &in);
        action = menu_state_step(st, &in);

        if (action == MENU_ACT_RESCAN_SLOTS)
        {
            menu_rescan(st, scratch);
            action = MENU_ACT_NONE;
        }
        else if (action == MENU_ACT_SAVE_SLOT)
        {
            err = saturn_saveslot_save(st->device, st->slotCursor);
            status = menu_layout_status_text(err, st->device);
            menu_rescan(st, scratch);
            action = MENU_ACT_NONE;
        }
        else if (action == MENU_ACT_LOAD_SLOT)
        {
            err = saturn_saveslot_load(st->device, st->slotCursor);

            if (err == SAT_BUP_OK)
            {
                break;
            }
            status = menu_layout_status_text(err, st->device);
            action = MENU_ACT_NONE;
        }
        else if (action != MENU_ACT_NONE)
        {
            break;
        }

        if (useClock)
        {
            menu_clock_step(&clk, platform_ticks(), pressed != 0, &frame);
            disc_set_music_volume(frame.music_volume);

            if (frame.music_restart)
            {
                disc_play_track(MENU_MUSIC_INDEX, 0);
            }
            if (frame.launch_attract && st->screen == MENU_TITLE)
            {
                if (fade)
                {
                    menu_fade_out(items, count, useClock);
                }
                menu_art_end();
                menu_art_present();
                disc_stop_track();
                disc_set_music_volume((unsigned char)MENU_VOLUME_MAX);
                play_intro();
                menu_art_begin(1);
                exclusiveNow = 1;
                menu_state_enter_title(st);
                status = 0;

                /* The cinematic ends black and the menu that replaces it comes
                   up out of that same black, which is the whole of the
                   "skipping fades back in to the title card" requirement. The
                   art is already at level 0 from the fade out above, so this
                   only re-arms the ramp. */
                if (fade)
                {
                    menu_art_fade(0);
                    fadeInStep = FADECALC_SEGA_CD_STEPS;
                    fadeInStart = platform_ticks();
                }

                menu_clock_enter(&clk, platform_ticks());
                disc_play_track(MENU_MUSIC_INDEX, 0);
                check_events();
                previous = menu_key_mask();
                continue;
            }
        }

        if (st->screen == MENU_TITLE && !exclusiveNow)
        {
            exclusiveNow = 1;
            menu_art_begin(1);
            video_set_fade(0);

            if (fade)
            {
                menu_art_fade(0);
                fadeInStep = FADECALC_SEGA_CD_STEPS;
                fadeInStart = platform_ticks();
            }
        }

        if (fadeInStep > 0)
        {
            unsigned int elapsed = platform_ticks() - fadeInStart;
            int step = FADECALC_SEGA_CD_STEPS
                     - (int)(elapsed / MENU_FADE_HOLD_MS);

            if (step < 0)
            {
                step = 0;
            }

            if (step != fadeInStep)
            {
                fadeInStep = step;
                menu_art_fade(fadecalc_step_level(step,
                                                  FADECALC_SEGA_CD_STEPS));
            }
        }

        count = menu_layout_build(st, status, items, MENU_LAYOUT_MAX_ITEMS);
        menu_art_draw(items, count);
        menu_art_present();
    }

    if (fade)
    {
        menu_fade_out(items, count, useClock && action != MENU_ACT_LOAD_SLOT);
    }

    menu_art_end();
    menu_art_present();

    if (useClock)
    {
        disc_set_music_volume((unsigned char)MENU_VOLUME_MAX);

        if (action != MENU_ACT_LOAD_SLOT)
        {
            disc_stop_track();
        }
    }

    saturn_lwram_free(scratch);
    rest(0);

    return action;
}

/*----------------------
 | menu_gate
 | Description: See menu.h.
 |
 |   Both non-load exits set MENU_GATE_LOAD, so the next arrival opens the load
 |   screen rather than the sub-title menu -- a player who reaches this room
 |   again mid-playthrough got here by dying, and wants their saves.
 |
 |   ending_played is consumed, not merely read. Clearing it as the mode is
 |   selected is what makes the flag mean "this arrival" rather than "this
 |   playthrough": leave it set and every death in the game the player starts
 |   from the credits would land on the sub-title menu too.
 |
 |   death_played is consumed the same way and picks the other mode. It is not
 |   strictly needed to reach MENU_GATE_LOAD -- every non-load exit already sets
 |   that mode, so a death mid-playthrough would land on the load screen anyway
 |   -- but the very first arrival of a session is MENU_GATE_SUBTITLE, and a
 |   player who dies before ever seeing this menu would otherwise be shown the
 |   sub-title screen for their first death. Clearing it also stops it surviving
 |   into a later gate arrival it has nothing to do with.
 |
 |   An ending wins if both are somehow set. The two cannot overlap in practice:
 |   each ends its script and arrives here on the next frame, and this consumes
 |   both. If they ever did, the credits are the stronger statement about where
 |   the player is -- a finished game wants a fresh start offered, not a list of
 |   saves from the playthrough that just ended.
 |
 |   menu_pause_poll is the only other thing that sets the mode back to
 |   MENU_GATE_SUBTITLE.
 |
 |   Every exit that hands control to the game arms screen.c's deferred fade in,
 |   because every one of them is leaving a black screen: the intro fades to
 |   black to get here, and menu_run's own fade out blacks both layers on the
 |   way past whatever else was showing. That includes the load return, which
 |   draws nothing itself -- quickload restores the room, the backdrop and the
 |   task program counters, and the first frame the restored script draws is the
 |   one that has new pixels in it. Arming on a screen that is somehow already
 |   lit is not a fault either: the ramp checks the level before it starts and
 |   declines to black a lit screen in order to bring it up again.
 |
 |   A death arrives with its last frame still on screen at full brightness --
 |   play_death_animation stopped fading it out precisely so the load card can
 |   be composited over it -- so that arrival, and only that arrival, opens the
 |   menu in overlay mode. Every other one has nothing behind it worth showing
 |   and opens exclusive, on the title card. s_gateMode cannot answer this:
 |   Return to Title and a cancelled load both set MENU_GATE_LOAD too, and
 |   neither leaves a frame worth holding.
 |
 |   s_pausePrev is re-primed on the way out even though this function never
 |   reads it. menu_pause_poll is not called while the gate is up -- run() is
 |   blocked inside here -- so its edge state would otherwise be as old as the
 |   frame before the menu opened, and a player who pressed Start on the
 |   sub-title screen and was still holding it when the game began would get
 |   the pause menu on the first frame of play.
 |
 |   Every path that returns MENU_START_ROOM resets the VM first, exactly as
 |   init() does at main.c:212. Before this branch there was no way to begin a
 |   new game mid-session -- the original password room was the only route from
 |   a finished playthrough back to a fresh room, and this branch makes that
 |   room unreachable by design -- so vm_reset() had one caller and needed no
 |   second. Now Return to Title, the ending credits and a cancelled post-death
 |   load screen all reach START GAME, and without this room 1 would begin with
 |   the last playthrough's progress flags, inventory bits and seen-cutscene
 |   variables still set: a failure that only appears on hardware and reads as
 |   a content bug. On the very first boot this merely repeats what init() just
 |   did, which costs one memset-sized reset and changes nothing.
 |
 |   The load path is deliberately excluded. quickload() restores variables[]
 |   and auxvars[] itself as part of the load, so resetting there would destroy
 |   the state the player just asked for.
 | Author: suinevere
 | Dependencies: menu_state.h, saturn_menuart.h, input.h, vm.h
 | Globals: s_gateMode, s_pausePrev, ending_played, death_played
 | Params: N/A
 | Returns: the room to load, or 0 if a load already restored the game state
 ----------------------*/
int menu_gate(void)
{
    MenuState st;
    MenuAction action;
    int overDeath = 0;

    if (ending_played)
    {
        ending_played = 0;
        death_played = 0;
        s_gateMode = MENU_GATE_SUBTITLE;
    }
    else if (death_played)
    {
        death_played = 0;
        s_gateMode = MENU_GATE_LOAD;
        overDeath = 1;
    }

    if (!menu_art_load())
    {
        rest(0);
        vm_reset();
        set_variable(227, 1);
        screen_arm_fade_in();
        return MENU_START_ROOM;
    }

    memset(&st, 0, sizeof(st));

    if (s_gateMode == MENU_GATE_LOAD)
    {
        menu_state_enter_slots(&st, 0, MENU_TITLE);
    }
    else
    {
        menu_state_enter_title(&st);
    }

    action = menu_run(&st, overDeath ? 0 : 1, 1, 1, MENU_ACT_START_GAME);
    s_pausePrev = menu_key_mask();

    if (action == MENU_ACT_LOAD_SLOT)
    {
        s_gateMode = MENU_GATE_LOAD;
        screen_arm_fade_in();
        return 0;
    }

    s_gateMode = MENU_GATE_LOAD;
    vm_reset();
    set_variable(227, 1);
    screen_arm_fade_in();
    return MENU_START_ROOM;
}

/*----------------------
 | menu_pause_poll
 | Description: See menu.h.
 |
 |   The edge shape here is deliberately current & ~previous rather than the
 |   debug chord's whole-combination compare: going from Start plus B to Start
 |   plus A plus B without releasing changed the chord value and fired a save
 |   under the old shape.
 |
 |   s_pausePrev is re-primed from the live pad after the menu closes, because
 |   the player is very likely still holding the button that dismissed it, and
 |   the stale zero it would otherwise hold would reopen the menu on the next
 |   frame. The art-load failure path re-primes too: menu_art_load reads four
 |   files off the disc and does not cache a failure, so it spends real time
 |   and retries on every open, and the hole a stale mask leaves there is the
 |   same one the normal path closes.
 | Author: suinevere
 | Dependencies: menu_state.h, saturn_menuart.h
 | Globals: s_pausePrev, s_gateMode, next_script
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void menu_pause_poll(void)
{
    MenuState st;
    MenuAction action;
    int current = menu_key_mask();
    int pressed = current & ~s_pausePrev;

    s_pausePrev = current;

    if ((pressed & MENU_BIT_PAUSE) == 0)
    {
        return;
    }
    if (!menu_art_load())
    {
        s_pausePrev = menu_key_mask();
        rest(0);
        return;
    }

    memset(&st, 0, sizeof(st));
    menu_state_enter_pause(&st);

    action = menu_run(&st, 0, 0, 0, MENU_ACT_RESUME);
    s_pausePrev = menu_key_mask();

    if (action == MENU_ACT_RETURN_TO_TITLE)
    {
        s_gateMode = MENU_GATE_SUBTITLE;
        next_script = MENU_PASSWORD_ROOM;
    }
}

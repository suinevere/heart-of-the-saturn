#include <string.h>
#include <stdio.h>

#include "menu.h"
#include "menu_state.h"
#include "menu_layout.h"
#include "menu_clock.h"
#include "checkpoints.h"
#include "saturn_menuart.h"
#include "saturn_saveslot.h"
#include "saturn_progress.h"
#include "saturn_backup.h"
#include "savedata.h"
#include "savegame.h"
#include "saturn_compat.h"
#include "disc.h"
#include "input.h"
#include "keymap.h"
#include "saturn_keymap.h"
#include "saturn_reset.h"
#include "platform.h"
#include "client.h"
#include "main.h"
#include "screen.h"
#include "video.h"
#include "fadecalc.h"
#include "vm.h"

extern int next_script;
extern int ending_played;
extern int death_played;
extern int current_room;

extern int access_code_skip;
extern int return_to_boot;

void rest(int fps);

#define MENU_GATE_FRONT  0
#define MENU_GATE_RESUME 1

#define MENU_FADE_HOLD_MS 60

#define MENU_MESSAGE_MS 3000

#define MENU_MESSAGE_MAX_ITEMS (2 + MENU_ROW_CHARS)

#define MENU_SAVE_SLOT 0

static int s_gateMode = MENU_GATE_RESUME;

static unsigned long s_reached;
static int           s_progressLoaded;

static int s_offRecord;
static int s_unlocked;

static int s_pausePrev;

#define MENU_BIT_UP      0x01
#define MENU_BIT_DOWN    0x02
#define MENU_BIT_LEFT    0x04
#define MENU_BIT_RIGHT   0x08
#define MENU_BIT_CONFIRM 0x10
#define MENU_BIT_CANCEL  0x20
#define MENU_BIT_PAUSE   0x40

static int menu_key_mask(void)
{
    unsigned int raw = input_raw_buttons();
    int mask = 0;

    if (raw & PAD_BIT_UP)    mask |= MENU_BIT_UP;
    if (raw & PAD_BIT_DOWN)  mask |= MENU_BIT_DOWN;
    if (raw & PAD_BIT_LEFT)  mask |= MENU_BIT_LEFT;
    if (raw & PAD_BIT_RIGHT) mask |= MENU_BIT_RIGHT;
    if (raw & (PAD_BIT_A | PAD_BIT_C)) mask |= MENU_BIT_CONFIRM;
    if (raw & PAD_BIT_B)     mask |= MENU_BIT_CANCEL;
    if (raw & PAD_BIT_START) mask |= MENU_BIT_PAUSE;

    return mask;
}

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

static PadButton first_pressed(unsigned int rawPressed)
{
    int b;

    for (b = (int)PAD_A; b <= (int)PAD_R; b++) {
        if (rawPressed & keymap_button_bit((PadButton)b)) {
            return (PadButton)b;
        }
    }
    return PAD_NONE;
}

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

static int s_jumpCode;
static int s_jumpRoom;
static int s_jumpEntry;

static unsigned long menu_progress(void)
{
    if (!s_progressLoaded)
    {
        s_reached = saturn_progress_load();
        s_progressLoaded = 1;
    }
    return s_reached;
}

static int menu_perform_jump(void)
{
    vm_reset();
    set_variable(VM_VAR_CODE_SEEN, 1);

    if (s_jumpCode != 0)
    {
        return vm_enter_code(s_jumpCode, 0);
    }

    set_variable(220, (unsigned short)s_jumpEntry);
    return s_jumpRoom;
}

static void menu_rescan(MenuState *st, unsigned char *scratch)
{
    SatBupDev internal;
    SatBupDev cart;

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

    savedata_probe(st->device, MENU_SAVE_SLOT, &st->save, scratch,
                   SAVE_MAX_BYTES);
    st->hasSave = st->save.state != SLOT_EMPTY;
}

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

static void menu_message(const char *text)
{
    MenuItem items[MENU_MESSAGE_MAX_ITEMS];
    int count = menu_layout_message(text, items, MENU_MESSAGE_MAX_ITEMS);
    unsigned int start;
    unsigned int previousRaw;

    menu_art_draw(items, count);
    menu_art_present();
    check_events();
    previousRaw = input_raw_buttons();
    start = platform_ticks();

    while (cls.quit == 0 && platform_ticks() - start < MENU_MESSAGE_MS)
    {
        unsigned int currentRaw;

        menu_art_draw(items, count);
        menu_art_present();
        check_events();

        currentRaw = input_raw_buttons();
        if ((currentRaw & ~previousRaw) != 0)
        {
            break;
        }
        previousRaw = currentRaw;
    }
}

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
    unsigned int previousRaw;
    unsigned int currentRaw;
    unsigned int pressedRaw;
    int count = 0;
    int err;
    int exclusiveNow = exclusive;
    int fadeInStep = 0;
    unsigned int fadeInStart = 0;

    scratch = (unsigned char *)saturn_lwram_alloc(SAVE_MAX_BYTES);

    if (scratch == 0)
    {
        printf("menu_run: no LWRAM for the probe scratch\n");
        input_swallow_held();
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
    previousRaw = input_raw_buttons();

    menu_rescan(st, scratch);
    st->unlocked = s_unlocked;
    st->reached = menu_progress();

    if (useClock)
    {
        menu_clock_enter(&clk, platform_ticks());
        disc_play_track(MENU_MUSIC_INDEX, 0);
    }
    else
    {
        disc_pause_music();
    }

    while (cls.quit == 0)
    {
        if (saturn_reset_taken())
        {
            menu_soft_reset();
            break;
        }

        check_events();
        current = menu_key_mask();
        pressed = current & ~previous;
        previous = current;
        currentRaw = input_raw_buttons();
        pressedRaw = currentRaw & ~previousRaw;
        previousRaw = currentRaw;

        menu_edges(pressed, &in);
        in.captured = first_pressed(pressedRaw);
        action = menu_state_step(st, &in);

        if (action == MENU_ACT_SAVE_GAME)
        {
            err = saturn_saveslot_save(st->device, MENU_SAVE_SLOT);

            if (err == SAT_BUP_OK)
            {
                menu_message("GAME SAVED");
                status = 0;
            }
            else
            {
                status = menu_layout_status_text(err, st->device);
            }

            menu_rescan(st, scratch);
            action = MENU_ACT_NONE;
            check_events();
            previous = menu_key_mask();
            previousRaw = input_raw_buttons();
        }
        else if (action == MENU_ACT_LOAD_GAME)
        {
            if (st->save.flags & SAVE_FLAG_CHECKPOINT)
            {
                unsigned short room = 0;
                unsigned char entry = 0;

                err = savegame_read_checkpoint(st->device, MENU_SAVE_SLOT,
                                               &room, &entry, scratch,
                                               SAVE_MAX_BYTES);
                if (err == SAT_BUP_OK)
                {
                    s_offRecord = 0;
                    s_jumpCode = 0;
                    s_jumpRoom = (int)room;
                    s_jumpEntry = (int)entry;
                    action = MENU_ACT_START_CHECKPOINT;
                    break;
                }
                status = menu_layout_status_text(err, st->device);
                action = MENU_ACT_NONE;
            }
            else
            {
                err = saturn_saveslot_load(st->device, MENU_SAVE_SLOT);

                if (err == SAT_BUP_OK)
                {
                    s_offRecord = 0;
                    break;
                }
                status = menu_layout_status_text(err, st->device);
                action = MENU_ACT_NONE;
            }
        }
        else if (action == MENU_ACT_SAVE_AND_RESUME
                 || action == MENU_ACT_SAVE_AND_QUIT)
        {
            err = savegame_write_checkpoint(st->device, MENU_SAVE_SLOT,
                                            (unsigned short)current_room,
                                            (unsigned char)get_variable(220),
                                            scratch, SAVE_MAX_BYTES);
            if (err != SAT_BUP_OK)
            {
                status = menu_layout_status_text(err, st->device);
                menu_rescan(st, scratch);
                action = MENU_ACT_NONE;
            }
            else
            {
                break;
            }
        }
        else if (action == MENU_ACT_START_CHECKPOINT)
        {
            int index = st->checkpoint;

            if (index > 0 && (st->reached & (1UL << index)) == 0UL)
            {
                s_offRecord = 1;
            }
            s_jumpCode = checkpoint_code(index);
            break;
        }
        else if (action == MENU_ACT_SAVE_KEYMAP)
        {
            keymap_set_active(&st->map);
            err = saturn_keymap_save(&st->map);
            status = menu_layout_status_text(err, st->device);

            if (err != SAT_BUP_OK)
            {
                st->screen = MENU_CONTROLS;
            }
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
                previousRaw = input_raw_buttons();
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

    s_unlocked = st->unlocked;

    if (fade)
    {
        menu_fade_out(items, count, useClock && action != MENU_ACT_LOAD_GAME);
    }

    menu_art_end();
    menu_art_present();

    if (useClock)
    {
        disc_set_music_volume((unsigned char)MENU_VOLUME_MAX);

        if (action != MENU_ACT_LOAD_GAME)
        {
            disc_stop_track();
        }
    }
    else if (action != MENU_ACT_LOAD_GAME
             && action != MENU_ACT_START_CHECKPOINT
             && action != MENU_ACT_RETURN_TO_TITLE)
    {
        disc_resume_music();
    }

    saturn_lwram_free(scratch);

    input_swallow_held();
    rest(0);

    return action;
}

static int menu_begin_new_game(void)
{
    s_gateMode = MENU_GATE_RESUME;
    s_offRecord = 0;

    play_intro();
    screen_arm_fade_restore();

    vm_reset();
    set_variable(VM_VAR_CODE_SEEN, 1);
    screen_arm_fade_in();
    return MENU_START_ROOM;
}

static int menu_apply_action(MenuAction action)
{
    if (action == MENU_ACT_LOAD_GAME)
    {
        s_gateMode = MENU_GATE_RESUME;
        screen_arm_fade_in();
        return 0;
    }
    if (action == MENU_ACT_START_CHECKPOINT)
    {
        int room = menu_perform_jump();

        s_gateMode = MENU_GATE_RESUME;
        screen_arm_fade_in();
        return room;
    }
    if (action == MENU_ACT_RESUME || action == MENU_ACT_SAVE_AND_RESUME)
    {
        s_gateMode = MENU_GATE_RESUME;
        access_code_skip = ACCESS_CODE_SKIP_ARMED;
        screen_arm_fade_in();
        return current_room;
    }
    if (action == MENU_ACT_SAVE_AND_QUIT)
    {
        return menu_front();
    }
    return menu_begin_new_game();
}

int menu_front(void)
{
    MenuState st;
    MenuAction action;

    if (!menu_art_load())
    {
        rest(0);
        return menu_begin_new_game();
    }

    memset(&st, 0, sizeof(st));
    menu_state_enter_title(&st);

    action = menu_run(&st, 1, 1, 1, MENU_ACT_START_GAME);
    s_pausePrev = menu_key_mask();

    return menu_apply_action(action);
}

int menu_gate(void)
{
    MenuState st;
    MenuAction action;
    int wasEnding = ending_played;
    int wasDeath = death_played;

    ending_played = 0;
    death_played = 0;

    if (wasEnding || s_gateMode == MENU_GATE_FRONT)
    {
        return menu_front();
    }
    if (!wasDeath)
    {
        s_gateMode = MENU_GATE_RESUME;
        access_code_skip = ACCESS_CODE_SKIP_ARMED;
        screen_arm_fade_in();
        return (current_room != 0) ? current_room : MENU_START_ROOM;
    }

    if (!menu_art_load())
    {
        rest(0);
        s_gateMode = MENU_GATE_RESUME;
        access_code_skip = ACCESS_CODE_SKIP_ARMED;
        screen_arm_fade_in();
        return current_room;
    }

    memset(&st, 0, sizeof(st));
    menu_state_enter_death(&st);

    action = menu_run(&st, 0, 1, 1, MENU_ACT_RESUME);
    s_pausePrev = menu_key_mask();

    return menu_apply_action(action);
}

void menu_soft_reset(void)
{
    fade_out_begin();
    fade_out_finish();

    s_gateMode = MENU_GATE_FRONT;
    s_pausePrev = menu_key_mask();

    return_to_boot = 1;
    cls.quit = 1;
}

void menu_reset_for_boot(void)
{
    s_gateMode = MENU_GATE_RESUME;
    s_pausePrev = 0;
    s_unlocked = 0;
    s_reached = 0;
    s_progressLoaded = 0;
    s_offRecord = 0;
}

void menu_note_checkpoint(void)
{
    unsigned long mask;
    unsigned long bit;
    int index;

    if (s_offRecord || s_unlocked)
    {
        return;
    }

    index = checkpoint_find(current_room, (int)get_variable(220));

    if (index < 0)
    {
        return;
    }

    mask = menu_progress();
    bit = 1UL << index;

    if ((mask & bit) != 0UL)
    {
        return;
    }

    s_reached = mask | bit;
    saturn_progress_save(s_reached);
}

void menu_pause_poll(void)
{
    MenuState st;
    MenuAction action;
    int current = menu_key_mask();
    int pressed = current & ~s_pausePrev;

    s_pausePrev = current;

    if (saturn_reset_taken())
    {
        menu_soft_reset();
        return;
    }
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

    if (action == MENU_ACT_START_CHECKPOINT)
    {
        s_gateMode = MENU_GATE_RESUME;
        next_script = menu_perform_jump();
        return;
    }

    if (action == MENU_ACT_RETURN_TO_TITLE)
    {
        s_gateMode = MENU_GATE_FRONT;
        next_script = MENU_PASSWORD_ROOM;
    }
}

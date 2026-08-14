/*----------------------
 | menu_clock.h
 | Description: The sub-title screen's timing: a CD-DA volume staircase, a
 |   40-second music cycle, and a 15-second idle trigger for the attract
 |   cinematic. Same shape as bootmenu.h -- decisions returned rather than
 |   performed, all arithmetic on unsigned differences so it stays correct
 |   across the millisecond counter's wrap -- but independent of it: separate
 |   MENU_* constants, no shared state, no include of bootmenu.h.
 |
 |   Deliberately free of SRL, stdio and every engine header, for the same
 |   reason bootmenu.h and discfmt.h are: compiled into the engine and by
 |   saturn/tests/run_tests.sh with the host gcc. A later task turns what this
 |   returns into disc calls.
 | Author: suinevere
 | Dependencies: stdint.h is not needed -- unsigned int and unsigned char
 |   match bootmenu.h's uint32_t/uint8_t exactly on this toolchain, and the
 |   brief's interface spells them out that way.
 ----------------------*/
#ifndef MENU_CLOCK_H
#define MENU_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MENU_MUSIC_INDEX
 | Description: The engine music index the sub-title screen plays.
 |   discfmt_cue_track_for_music maps it to cue track 3 -- track03.wav,
 |   2:46.17. The disc numbers its 41 audio tracks 02..42, so this is the
 |   second audio track despite being disc track three. The wrong index is
 |   inaudible as a bug and merely sounds like different music, which is why
 |   test_menu_clock.c pins the mapping against discfmt.c directly.
 | Author: suinevere
 ----------------------*/
#define MENU_MUSIC_INDEX     2

/*----------------------
 | MENU_MUSIC_CYCLE_MS
 | Description: How long the track plays before menu_clock_step restarts it
 |   silently -- see menu_ramp's banner in menu_clock.c for why the restart
 |   and the silence coincide.
 | Author: suinevere
 ----------------------*/
#define MENU_MUSIC_CYCLE_MS  40000u

/*----------------------
 | MENU_FADE_MS
 | Description: How long the fade in and fade out each take, at either end
 |   of a music cycle.
 | Author: suinevere
 ----------------------*/
#define MENU_FADE_MS          1000u

/*----------------------
 | MENU_IDLE_MS
 | Description: How long without input before menu_clock_step reports
 |   launch_attract. This timer runs only while the sub-title screen itself
 |   is up -- menu.c does not call menu_clock_step while the slot list or a
 |   confirm prompt is showing, because a player reading three slot rows is
 |   not idle, and menu_clock has no way to know a submenu is open other than
 |   not being asked.
 | Author: suinevere
 ----------------------*/
#define MENU_IDLE_MS         15000u

/*----------------------
 | MENU_VOLUME_MAX
 | Description: Full CD-DA volume. SND_SetCdDaLev takes 0..7, so a fade is
 |   an eight-step staircase, not a ramp. Lengthening MENU_FADE_MS adds time
 |   to notice each step, never resolution.
 | Author: suinevere
 ----------------------*/
#define MENU_VOLUME_MAX          7u

/*----------------------
 | menu_clock_state
 | Description: Everything the sub-title screen's clock remembers between
 |   frames. Opaque to callers by convention; only menu_clock.c reads the
 |   fields.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned int music_start_ms;
    unsigned int idle_start_ms;
} menu_clock_state;

/*----------------------
 | menu_clock_frame
 | Description: What the caller should do this frame.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char music_volume;
    int           music_restart;
    int           launch_attract;
} menu_clock_frame;

/*----------------------
 | menu_clock_enter
 | Description: Resets both timers. The caller starts the track immediately
 |   after; the first frame's volume is therefore 0 and fades in, which is
 |   what makes re-entry from the attract loop silent at the seam instead of
 |   clicking.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to initialise; now_ms -- the clock's current reading,
 |         which need not be zero
 | Returns: N/A
 ----------------------*/
void menu_clock_enter(menu_clock_state *st, unsigned int now_ms);

/*----------------------
 | menu_clock_step
 | Description: Advances the clock one frame and reports this frame's music
 |   volume, whether the track just restarted, and whether the idle timer
 |   has fired. All arithmetic is on unsigned differences, so it is correct
 |   across the millisecond counter's wrap.
 |
 |   On the restart frame, resetting music_ms to 0 before it feeds menu_ramp
 |   is deliberately redundant with menu_ramp's own out-of-range branch --
 |   without the reset, the stale elapsed value is already >=
 |   MENU_MUSIC_CYCLE_MS, and menu_ramp returns 0 for that too, so a test
 |   cannot observe the reset by itself. It is kept anyway: the restart
 |   frame's silence should follow from the cycle actually restarting at
 |   elapsed zero, not from an out-of-range guard in a different function.
 |   Deleting it would leave that silence dependent on menu_ramp never
 |   clamping instead of returning 0 for out-of-range input -- a future
 |   change there could reintroduce the 0->7 seam click this design exists
 |   to remove, silently and near-unobservably on real hardware.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state; now_ms -- clock reading; had_input -- nonzero if the
 |         player touched the pad this frame, which resets the idle timer;
 |         out -- filled in with this frame's decisions
 | Returns: N/A
 ----------------------*/
void menu_clock_step(menu_clock_state *st, unsigned int now_ms, int had_input,
                     menu_clock_frame *out);

#ifdef __cplusplus
}
#endif

#endif /* MENU_CLOCK_H */

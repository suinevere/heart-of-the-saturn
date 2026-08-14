#ifndef __SCREEN_INCLUDED__
#define __SCREEN_INCLUDED__

int screen_init();

int get_selected_screen();
char *get_selected_screen_ptr();

void select_screen(int which);
void update_screen(int which);
char *get_screen_ptr(int which);

/*----------------------
 | screen_arm_fade_restore
 | Description: Asks for FADECALC_LEVEL_NORMAL to be written on the next frame
 |   the game actually draws, rather than now.
 |
 |   Armed rather than immediate because the two halves of the decision live in
 |   different places. The caller is the one that knows it is handing the game a
 |   screen it left black -- a cutscene that has just faded out, a death that
 |   has, a menu that is closing over a black layer -- but only update_screen
 |   knows when new pixels have replaced the outgoing scene. A fade blacks the
 |   screen by darkening the palette while the framebuffer still holds the old
 |   picture, so writing the level back before anything new is drawn shows that
 |   old picture at full brightness until the first frame lands. A frame count
 |   would not help either: what sits in that gap is a room load, and its length
 |   is the drive's to decide.
 |
 |   This is animation.c's g_restorePending for the game path. That one hangs
 |   off copy_to_screen, the animation player's draw choke point; this one hangs
 |   off update_screen, which is the game's.
 |
 |   Every call to this is unconditional, including the ones sitting next to
 |   code that is not. The rule is that whatever fades to black and then hands
 |   control to the game arms this, and the rule has no platform in it because
 |   the fades have no platform in them either -- play_animation's tail and
 |   play_death_animation's terminal fade compile into every build. The menu
 |   layer does not, so a call guarded to match the menu routing beside it
 |   leaves the fade with nothing to undo it: that is exactly how the host
 |   build's intro, ending and every death ended up behind a screen that never
 |   came back. Arming twice costs one palette rebuild at normal level and
 |   nothing else, so err toward arming.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void screen_arm_fade_restore(void);

/*----------------------
 | screen_arm_fade_in
 | Description: screen_arm_fade_restore's ramped form: instead of one write of
 |   FADECALC_LEVEL_NORMAL on the frame that has new pixels, walk the ladder up
 |   from black across the frames after it.
 |
 |   Everything screen_arm_fade_restore's banner says about deferring applies
 |   unchanged; the only difference is what happens once the deferral is spent.
 |   The ramp is scheduled on the clock rather than counted per call, so a room
 |   drawing at 12 fps and one drawing at vsync take the same wall clock to come
 |   up, and it costs no time of its own -- it runs over the room's opening
 |   frames rather than in front of them, exactly as animation.c's does over a
 |   cutscene's.
 |
 |   A screen that is already at normal brightness when the first frame lands is
 |   not ramped: there is nothing to lift, and starting from black would black a
 |   lit screen to do it. That is the case the load screen over a held death
 |   frame arrives in.
 |
 |   The two arms are mutually exclusive -- arming either disarms the other --
 |   because a ramp and a single write disagree about the same palette, and the
 |   write would win by landing first.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void screen_arm_fade_in(void);

/*----------------------
 | screen_fade_cancel
 | Description: Forgets both arms and any ramp in flight, without writing a
 |   level.
 |
 |   Called by main.c's fade_out_arm, because a fade out is a statement about
 |   the same palette a pending ramp is walking, and the two would then write
 |   alternating levels for as long as both ran. Whoever fades out is by
 |   definition ending whatever the ramp was bringing up.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void screen_fade_cancel(void);

void copy_screen(int dest, int src);

/** Fills the entire screen with a single color
    @param dest     --unused--
    @param color    entry from 4 bit palette
*/
void fill_screen(int dest, char color);

void fill_line(int count, int x, int y, int color);
void fill_line_reversed(int count, int x, int y, int color);

#endif


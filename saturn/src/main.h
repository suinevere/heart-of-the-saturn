#ifndef __MAIN_INCLUDED__
#define __MAIN_INCLUDED__

void update_keys();

/*----------------------
 | play_intro
 | Description: Plays the four-file opening cinematic, INTRO1..4.BIN over cue
 |   tracks 31..34. Any button breaks the whole sequence, not just the current
 |   file, because play_anm is called with skippable == 0.
 |
 |   Not static, because menus/menu.c replays it as the sub-title menu's
 |   attract loop. The alternative -- a re-entrant gate returning "play the
 |   cinematic and call me again" -- buys nothing and complicates the one loop
 |   in the program that must stay readable.
 | Author: suinevere
 | Dependencies: disc.h, animation.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void play_intro(void);

/*----------------------
 | fade_out_begin / fade_out_finish
 | Description: A fade out that runs underneath disc work rather than in
 |   front of it, and so costs no time of its own.
 |
 |   fade_out_begin starts the clock and installs the fade as the disc
 |   layer's tick (see disc.h). From then on every read chunk boundary and
 |   every poll of the music wait advances the palette, so the fade plays
 |   across the second and a half of loading that was going to happen
 |   anyway. fade_out_finish spends whatever is left of the fade if the disc
 |   work finished early, uninstalls the tick, and returns with the screen
 |   black.
 |
 |   Between the two calls the caller must do the disc work -- that is the
 |   entire arrangement. A begin/finish pair with nothing between them
 |   degrades to an ordinary blocking fade, correct but pointless.
 |
 |   A fade out only ever darkens, and on a screen that is already black this
 |   pair costs nothing at all. fade_out_begin asks video_get_fade() where the
 |   palette is; at 0 it writes no levels and arms the state as already
 |   finished, so finish has nothing left to spend and returns at once. The
 |   tick is still installed and uninstalled exactly as on any other path, so
 |   the pair stays balanced whichever way it went.
 |
 |   The check lives in here rather than at the call sites because this is the
 |   only place that can ask -- and because it is what lets a caller open with
 |   the pair unconditionally. Every animation does: it cannot know whether the
 |   thing before it left a picture on screen or a black one, and without this
 |   two cutscenes back to back would spend half a second fading black to black
 |   between them, after first flashing the outgoing frame back up at seven
 |   eighths brightness to have something to fade from.
 |
 |   fade_out_begin_hold does not take the short cut -- see its banner.
 |
 |   There is no fade in here, and there will not be one. Coming back up is not
 |   a transition: it runs during the incoming scene rather than between two of
 |   them, so it has no disc work to hide behind and nothing for the tick above
 |   to drive it from. Whatever leaves the screen black owns lifting it, and
 |   both owners walk the ladder from their own draw choke point --
 |   animation.c's copy_to_screen for a cutscene or a death, screen.c's
 |   update_screen for the game. See video.h for why setting a palette does not
 |   lift the level on its own.
 |
 |   A fade out cancels whatever ramp screen.c is holding, since the two write
 |   the same palette and only one of them can be right.
 | Author: suinevere
 | Dependencies: video.h, fadecalc.h, platform.h, disc.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void fade_out_begin(void);
void fade_out_finish(void);

/*----------------------
 | fade_out_begin_hold
 | Description: fade_out_begin with an explicit per-step hold, so a fade can be
 |   made slower without gaining steps -- the same eight pictures, each held
 |   longer. fade_out_begin is this with FADE_HOLD_MS.
 |
 |   Exists for the death that plays two sequences back to back. That death is
 |   two adjacent script opcodes with nothing between them, so the second
 |   sequence's fade covered no disc work -- but its half second was the delay
 |   syncing the splat, and four attempts to delete it lost the splat. Spending
 |   both halves in one slower fade up front leaves the middle of the death
 |   alone. The hold is in milliseconds rather than whole fade lengths because
 |   it is being tuned by eye and a doubling is too coarse a step. See
 |   mem/2026-08-11-death-animation-cdda-sync.md.
 |
 |   Unlike fade_out_begin, this spends its full duration even when the screen
 |   is already black. What it is asked for is the time, not the picture: the
 |   hold is what holds the splat in place, and letting it finish early on some
 |   arrivals and not others would retune by accident a number that was settled
 |   by eye. It still writes nothing on a black screen -- only darkening is a
 |   fade -- so a silent hold is a plain wait.
 |
 |   Its one caller passes DEATH_CHAINED_HOLD_MS, 200 ms per step and so 1600 ms
 |   over the eight. Older prose here and in mem/ said 960; see that constant's
 |   banner in animation.c for which is right and why.
 | Author: suinevere
 | Dependencies: video.h, fadecalc.h, platform.h, disc.h
 | Globals: N/A
 | Params: hold_ms -- milliseconds each of the eight steps stays on screen,
 |   clamped to at least 1
 | Returns: N/A
 ----------------------*/
void fade_out_begin_hold(unsigned int hold_ms);

#endif

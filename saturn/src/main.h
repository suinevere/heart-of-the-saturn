#ifndef __MAIN_INCLUDED__
#define __MAIN_INCLUDED__

void update_keys();

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
 |   There is no fade in here, deliberately. The Sega CD fades a scene out and
 |   then the next one is simply there; coming back is a palette set at full
 |   brightness, not a second walk up the ladder. Whatever leaves the screen
 |   black is responsible for restoring it with
 |   video_set_fade(FADECALC_LEVEL_NORMAL) -- see video.h for why setting a
 |   palette does not do that on its own.
 |
 |   Deaths are the one exception and they own it themselves: animation.c walks
 |   the ladder back up across a death sequence's opening frames. It is not
 |   offered here because it is not a transition -- it runs during the incoming
 |   scene rather than between two of them, so it has no disc work to hide
 |   behind and nothing for the tick above to drive it from.
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
 | Author: suinevere
 | Dependencies: video.h, fadecalc.h, platform.h, disc.h
 | Globals: N/A
 | Params: hold_ms -- milliseconds each of the eight steps stays on screen,
 |   clamped to at least 1
 | Returns: N/A
 ----------------------*/
void fade_out_begin_hold(unsigned int hold_ms);

#endif

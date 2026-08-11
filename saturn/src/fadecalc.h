/*----------------------
 | fadecalc.h
 | Description: The arithmetic behind a palette fade, kept separate from both
 |   video backends for the same reason cdda_classify.c and discfmt.c are
 |   separate: it is pure, it is where the mistakes are, and a host test pins
 |   it in milliseconds where an emulator listen-and-look cannot.
 |
 |   Two things live here. fadecalc_scale is the per-channel dim, applied by
 |   each backend at its own colour depth so the widening stays where it
 |   already is -- 5 bits of CRAM on Saturn, 8 on the host -- and only the
 |   ratio is shared. fadecalc_step_level is the ladder: which level belongs
 |   to step i of a fade of n steps.
 |
 |   The ladder is deliberately short. A Sega CD fade is not a smooth ramp;
 |   it shows on the order of eight distinct pictures across its two seconds,
 |   and reproducing that means stepping the palette a handful of times and
 |   holding each one, not recomputing it every frame. FADECALC_SEGA_CD_STEPS
 |   is that count, named rather than spelled 8 at the call site so the
 |   provenance of the number survives.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef FADECALC_H
#define FADECALC_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | FADECALC_LEVEL_NORMAL
 | Description: The level at which a fade is not a fade: every channel comes
 |   back unchanged. Named because 255 appears in both backends and in the
 |   step ladder, and a reader should not have to infer that they mean the
 |   same 255.
 | Author: suinevere
 ----------------------*/
#define FADECALC_LEVEL_NORMAL 255

/*----------------------
 | FADECALC_SEGA_CD_STEPS
 | Description: How many distinct pictures a Sega CD fade puts on screen.
 |   Eight, across roughly two seconds -- a step every 250 ms or so, held.
 |   The chunkiness is the authentic part and is not to be smoothed out: a
 |   per-frame ramp over the same two seconds looks like a different game.
 | Author: suinevere
 ----------------------*/
#define FADECALC_SEGA_CD_STEPS 8

/*----------------------
 | fadecalc_scale
 | Description: Dims one already-widened colour channel by a fade level.
 |   Rounds to nearest rather than truncating, so a channel at full intensity
 |   comes back at full intensity for level 255 exactly, and the ladder's
 |   midpoints do not all sit one step dark.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: value -- channel at the backend's own depth, clamped below 0 and
 |   returned unchanged for level 255; level -- 0 (black) to 255 (normal),
 |   clamped
 | Returns: the dimmed channel, never above value and never below 0
 ----------------------*/
int fadecalc_scale(int value, int level);

/*----------------------
 | fadecalc_step_level
 | Description: The fade level for step `step` of a fade with `steps` steps.
 |   Step 0 is FADECALC_LEVEL_NORMAL and step `steps` is 0, so a fade out
 |   walks 0..steps inclusive and a fade in walks steps..0 -- one ladder
 |   serving both directions rather than two that can disagree.
 |
 |   That inclusive range is why a fade of n steps shows n+1 pictures, the
 |   first of which is the undimmed one already on screen. A caller wanting
 |   eight *new* pictures asks for eight steps and skips step 0, which is
 |   what it would do anyway: redrawing the picture already displayed costs
 |   a step of the fade's time budget and shows the viewer nothing.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: step -- 0..steps, clamped; steps -- 1 or more; 0 or less yields 0
 | Returns: level 0..255
 ----------------------*/
int fadecalc_step_level(int step, int steps);

#ifdef __cplusplus
}
#endif

#endif

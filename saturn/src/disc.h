/*----------------------
 | disc.h
 | Description: The single platform boundary between the engine and disc I/O.
 |   Every other engine file that needs bytes or music off the disc calls
 |   through these five functions and nothing else -- no FILE *, no SDL, no
 |   platform header leaks above this line. src/host/disc_cue.c is the host
 |   implementation, reading a real bin/cue rip through discfmt.h's pure
 |   sector and ISO9660 arithmetic. A Saturn implementation is a sibling of
 |   disc_cue.c -- same five functions, over SRL::Cd::File and
 |   SRL::Sound::Cdda instead of FILE * -- swapped in without touching a
 |   single caller above this header.
 |
 |   Call-ordering contract, all five functions: disc_open must be called,
 |   and must succeed, before the first disc_read_file or disc_play_track --
 |   both refuse (disc_read_file returns negative, disc_play_track is a
 |   silent no-op) if called first or after disc_open failed. disc_close is
 |   the only function safe to call before disc_open, after disc_open fails,
 |   or twice in a row -- it always leaves the seam in the same
 |   never-been-opened state, which is what lets it sit unconditionally in
 |   atexit_callback. disc_stop_track is likewise safe to call with nothing
 |   playing. disc_play_track/disc_stop_track do not require the disc to
 |   still be open at every call -- see src/host/disc_cue.c's ordering
 |   comment -- but disc_open must have succeeded at some point and
 |   disc_close must not have run since, or they no-op the same way
 |   disc_read_file does.
 |
 |   Caveat -- this is a drop-in seam for reads and playback, not for
 |   initialization: two things above this header are host-shaped and a
 |   Saturn backend will not reuse them as-is. First, main.c's audio-device
 |   setup (the Mix_OpenAudioDevice call, the allowed_changes=0 argument, the
 |   Mix_QuerySpec verification) exists solely to guarantee disc_cue.c's
 |   CD-DA precondition and is host-backend policy living in engine code --
 |   SRL::Sound::Cdda has no negotiated spec and no resampler question, so
 |   none of it applies on Saturn. Second, disc_open's cue_path parameter
 |   bakes in a host-filesystem concept: a Saturn disc has no cue sheet,
 |   there is one drive and one mounted disc, so a Saturn backend must
 |   accept and ignore the parameter, and main.c's find_cue_path/argv
 |   handling/opendir("cd") fallback is host-only discovery code with
 |   nothing to port. Neither is a design failure -- both are contained to
 |   main.c -- but a second implementer should know up front that "same five
 |   functions" covers the data path, not main()'s startup sequence.
 |
 |   Design: docs/superpowers/specs/2026-07-31-hota-bincue-disc-backend-design.md
 | Author: suinevere
 | Dependencies: none (only opaque parameters here; each backend pulls in
 |   whatever it needs on its own side of this line)
 ----------------------*/
#ifndef DISC_H
#define DISC_H

/* The Saturn backend is C++; without this its definitions would get C++
   linkage and fail to satisfy the C callers. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | disc_open
 | Description: Opens the disc named by cue_path, parses its layout and
 |   validates the 19-file manifest against the real ISO9660 directory
 |   before any caller can read a byte. Must run before the first
 |   disc_read_file call -- game2bin_init() is the earliest one in the
 |   engine's startup sequence. Returns 1 on success, 0 on failure, matching
 |   discfmt.h's convention since this function is built entirely out of
 |   discfmt calls. Safe to call again at any time, including after a prior
 |   disc_open succeeded or failed -- always starts from a clean slate (see
 |   src/host/disc_cue.c) rather than layering state on top of a previous
 |   open. On failure, leaves the seam exactly as if disc_open had never been
 |   called: no partial manifest, no stale cue table for disc_play_track to
 |   iterate.
 | Author: suinevere
 ----------------------*/
int disc_open(const char *cue_path);

/*----------------------
 | disc_read_file
 | Description: Reads a whole file off the disc's data track by ISO9660 name
 |   into out. max_size is the reason this signature differs from the old
 |   read_file(filename, out): none of this function's three call sites
 |   (src/main.c, src/animation.c, src/game2bin.c) bound-check the pointer
 |   they hand in -- each is a raw address into the emulated 68000 memory
 |   map, or a fixed-size static array, with no check anywhere else in the
 |   engine that the file actually fits. Without max_size, a wrong or
 |   truncated dump does not fail here -- it scribbles past the destination
 |   into unrelated VM state and surfaces minutes later as a corrupted room,
 |   nowhere near the read that caused it. Returns 0 on success, negative on
 |   failure, matching the `< 0` check every call site already has. Requires
 |   a prior disc_open that returned success and no disc_close since --
 |   called any other time, returns negative without touching *out.
 |
 |   Any other failure leaves *out undefined. A backend reading in chunks can
 |   fail partway through and hand back a destination that is part real data,
 |   part whatever was there before -- this header makes no promise about
 |   which -- and neither backend's own error path relies on partial contents,
 |   nor does any of the three call sites read *out after a negative return,
 |   so this is a documentation gap, not a live bug.
 |
 |   max_size bounds the bytes written to out, not the sectors read off the
 |   disc. A backend is free to transfer whole sectors for speed -- that is
 |   the difference between one drive request and forty -- but the partial
 |   final sector of a file carries bytes past the file's end that belong to
 |   nobody, and those must land somewhere the backend owns rather than in the
 |   caller's buffer. saturn/src/system/disc_srl.cxx does exactly that. This is
 |   the non-obvious constraint a third implementer would otherwise discover
 |   the way the second one nearly did.
 | Author: suinevere
 ----------------------*/
int disc_read_file(const char *name, void *out, int max_size);

/*----------------------
 | disc_play_track / disc_stop_track
 | Description: Starts and stops the disc's CD-DA music for the engine's
 |   music index, mapped to a cue track by discfmt_cue_track_for_music.
 |
 |   The two backends differ in kind, not just in API. src/host/disc_cue.c
 |   streams the raw track through Mix_HookMusic -- fread straight into the
 |   mixer's buffer, no decode, no resample -- and a concurrent
 |   disc_read_file cannot disturb it, because reads and playback are separate
 |   file handles. A Saturn has one drive: any read seeks away from the
 |   playing track and silences it, and main.c's play_anm calls
 |   disc_play_track and then immediately reads a whole animation file, so a
 |   backend that merely forwards these calls is silent for exactly the
 |   content the game has music for. saturn/src/system/disc_srl.cxx therefore
 |   owns playback state and brackets every read with a suspend and a
 |   restore. A third backend must do the same or accept silence.
 |
 |   disc_play_track requires the same precondition as disc_read_file (a
 |   disc_open that succeeded, no disc_close since) -- called any other time
 |   it is a silent no-op, same as calling it with cls.nosound set, or with a
 |   track the mounted disc does not carry. Call it any number of times; it
 |   always supersedes whatever was playing, except for a request identical to
 |   what the drive is confirmably already looping, which the Saturn backend
 |   drops to avoid a needless seek. disc_stop_track is always safe, including
 |   before disc_open, after disc_close, or with nothing playing, which is why
 |   atexit_callback can call it unconditionally right before disc_close.
 | Author: suinevere
 ----------------------*/
void disc_play_track(int engine_index, int loop);
void disc_stop_track(void);

/*----------------------
 | disc_close
 | Description: Releases whatever disc_open acquired (the data track
 |   FILE *, the cached directory buffer). Safe to call even if disc_open
 |   was never called, or failed partway through, so it can sit
 |   unconditionally in atexit_callback with no separate "was it open" check
 |   at the call site.
 | Author: suinevere
 ----------------------*/
void disc_close(void);

#ifdef __cplusplus
}
#endif

#endif /* DISC_H */

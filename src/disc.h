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
 |   Design: docs/superpowers/specs/2026-07-31-hota-bincue-disc-backend-design.md
 | Author: suinevere
 | Dependencies: none (only opaque parameters here; each backend pulls in
 |   whatever it needs on its own side of this line)
 ----------------------*/
#ifndef DISC_H
#define DISC_H

/*----------------------
 | disc_open
 | Description: Opens the disc named by cue_path, parses its layout and
 |   validates the 19-file manifest against the real ISO9660 directory
 |   before any caller can read a byte. Must run before the first
 |   disc_read_file call -- game2bin_init() is the earliest one in the
 |   engine's startup sequence. Returns 1 on success, 0 on failure, matching
 |   discfmt.h's convention since this function is built entirely out of
 |   discfmt calls.
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
 |   failure, matching the `< 0` check every call site already has.
 | Author: suinevere
 ----------------------*/
int disc_read_file(const char *name, void *out, int max_size);

/*----------------------
 | disc_play_track / disc_stop_track
 | Description: Streams the raw CD-DA audio track named by
 |   discfmt_cue_track_for_music(engine_index) through Mix_HookMusic --
 |   fread straight into the mixer's buffer, no decode, no resample, since
 |   the track and the audio device are the same format (44100/S16/stereo).
 |   disc_stop_track is the unhook-then-close half of disc_play_track's
 |   track-change sequence, exposed on its own for the callers that only
 |   need to stop. See src/host/disc_cue.c for the implementation and the
 |   ordering reason.
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

#endif /* DISC_H */

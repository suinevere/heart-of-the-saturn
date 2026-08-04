# Heart of The Alien → Sega Saturn — CD-DA Music Design Spec

**Date:** 2026-08-04
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Make the game's music play on Saturn.

The boot-and-video sub-project left `disc_play_track` and `disc_stop_track` as silent
no-ops in `saturn/src/system/disc_srl.cxx`, with `disc.h`'s contract explicitly
permitting that. This sub-project replaces them with real CD-DA playback over the CD
block, and teaches `disc_read_file` to coexist with it.

Done means: with `HOTA_AUDIO=full`, the disc boots and each of the four intro
animations plays with its music under it, the music survives the whole-file load that
immediately precedes it, and playback stops when the intro ends. With the default
`HOTA_AUDIO=none` disc, everything behaves exactly as it does today — no music, and
no CD-block damage from asking for tracks that are not there.

Sound effects are **not** in scope. `play_sample` and `sound_flush_cache` stay
no-ops in `sound_srl.cxx`; SFX over `SRL::Sound::Pcm` is a separate sub-project.

## The finding that drives every decision

**`SRL::Cd::TableOfContents` reads the wrong data, and `SRL::Sound::Cdda::Resume`
inherits the fault.**

`srl_cd.hpp:794` declares `struct TrackLocation : public ITrack`. `ITrack` holds
`unsigned int Control : 4`; the derived class adds `unsigned int fad : 24`. A bitfield
in a base subobject cannot share a storage unit with one declared in the derived class,
so `sizeof(TrackLocation)` is 8 bytes, not the 4 the layout assumes. `Tracks[99]` plus
the first-track, last-track and lead-out records measures roughly 812 bytes.

`GetTable()` (`srl_cd.hpp:858`) fills it with `CDC_TgetToc((uint32_t *)&toc)`. The BIOS
writes exactly 102 longwords — 408 bytes. Two consequences:

- `toc.Tracks[t]` reads longword `2t`: the wrong track, every time.
- Everything past `t = 50` is uninitialised stack, because the BIOS never wrote there.

`Cdda::Resume()` (`srl_sound.hpp:186`) derives its playback end address from
`toc.Tracks[TargetTrack + 1]`. The intro's tracks are cue 33–36, so it would read
longwords 68–74 — wrong entries — and seek somewhere else on the disc entirely.

This was found the expensive way in the sibling port `zaturn`, over several rounds,
and is documented in `zaturn/saturn/src/sound/music_cdda.cxx`. It has been re-verified
against the SRL checked in to this repo. Three decisions follow directly from it, and
none of them are negotiable:

1. This port does not call `SRL::Cd::TableOfContents`. It reads the BIOS TOC itself.
2. This port does not call `Cdda::Resume` or `Cdda::StopPause`. It writes its own
   pause and resume from raw `CDC_*` calls.
3. `Cdda::PlaySingle` **is** used. It is plain `CDC_CdPlay` by track number and never
   touches the table.

A second inherited warning, same source: the CD block's repeat-**count** field does not
produce N passes on hardware. Only infinite repeat (`0xf`) and one-shot are usable.
This port needs only those two, but nobody should rediscover it.

## The problem: one drive, two consumers

`main.c:529`, inside `play_anm`:

```c
disc_play_track(anm[seq].track, 0);
ok = play_animation(anm[seq].filename, anm[seq].offset);
```

`play_animation` (`animation.c:919`) does one `disc_read_file` of the whole animation
file into the emulated 68000 map, then plays from RAM. So the sequence is: issue a
CD-DA play command, then immediately seek the drive to read several hundred kilobytes.
The read wins; the music never starts.

Every intro animation hits this, and the intro is where most of the game's music is.
A backend that merely forwards the two calls would be silent for exactly the content
we can verify.

Reads are discrete and blocking — animations preload, they do not stream — so this is
solvable, but only if the backend owns playback state rather than forwarding calls.

## Architecture

No new seam. No engine change. `disc.h`'s five-function contract is unchanged; two
no-ops become real, and a third function grows a bracket around its existing body.

```
engine (main.c, decode.c)
  |
  |  disc_play_track / disc_stop_track / disc_read_file      <- disc.h, unchanged
  v
disc_srl.cxx          playback state, drive classification, CDC calls
  |            \
  |             `-- cdtoc.c    pure BIOS-TOC arithmetic, host-testable
  v
SRL::Sound::Cdda::PlaySingle, raw CDC_CdPlay / CDC_CdSeek / CDC_GetCurStat
```

## Components

### `cdtoc.{c,h}` — the pure decoder (new)

A sibling of `discfmt.{c,h}`, and deliberately its twin: pure integer arithmetic over a
buffer somebody else fetched, so it compiles for both host and SH-2 and can be unit
tested off-target. `discfmt.c` earned that shape for ISO9660; the BIOS TOC earns it for
the same reason.

The BIOS TOC is 102 longwords:

| Index | Contents |
| --- | --- |
| `0..98` | one entry per CD track 1..99, as `(ctrladr << 24) \| fad`; absent tracks read `0xFFFFFFFF` |
| `99` | first-track record, `(ctrladr << 24) \| (track << 16) \| ...` |
| `100` | last-track record, same shape |
| `101` | lead-out, `(ctrladr << 24) \| fad` |

`ctrladr`'s high nibble is the control field: `0x0f` marks the entry absent, bit 2 set
means data, clear means audio. FAD is in 1/75-second frames.

Interface, all taking `const uint32_t *toc`:

- `cdtoc_is_audio(toc, track)` — `ctrl != 0xf && (ctrl & 0x4) == 0`
- `cdtoc_track_start(toc, track)` — the track's first frame address
- `cdtoc_track_end(toc, track)` — the next track's start, or the lead-out for the last
- `cdtoc_max_audio_track(toc)` — the highest audio track that exists, or 0

Every function returns 0 for an out-of-range track or a TOC that cannot answer, and
callers treat 0 as "unknown", never as a valid address.

### `disc_srl.cxx` — playback state and the drive (changed)

State, file-static alongside the existing `g_discOpened`:

| Name | Meaning |
| --- | --- |
| `g_toc[102]`, `g_tocReady` | the raw BIOS TOC, fetched once by `CDC_TgetToc` |
| `g_maxAudioTrack` | highest real audio track; 0 on a data-only disc |
| `g_musicTrack` | engine index currently requested, −1 for none |
| `g_musicLoop` | whether it was requested looping |
| `g_pauseFad` | frame address captured by the last suspend |
| `g_wasPlaying` | whether the drive confirmed `CDC_ST_PLAY` at that suspend |

`disc_open` — after the existing manifest validation, fetch the TOC and compute
`g_maxAudioTrack`; clear music state. Re-entrant, as the contract already requires.

`disc_play_track(engine_index, loop)`:

1. No-op if `!g_discOpened`, or if `cls.nosound` is set.
2. `cue = discfmt_cue_track_for_music(engine_index)`; a return of 0 (out of range) is a
   no-op. The `+ 2` is not re-derived here — it stays in the one tested function that
   both backends share.
3. **TOC guard**: `cue > g_maxAudioTrack` is a no-op that clears music state. This is
   what makes the default 12 MB data-only disc safe rather than undefined.
4. If the request is identical to what the drive is confirmably doing — same cue track,
   both looping, status `CDC_ST_PLAY` — return without touching the drive. Re-issuing
   `CDC_CdPlay` costs a seek and an audible gap, and "play T looping" when T is already
   looping is a no-op by definition. Any other repeat is honoured as a fresh play. This
   is the one place the Saturn backend deliberately differs from `disc_cue.c`, where
   restarting an already-hooked stream is free.
5. Otherwise `SRL::Sound::Cdda::PlaySingle(cue, loop != 0)` and record state.

`disc_stop_track()` — halt (capture nothing, `CDC_CdSeek` with `CDC_PTYPE_DFL`) and
clear music state. Clearing the state is what stops a later read from resuming it.
Safe before `disc_open`, after `disc_close`, and with nothing playing.

`disc_close()` — clears music state as well as `g_discOpened`, so a `stop`-then-`close`
and a bare `close` end in the same place.

`disc_read_file()` — body unchanged, bracketed by two private helpers.

### The suspend / restore bracket

**Suspend**, before the read. Returns immediately if `g_musicTrack < 0`. Otherwise
`CDC_GetCurStat`, record `g_wasPlaying = (CDC_GET_STC(&stat) == CDC_ST_PLAY)` and
`g_pauseFad = CDC_STAT_FAD(&stat)`, then `CDC_CdSeek` with `CDC_PTYPE_DFL`. The seek is
what actually silences the output.

**Restore**, after the read. Returns immediately if `g_musicTrack < 0`. Otherwise
classify by where the saved frame sits in the track's `[start, end)`:

| Saved FAD | Meaning | Action |
| --- | --- | --- |
| inside the range, and `g_wasPlaying` | genuinely interrupted mid-track | resume or restart, per the loop rule below |
| below `start` | the play was issued but never began — the animation case | `PlaySingle(cue, loop)` from the start |
| at or past `end` | the track ran to completion on its own | clear music state, restore nothing |
| TOC cannot answer (`start == end == 0`) | unknown | `PlaySingle(cue, loop)` from the start |

Classification is by frame address, not by `CDC_STAT_TNO`. The FAD-within-track check
is the one `zaturn` validated; `tno` while stopped is unproven here and stays unused.

The third row earns its place: without it, a one-shot track that finished minutes ago
would be restarted by the next unrelated room load. The last row follows `zaturn`'s
rule that restarting is a far better failure than silence.

**The loop rule.** A resume is a `CDC_CdPlay` over a frame range, and a frame range
plays as a one-shot:

- `loop == 0` — resume the remainder: `STYPE=FAD, SFAD=g_pauseFad, ETYPE=FAD,
  EFAS=end − g_pauseFad, PMODE=CDC_PM_DFL`. One-shot semantics preserved exactly. This
  is the animation case, where mid-track continuity matters most.
- `loop == 1` — restart the track from the top, looping. Resuming would silently drop
  the repeat: the track would finish its remainder and then stop forever, which is a
  worse failure than a restart. `zaturn` absorbed this with a `music_tick` poll; there
  is no tick at this seam and inventing one is out of scope.

In practice reads cluster at room and animation loads, which change the music anyway,
so the restart is rarely audible as a restart.

### Volume

Untouched. `SRL::Sound::Hardware::Initialize()` — already called at boot by
`srl_core.hpp:108`, since `saturn/makefile` sets `SRL_USE_SGL_SOUND_DRIVER = 1` — sets
`SND_SetCdDaLev(7, 7)`, the maximum. The engine has no music-volume concept to map, so
this sub-project issues no `SetVolume` call at all.

### `cls.nosound`

Honoured, matching `disc_cue.c:527`. On Saturn it is always 0 today, because `main.c`
drops option parsing under `HOTA_SATURN`, so it is currently a constant-false branch.
It goes in anyway: `disc.h`'s stated contract then holds literally on both backends,
and a future Saturn sound-off option has somewhere to land.

### `disc.h` — the seam banner

Rewritten. It currently describes `Mix_HookMusic` as though it were the implementation
and tells a future Saturn implementer that playback is a clean drop-in. It is not: one
drive means reads and playback contend, and the backend must own playback state to
survive it. That is the non-obvious thing the next reader needs stated at the seam.

## Data and control flow

The intro, per animation:

```
play_anm
  disc_play_track(31, loop=0)
    cue = 33, <= g_maxAudioTrack, PlaySingle(33, false)     drive begins seeking
  play_animation("INTRO1.BIN")
    disc_read_file
      suspend:  GetCurStat -> not yet CDC_ST_PLAY, fad below track 33's start
                CdSeek(DFL)
      <whole-file read, ~10 KB HWRAM work buffer, unchanged>
      restore:  fad < start  ->  PlaySingle(33, false) from the start
    decode frames from RAM, no further disc access          music plays throughout
  ... repeat for INTRO2..4 with tracks 32..34 ...
disc_stop_track()                                            CdSeek(DFL), state cleared
```

A room's looping music, interrupted by a later load:

```
decode.c 0x1b        disc_play_track(i, loop=1)  ->  PlaySingle(cue, true)
... room runs, no disc access ...
room change          disc_read_file
                       suspend: CDC_ST_PLAY, fad inside track    -> g_wasPlaying = 1
                       restore: loop == 1  ->  PlaySingle(cue, true) from the top
```

## Build and test

`HOTA_AUDIO` stays `none`. Routine builds remain a 12 MB data-only disc, and the TOC
guard makes them silently music-free. Music verification is an explicit
`HOTA_AUDIO=full ./compile.bat` from `saturn/`, producing the ~425 MB disc with all 41
CD-DA tracks laid from `saturn/cd/music/*.raw` in the order given by
`saturn/cd/music/tracklist` (present, 41 entries, header says do not resort).

The only `makefile` change is a comment: the `HOTA_AUDIO` block currently says the
tracks are laid "for a build that never plays them", which stops being true.

Verification, in ascending cost:

1. **`saturn/tests/test_cdtoc.c`**, a sibling of `test_discfmt.c`, over a synthetic
   102-longword TOC: audio versus data classification, absent entries (`0xFFFFFFFF`),
   start and end frame addresses, lead-out standing in as the last track's end, max
   audio track, out-of-range indices. Plus one test pinned to the SRL bug's shape —
   that track *t* decodes from longword *t − 1* — so the trap cannot silently return.
2. **Host build unaffected.** `disc_cue.c` is untouched; `make -C saturn/src` and
   `sh saturn/tests/run_tests.sh` still pass. `rm -f saturn/src/*.o` between the host
   and Saturn builds — the standing object-collision trap.
3. **Saturn link and map check.** This adds 408 bytes of static TOC plus code; confirm
   against the map that HWRAM headroom has not moved meaningfully.
4. **Data-only disc regression**, `HOTA_AUDIO=none`, the default. Boots, intro renders
   exactly as today, no music, no hang, no CD-block error. This is what proves the TOC
   guard, and it is the disc every future build produces.
5. **Full disc, on the emulator, run by Suinevere.** Music under each of the four intro
   animations; music surviving the whole-file load that immediately precedes each;
   no restart stutter; silence after `play_anm` ends.

Diagnostics use `printf`, never `fprintf` — `fprintf` renders nothing on Saturn and the
cause is still unknown. At most three: TOC track count at open, a play refused by the
guard, and a restore classified as finished or never-started.

## Risks and mitigations

**Emulator fidelity.** Every hardware finding this design rests on came from `zaturn`
on real hardware. Whether Mednafen models data-read-versus-CDDA contention,
`CDC_TgetToc` and post-seek status the same way a real CD block does is unknown.
Mitigation: none available here — but "works in Mednafen" will not be written up as
"works", and the design follows hardware-derived behaviour wherever the two could
differ.

**A stale FAD read immediately after `PlaySingle`** could land inside the track and
trigger a resume from a spurious position. Mitigation: the inside-range branch also
requires status `CDC_ST_PLAY`; anything else falls through to below-start and restarts.

**Resume is sector-granular**, so a resumed remainder may begin with a small click or
gap. Accepted; the alternative is restarting every interrupted track.

**A missing or resorted `tracklist`** assigns every track the wrong number and plays
the wrong song for every cue, with no error. Mitigation: the full-disc build step
checks it exists and has 41 entries before trusting any listening test.

**Coverage stops at the intro.** Rooms and gameplay have never been exercised on
Saturn at all, so in-game looping tracks, the loop-restart rule and mid-room loads are
reasoned about here but will not be proven by this sub-project. Stated rather than
implied.

## Deferred / stubbed

- **Sound effects.** `play_sample` and `sound_flush_cache` stay no-ops in
  `sound_srl.cxx`. SFX over `SRL::Sound::Pcm` is its own sub-project: the samples live
  inside the 512 KB emulated 68000 map in LWRAM as 8 kHz 8-bit sign-magnitude, SRL's
  concrete `IPcmFile` subclasses all load from a `Cd::File`, and the caching and memory
  budget questions are entirely separate from anything here.
- **Music volume, ducking, fades.** No engine concept to drive them.
- **A per-frame music tick.** Would be needed to restore looping after a resumed
  remainder; the loop rule avoids needing one.

## Out of scope

- `sound.c` remains filtered out of the SH-2 build.
- The host backend `disc_cue.c` is not touched.
- The `fprintf` silent-failure bug is not investigated here; this sub-project only
  avoids it.

## Acceptance

1. `sh saturn/tests/run_tests.sh` passes, including the new `test_cdtoc` cases.
2. `make -C saturn/src` still builds. `disc_cue.c` is byte-identical, so the host's
   music path carries no regression risk and is not re-tested by hand.
3. `./compile.bat` with the default `HOTA_AUDIO=none` produces a disc that boots and
   renders the intro exactly as `3051d5c` does, with no music and no CD-block error.
4. `HOTA_AUDIO=full ./compile.bat` produces a disc on which all four intro animations
   play with their music, the music survives the load preceding each animation, and
   playback stops when the intro ends — confirmed by Suinevere on the emulator.
5. No `SRL::Cd::TableOfContents`, `Cdda::Resume` or `Cdda::StopPause` call exists in
   the port.

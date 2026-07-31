# Heart of The Alien → Sega Saturn — bin/cue Disc Backend Design Spec

**Date:** 2026-07-31
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Give the engine one portable way to get bytes and music off the disc, and implement
it on the host against the real Sega CD bin/cue set.

Today the engine reads its 19 data blobs out of a monolithic `.iso` at hardcoded byte
offsets and plays its music as loose `.mp3`/`.ogg` files. Neither exists on a Saturn.
This sub-project replaces both with a single seam — `src/disc.h` — implemented once
for the host on bin/cue, and later, unchanged above the seam, for Saturn on
`SRL::Cd::File` and `SRL::Sound::Cdda`.

This is the first sub-project of the port and it is deliberately the data layer:
nothing else can be brought up until the game can load.

## The finding that drives every decision

**The hardcoded offset table is a cached copy of the disc's own ISO9660 directory.**

`src/cd_iso.c:35-56` carries 19 entries of `{filename, offset, size}`. The data track
of the bin/cue set in `cd/` carries a real, intact ISO9660 filesystem — Primary Volume
Descriptor at sector 16, root directory at LBA 20, length 4096. Every one of the 19
entries matches a directory record **exactly**, LBA and size, with no drift:

```
offset == LBA * 2048        for all 19, every offset sector-aligned
```

So the table never encoded knowledge the disc lacks. It encoded a filesystem walk that
someone did once by hand. Reading the directory at runtime replaces the table with the
thing the table was copied from, and gets name-based lookup — which is exactly the
shape `SRL::Cd::File` offers on Saturn — for free.

The disc also holds ~30 files the engine never reads (`*.MAC`, `*.MAT`, `*.SND`,
`MAKE2EB.BIN`, `MAKE2IB.BIN`, `MAKE2S.BIN`, `MAKE1IB.BIN`, `TEST3.BIN`). They are
Sega CD assets the Redux engine reimplemented in native code. Only the 19 are needed.

## Decision: one seam, a pure format layer, two backends

`src/disc.h` is the only disc-facing interface the engine calls:

```c
int  disc_open(const char *cue_path);
int  disc_read_file(const char *name, void *out, int max_size);
void disc_play_track(int track, int loop);
void disc_stop_track(void);
void disc_close(void);
```

`src/cd_iso.{c,h}` and `src/music.{c,h}` are deleted. This mirrors Another-Saturn's
`struct System`: one seam, one file to replace per platform, engine untouched above it.

**The refinement that matters: the format logic is split out from the I/O.**
`src/discfmt.{h,c}` holds the cue parser, the ISO9660 directory parser, the sector
arithmetic and the track-number mapping as **pure functions over buffers, with no file
handles and no SDL**. `src/host/disc_cue.c` holds the `FILE *`s and implements
`disc.h` on top of it.

That split buys three things, and the third is the reason it is not over-engineering:

1. Every piece of arithmetic that can be silently, plausibly wrong — sector
   translation, the ISO9660 record walk, the music off-by-one — becomes host-testable
   with plain `gcc`, exactly as `scsp_voice.{h,cxx}` did for Another-Saturn's audio.
2. `tools/extract_disc.c` links `discfmt.c` verbatim. The extractor and the runtime
   agree about the disc layout by construction, not by two parallel implementations
   drifting.
3. `discfmt.c` is the part the Saturn backend keeps. `SRL::Cd::File` does its own
   ISO9660 lookup, so the Saturn build needs the *mapping* logic and none of the
   parsing — but the boundary is already drawn.

## Architecture

| File | Language | Responsibility |
|------|----------|----------------|
| `src/disc.h` | C | New. **The seam.** Five functions. No SDL, no stdio, no SRL, no SRL-visible types. |
| `src/discfmt.h` | C | New. Pure disc-format logic: cue parse, ISO9660 walk, sector maths, track mapping. Depends on nothing but `<stdint.h>`/`<stddef.h>`. |
| `src/discfmt.c` | C | New. Its implementation. Host-testable; linked by the engine, the extractor and the test runner alike. |
| `src/host/disc_cue.c` | C | New. Host backend. Owns the `FILE *`s, the `Mix_HookMusic` streamer, and the validation pass. The only file that does disc I/O. |
| `tools/extract_disc.c` | C | New. Extracts the 19 blobs and 41 CD-DA tracks into `saturn/cd/`, and writes the `tracklist`. Links `discfmt.c`. |
| `saturn/tests/test_discfmt.c` | C | New. Host unit tests for every pure function above. |
| `src/cd_iso.c`, `src/cd_iso.h` | — | **Deleted.** Replaced by `disc.h` + `disc_cue.c`. |
| `src/music.c`, `src/music.h` | — | **Deleted.** Replaced by `disc_play_track`/`disc_stop_track`. |
| `src/client.h` | C | Modified. `use_iso` and `iso_prefix` removed. |
| `src/main.c` | C | Modified. `--iso`/`--iso-prefix` dropped, `disc_open`/`disc_close` wired in, `music_*` calls renamed. |
| `src/animation.c`, `src/game2bin.c` | C | Modified. `read_file(...)` → `disc_read_file(..., max_size)`; three call sites total. |
| `src/decode.c` | C | Modified. `play_music_track`/`stop_music` → `disc_play_track`/`disc_stop_track`. Mechanical. |
| `src/Makefile` | make | Modified. New objects, new source dir. |
| `saturn/makefile` | make | Modified. `HOTA_AUDIO` knob, `CD_NAME`. |

Note `src/sound.c` is **not** in this table. It uses SDL_mixer for *sound effects* and
keeps doing so; see "What 'drop SDL_mixer' does and does not mean" below.

## Components

### `discfmt` — the pure layer

Everything here takes a buffer and returns a value. Nothing here opens a file.

**Cue parsing.** `discfmt_cue_parse(text, len, DiscCue *out)` fills a track table of
`{ number, mode, filename, pregap }`. The set in `cd/` is the multi-file form — one
`FILE "…(Track NN).bin" BINARY` per `TRACK`, 42 of them. Single-file cues (one `.bin`,
tracks distinguished by `INDEX` MSF) are common in the wild and the parser must
recognise the shape and **reject it with a clear message** rather than
mis-parse it; supporting it is deferred, not silently handled.

**ISO9660 directory walk.** Two functions, mirroring the two sectors that matter:

- `discfmt_iso_root(pvd_user, uint32_t *lba, uint32_t *len)` — reads the root directory
  record out of the PVD's 2048-byte user area at byte 156.
- `discfmt_iso_find(dir, dir_len, name, uint32_t *lba, uint32_t *size)` — walks the
  directory records, matching names.

Name matching is its own trap and its own function. ISO9660 stores `END1.BIN;1` —
uppercase, with a `;1` version suffix — so a plain `strcmp` against `"END1.BIN"` finds
nothing. `discfmt_iso_name_eq` compares up to the `;`, case-insensitively. It also has
to handle the record-length-zero padding that ends each 2048-byte block of the
directory: a zero length byte means "skip to the next sector boundary", not
"end of directory".

**Sector translation.** One line of arithmetic, one function, because it is the single
most load-bearing expression in the sub-project:

```c
raw_offset = lba * 2352 + 16      /* MODE1/2352: 12 sync + 4 header, then 2048 user */
```

`discfmt_mode1_user_offset(lba)`. A read of `size` bytes from `lba` is
`(size + 2047) / 2048` sectors, each contributing 2048 bytes from its own raw offset —
**not one contiguous `fread`.** The 304 bytes of sync/header/EDC/ECC between every
2048 bytes of payload is exactly what makes a naive port of `read_file_internal`
produce data that looks almost right and is corrupt every 2 KB.

### The validation manifest

The 19-entry table survives, demoted from lookup mechanism to **integrity check**.
`disc_open` walks the real ISO9660 directory, then confirms the 19 known blobs are
present as expected.

This is not belt-and-braces. `disc_read_file`'s three callers pass raw pointers into
the emulated 68000 address space with **no bounds checking whatsoever**:

- `src/main.c:117` — `get_memory_ptr(0xf900)`, then reads a `ROOMS*.BIN`
- `src/animation.c:871` — `get_memory_ptr(0x809a - fileoffset)`, then reads an animation
- `src/game2bin.c:56` — a 409,600-byte `static char` array

Nothing anywhere compares the size on the disc against the space at the destination. A
truncated, byte-swapped or wrong-region dump therefore does not fail — it scribbles
past a buffer into unrelated VM state and manifests as a corrupted room three minutes
later. Two defences, both cheap:

1. **Startup validation.** Fail at `disc_open` with a message naming the file, not at
   some indeterminate later point.
2. **`max_size` on every read.** The parameter added to `disc_read_file` is the reason
   the signature changed from `read_file`; each of the three call sites passes the
   space it actually has.

**Resolved ambiguity — what a mismatch means.** Size mismatch or a missing file is
**fatal**: size is what protects the buffers. An LBA mismatch is a **warning only**,
naming the likely cause. LBA is a fingerprint of this particular dump, not a
correctness property — a differently-mastered but perfectly playable dump would shift
every LBA while every size stayed identical, and hard-failing on that would reject a
good disc for no reason.

### The music track mapping — and the off-by-one that is easy to get wrong

The engine numbers music from zero. The disc numbers tracks from one, with track 1
being the data track. The mapping is therefore:

```
cue_track = engine_index + 2
```

**Engine index 0 → TRACK 02. Engine index 40 → TRACK 42.**

The arithmetic that makes this the only possible answer: the disc has 41 audio tracks,
numbered `TRACK 02` through `TRACK 42`. The engine's index range is 0..40 — 41 values
(`anm_files` in `src/main.c:55-67` uses 31..40; `src/decode.c:1846-1862` names 35..40
explicitly; bytecode opcode `0x1a` supplies the rest). 41 indices onto 41 tracks, in
order, is a perfect fit at `+2` and nothing else.

**`+1` is the trap, and it is a plausible-looking mistake.** The deleted `music.c:63`
built its filename as `"%02d", track + 1` — so engine 0 was file `01`. But that
numbering was over the *mp3 rip*, which numbered the 41 **audio** tracks 01..41.
Audio track 1 is disc track 2. Reading the old `+1` as a cue-track formula puts engine
index 0 on **the data track**, which on real hardware is a burst of noise or a hang,
and in an emulator may silently play nothing at all.

This lives in `discfmt_cue_track_for_music(engine_index)` — one named, tested function,
so the constant exists in exactly one place.

The bytecode layer above it (`src/decode.c:1882-1901`, opcode `0x1a`) is unchanged and
stays in engine indices:

| `imm8` | Meaning | Engine index |
|---|---|---|
| `0` | stop music | — |
| `1..99` | play looping | `imm8 - 1` |
| `>= 100` | play once | `imm8 - 101` |

### Host backend — `disc_cue.c`

`disc_open(cue_path)` reads the cue, keeps the data track's `FILE *` open for the
process lifetime (the engine loads blobs throughout play, and re-opening per read
serves nothing), records the audio tracks' paths, then runs the validation pass.

`disc_read_file` resolves the name through the cached ISO9660 directory, checks
`size <= max_size`, and copies sector by sector.

### Music on the host — `Mix_HookMusic`, not `Mix_LoadMUS`

The audio tracks are raw CD-DA: 44,100 Hz, 16-bit signed, stereo, little-endian, no
header, every file a whole multiple of 2352 bytes. SDL_mixer cannot `Mix_LoadMUS` that
— there is no container for it to recognise.

**Use `Mix_HookMusic`.** It installs a callback that feeds the music stream directly,
and `main.c:149` already opens the device as `Mix_OpenAudio(44100, AUDIO_S16, 2, 4096)`
— which is *bit-for-bit the format on the disc*. The callback is a buffered `fread`
and a `memcpy`. No decode, no resample, no format conversion.

Three reasons this beats the alternatives:

- `Mix_LoadWAV_RW` over a synthesised header would pull a whole track into RAM;
  `Track 03.bin` alone is 29 MB.
- Opening a second raw `SDL_AudioDevice` would fight `sound.c` for the output device.
- It is the closest host analogue to what Saturn actually does — hardware streams a
  track off the disc while the CPU does something else — so the host behaviour being
  ported *from* matches the behaviour being ported *to*.

Two hazards to handle explicitly:

- **The callback runs on the audio thread.** Swapping tracks must go through
  `Mix_HookMusic(NULL, NULL)` first, which SDL_mixer serialises against its own mixer,
  before the old `FILE *` is closed. Closing it under a running callback is a
  use-after-free with a very unpleasant signature.
- **`AUDIO_S16` is `AUDIO_S16LSB`** — native little-endian. That matches CD-DA on x86
  and would need a byte swap on a big-endian host. The host build is
  Windows/x86; note it and move on.

Looping: `loop != 0` seeks back to the start of the track file at EOF. `loop == 0`
stops and unhooks. This matches the old `Mix_PlayMusic(music, loop)` semantics closely
enough that no caller changes.

### What "drop SDL_mixer" does and does not mean

**It means:** stop using SDL_mixer to *decode and play music files*. `Mix_LoadMUS`,
`Mix_PlayMusic`, `Mix_FreeMusic` go away with `music.c`.

**It does not mean:** removing SDL_mixer. `src/sound.c` uses it for **sound effects** —
`Mix_Chunk`, `Mix_PlayChannelTimed`, `Mix_HaltChannel`, a 256-entry sample cache — and
that is a different subsystem on a different sub-project's schedule.
`-lSDL2_mixer` stays in `src/Makefile`, `Mix_OpenAudio` stays in `main.c:139-160`, and
`Mix_HookMusic` needs the device open anyway.

Getting this backwards deletes every sound effect in the game while looking like it
only touched music, so it is called out here rather than left to inference.

### The extractor — `tools/extract_disc.c`

```
extract_disc <path-to.cue> <saturn-cd-dir>
```

Writes:

| Output | Contents |
|---|---|
| `saturn/cd/data/*.BIN` | The 19 engine blobs, 7.14 MB total, read through the same sector path as the runtime. |
| `saturn/cd/music/trackNN.wav` | The 41 CD-DA tracks, 414 MB, each a 44-byte canonical RIFF/WAVE header prepended to the untouched raw track data. |
| `saturn/cd/music/tracklist` | The 41 filenames in cue order, one per line. |

**Why `.wav` and not raw.** `shared.mk:409` auto-discovers only
`*.mp3|wav|ogg|flac|aac|m4a|wma`; a bare `.raw` is invisible to it. Wrapping the
existing bytes in a 44-byte header costs nothing and makes them a first-class input.
sox then converts back to raw, which is a byte-identical round trip because the source
already *is* 44.1/16/stereo.

**The `tracklist` is not optional.** Without it `shared.mk:409` falls back to `find`,
whose output order is not defined. Music would then be laid onto the disc in
arbitrary order and every cue would point at the wrong song — a failure that is
obvious to a listener and invisible to a build log. `NN` is zero-padded so that even
the fallback path sorts correctly, but the `tracklist` is what actually guarantees it.

**`NN` is the cue track number, not the engine index** — `track02.wav` … `track42.wav`.
Naming the files after the thing they will become on the target disc keeps the one
off-by-one in this sub-project confined to `discfmt_cue_track_for_music`.

### The audio build knob

`HOTA_AUDIO = none | full`, default `none`. With 41 tracks the disc image is ~425 MB
and every `compile.bat` re-lays 41 audio tracks; while the video and input sub-projects
are being brought up, that build cost dominates and buys nothing.

Implemented without patching the SDK, by overriding `MUSIC_DIR` **after** the
`include $(SDK_ROOT)/shared.mk`:

```make
HOTA_AUDIO ?= none
ifneq ($(HOTA_AUDIO),full)
MUSIC_DIR = ./cd/music-off
endif
```

`shared.mk:146` sets `MUSIC_DIR = ./cd/music` with `=`, and every use is inside a
recipe, so a later assignment wins. `saturn/cd/music-off/tracklist` is committed,
empty but for a comment, and `shared.mk` reports "No audio files found" and lays a
data-only disc. This is the same "append after the include" idiom the makefile already
documents for `LIBS`.

## Data and control flow

```
main()
 └─ disc_open("cd/….cue")
     ├─ discfmt_cue_parse            -> 42 tracks: 1 data, 41 audio
     ├─ open data track FILE *
     ├─ read sector 16      -> discfmt_iso_root      -> root LBA 20, len 4096
     ├─ read LBA 20..21     -> cached directory records
     └─ validate 19 manifest entries (size fatal, LBA warn)

load_room / play_animation / game2bin_init
 └─ disc_read_file(name, ptr, max)
     ├─ discfmt_iso_find(dir, name)  -> lba, size
     ├─ size > max ? -> fail loudly
     └─ per sector: fseek(lba*2352 + 16), fread(2048)

decode.c op 0x1a / play_anm
 └─ disc_play_track(engine_index, loop)
     ├─ discfmt_cue_track_for_music  -> engine_index + 2
     ├─ Mix_HookMusic(NULL,NULL); close old FILE *
     └─ open trackNN .bin; Mix_HookMusic(stream_cb, state)
```

## Memory

Host: unchanged. Blobs land in the same buffers they always did.

Saturn (designed-for, not built here) — and this is the section worth reading twice:

| Where | What | Size |
|---|---|---|
| Static BSS | `src/vm.c:26` `static unsigned char memory[512*1024*2]` | **1,048,576 B** |
| Static BSS | `src/game2bin.c:27` `static char game2bin[409600]` | **409,600 B** |
| Transient | Largest single blob (`MAKE2MB.BIN`) read into the above | 436,224 B |
| Disc | 19 blobs | 7.14 MB |
| Disc | 41 CD-DA tracks (`HOTA_AUDIO = full`) | 414 MB / ~41 min |

**The static arrays alone are ~1.4 MB, and the Saturn has 1 MB of High Work RAM.**
They are `static`, so they are BSS — they never touch SRL's TLSF arena and cannot be
made to by tuning an allocator. This does not block the present sub-project, which
changes neither array, but it is the largest known obstacle to the port as a whole and
is recorded here so it is not discovered late. Options when it is faced: split the
68000 image across HWRAM and LWRAM, move `game2bin` to a demand-read off the disc
(it is already accessed only through `copy_from_game2bin`, which is a helpful shape),
or reduce the emulated map to what the game actually addresses.

The CD-DA total needs no Saturn RAM at all — hardware streams it — which is precisely
why `SRL::Sound::Cdda` is the right target and why music is cheap on this platform
even though it is 98% of the disc.

## Deferred / stubbed

- **The Saturn `disc` backend itself.** Designed for, not built. It is a sibling of
  `disc_cue.c` over `SRL::Cd::File` and `SRL::Sound::Cdda::Play(from, to, loop)`, and
  it needs no engine change above the seam.
- **Single-file cue sheets.** Detected and rejected with a clear message, not
  supported.
- **Reading the ~30 unused Sega CD assets.** They stay on the source disc.
- **CD-DA volume and pan** (`Cdda::SetVolume`, `SetPan`). Saturn-side, later.
- **Track pregap and `INDEX 00` handling.** Every audio track in this set has the same
  `INDEX 00 00:00:00` / `INDEX 01 00:02:00` shape, and `shared.mk` emits its own
  `PREGAP 00:02:00` on track 2. Parsed, recorded, otherwise unused.

## Build and test

**Host engine:** `cd src && make` → `alien`. Runs as
`./alien "cd/Heart of the Alien … (RE).cue"`.

**Host unit tests:** `sh saturn/tests/run_tests.sh`, plain `gcc`, no SDL and no Saturn
toolchain. Following the `test_scsp_voice.cxx` pattern already in `saturn/tests/`.
Covering, at minimum:

- `discfmt_mode1_user_offset` — LBA 0, 16, 20, 2593; the multi-sector span calculation
  for a size that is and is not a multiple of 2048.
- `discfmt_iso_name_eq` — `"END1.BIN;1"` vs `"END1.BIN"`, case folding, a non-match
  that shares a prefix (`"ROOMS1.BIN"` vs `"ROOMS11.BIN"`).
- `discfmt_iso_find` over a synthesised directory block, **including a record-length-0
  pad forcing a jump to the next 2048-byte boundary** — the case a naive walk turns
  into an infinite loop.
- `discfmt_cue_track_for_music` — `0 → 2`, `40 → 42`, and an explicit assertion that
  it never returns 1, since 1 is the data track.
- `discfmt_cue_parse` over the real 168-line cue: 42 tracks, track 1 `MODE1/2352`,
  tracks 2–42 `AUDIO`, filenames intact including spaces and parentheses.
- Rejection of a synthesised single-file cue.

**Against the real disc** (skipped with a clear message when `cd/` is absent, since it
is not committed): open the set, walk the directory, confirm all 19 manifest entries
resolve, and checksum one blob end to end.

**Saturn build:** `cd saturn && ./compile.bat debug` must stay clean throughout;
nothing in this sub-project adds a Saturn source file yet.

**Emulator:** Suinevere runs Mednafen. Build the disc, report what changed and what to
look for, and ask what they see. Never launch it from a tool call.

## Risks and mitigations

- **The music off-by-one.** `+1` versus `+2` is a one-character error that silently
  plays the wrong song for the whole game, or aims track 0 at the data track. Confined
  to one named function with a test that asserts it never returns 1.
- **Sector translation applied at the wrong granularity.** Treating a multi-sector read
  as one contiguous `fread` yields data that is correct for its first 2048 bytes and
  garbage thereafter — which decodes far enough to look like a *decoder* bug. The
  per-sector loop is unit-tested independently of any file.
- **Closing a track file under the audio callback.** A use-after-free on the audio
  thread, timing-dependent and rare. Always `Mix_HookMusic(NULL, NULL)` before
  `fclose`, in that order, with no path that skips it.
- **Deleting SDL_mixer along with music.** Would remove every sound effect while
  appearing to touch only music. `src/sound.c` is explicitly out of scope and
  `-lSDL2_mixer` explicitly stays.
- **A different dump.** LBAs shift, sizes do not. Hence size-fatal / LBA-warning rather
  than failing a good disc.
- **425 MB build times.** `HOTA_AUDIO` defaults to `none`.
- **Disk cost of extraction.** 414 MB of `.wav` plus another 414 MB of sox `.raw` when
  built with `HOTA_AUDIO = full`. Both are already covered by
  `saturn/cd/music/.gitignore`; `tracklist` is tracked and contains no game data.
- **`saturn/` is Another-Saturn's scaffolding.** `saturn/makefile` still says
  `CD_NAME = Another World (USA)` and carries `-DBYPASS_PROTECTION` and the LIBPCM
  block; `saturn/tests/` still holds that project's SCSP tests. Only `CD_NAME` and the
  new audio knob are in scope here. The rest is noted, not touched — but it is stale
  and will mislead someone if it is left indefinitely.

## Out of scope

Each becomes its own spec, in roughly this order:

1. **The Saturn `disc` backend** — `SRL::Cd::File` + `SRL::Sound::Cdda`.
2. **The libc / SRL shims** — the five include-path and linkage traps from
   Another-Saturn's `srl-libc-shadowing` notes apply here essentially unchanged.
3. **The memory map** — the 1.4 MB of static arrays above, against 1 MB of HWRAM. The
   real blocker.
4. **Video** — `render.c`, `screen.c`, `scale2x/3x` onto VDP2.
5. **Input** — SDL keyboard/joystick onto `SRL::Input::Digital`.
6. **Sound effects** — `sound.c` off SDL_mixer and onto the SCSP.
7. **Saves**, and the stale Another-Saturn scaffolding in `saturn/`.

## Acceptance

1. `sh saturn/tests/run_tests.sh` passes, including the real-disc checks.
2. The host build runs the game from the bin/cue set with no `.iso` and no `.mp3`
   present anywhere, through the intro and into the first room.
3. Music plays, and the track heard for a given engine index is the same one the old
   mp3 path played for that index.
4. `tools/extract_disc` populates `saturn/cd/data/` with 19 files whose sizes match the
   manifest exactly, and `saturn/cd/music/` with 41 `.wav`s plus a `tracklist` naming
   all 41 in cue order. (`shared.mk` strips `#` comments and blank lines from the
   tracklist, so a leading comment line is safe.)
5. `cd saturn && ./compile.bat debug` builds a data-only disc by default, and a disc
   with 42 tracks under `HOTA_AUDIO=full`.
6. `grep -rn "cd_iso\|use_iso\|iso_prefix\|Mix_LoadMUS\|Mix_PlayMusic" src/` returns
   nothing.

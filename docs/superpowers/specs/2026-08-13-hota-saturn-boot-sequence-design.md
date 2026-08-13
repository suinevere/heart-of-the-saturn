# Heart of The Alien → Sega Saturn — Boot Sequence Design Spec

**Date:** 2026-08-13
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Give the disc a front end: the Sega CD opening screens, then the game-select menu, then
the game.

Today `main()` runs `initialize()` and drops straight into `run()`. A player sees a black
screen, a two-second load, and gameplay. The original opens on legal text, the Virgin
Interactive logo, the *HEART OF THE ALIEN* title card, and a menu offering *OUT OF THIS
WORLD* and *HEART OF THE ALIEN* over the city backdrop.

Done means: booting the disc plays the four opening screens with cue track 03 under them,
lands on the menu, lets the player move a cursor between the two entries and start Heart
of the Alien, and replays the opening as an attract loop when the menu is left alone.

## Scope

**The boot sequence between `initialize()` and `run()`, and the tool that produces its
art.** Nothing inside the game loop changes; nothing in `video_srl.cxx`, `sound_srl.cxx`
or the engine's renderer changes.

Explicitly not in scope, each argued in [Out of scope](#out-of-scope): running *Out of
This World*, a save-game menu, an options screen, region or language selection, and any
change to the host (SDL) build beyond the stubs needed to keep it linking and its tests
passing.

## What the captures actually are

Two Kega Fusion captures were supplied in `tools/assets/avi/`, both KGV1, 640×480, 60 fps,
with a `pcm_s16le` 44.1 kHz stereo track. **Every measurement below is from the files, not
from assumption**, and several of them overturned the design that was drafted before they
were taken.

### The opening is four still images, not a movie

`Heart of the Saturn.avi` is 1237 frames over 20.617 s. Hashing every frame gives **six
distinct images**:

| # | frames | held | content | colours (320×224) |
|---|---|---|---|---|
| 0 | 1–6 | 0.10 s | black | 1 |
| 1 | 7–312 | 5.10 s | legal / trademark text | 2 |
| 2 | 313–618 | 5.10 s | Virgin Interactive logo | 8 |
| 3 | 619–925 | 5.12 s | the same logo, minus 3 rows of its frame | 8 |
| 4 | 926–1233 | 5.13 s | *HEART OF THE ALIEN* title card | 21 |
| 5 | 1234–1237 | 0.07 s | the menu — the capture ran past the end | 16 |

There is no motion anywhere in it. Screens 2 and 3 are the same picture to within **570
pixels of 71,680** — they differ only in rows `y26–29`, columns `x76–243`, and are
identical everywhere else. The first hold carries three rows of white (`#e7e7e7`) along the
top of the Virgin logo's frame; the second has black there.

An earlier draft of this spec called that difference a "pure 3 px vertical shift", inferred
from the two ink bounding boxes `y26–214` and `y29–214`. That inference was wrong, and the
shared bottom edge should have given it away: a translation moves both edges. Drawing the
second hold 3 px lower would displace the whole logo — a far larger error than the 570
pixels it was meant to reproduce. The low PSNR between the two (22.55 dB) is consistent
with a few hundred pixels differing by a large amount and is not evidence of displacement.

Both holds therefore draw **one texture**, cut from the first, and the logo sits unbroken
for 10.2 s. The two state-machine slots are kept rather than collapsed so that the four
stills stay uniform and the screen index remains `phase_ms / BOOT_STILL_MS`.

Screen 0 gets no state in the design. Six frames of black is 100 ms, less time than the
first still takes to reach the screen, and a state for it would only be observable as a
missing frame if it were left out.

Screen 5 is tabulated at 16 colours where the menu capture measures 15. They are different
recordings of the same screen, and only the menu capture is used as a source — see [The
menu is two states over one 15-colour
palette](#the-menu-is-two-states-over-one-15-colour-palette). Neither number reaches 17, so
the choice of `Paletted16` holds whichever is authoritative.

**This removes Cinepak from the design entirely.** No `SRL::CinepakPlayer`, no 200 KB
LWRAM ring buffer, no 128 KB PCM reservation at `0x25a20000`, no sharing the SCSP with the
SGL driver, and none of the bitstream invariants (even-alignment of every chunk, strip,
frame and sample offset; `-skip_empty_cb 1`; a pinned strip count) that cost the
Another-Saturn port eight hardware builds. Nothing streams, so nothing contends with the
drive, which is also why CD-DA is free to play throughout.

### Both captures are digitally silent

`volumedetect` reports `mean_volume: -91.0 dB` and `max_volume: -91.0 dB` over all
1,818,390 samples of the opening and all samples of the menu capture. Kega recorded video
only. The soundtrack is therefore **chosen, not extracted** — see [Audio](#audio).

### Geometry

640×480 is Kega's 2× scale of a 320×240 frame, and the 2:1 box reduction back to 320×240
is exact: every colour in the reduced active area is a multiple of `0x21`, which is the
Mega Drive's 3-bits-per-channel ladder (`00 21 42 63 84 a5 c6 e7`). No scaling noise
survives into the assets.

The Sega CD's 224-line display sits centred in the 240-line capture, so the active area is
**rows 8–231**, and every ink bounding box measured (`y24–216` at the widest) falls inside
it. A 320×224 sprite maps 1:1 onto this project's NTSC display, where `video_srl.cxx`
already centres its 304×192 framebuffer with `OFFSET_Y = (224 − 192) / 2`.

### The menu is two states over one 15-colour palette

`Heart of the Alien - Main Menu.avi` is 185 frames over 3.083 s and holds **exactly two
distinct images**: frames 1–106 with *OUT OF THIS WORLD* lit red and *HEART OF THE ALIEN*
dark, and frames 107–185 with the reverse.

Both states draw from **15 colours** in the active area, and their union is also 15 — the
highlight swap re-uses the palette rather than extending it. One 16-entry palette covers
the entire menu in both states, so it is a `Paletted16` 4bpp texture.

Diffing the two states, the changed rows span `y46–119` in 224-space, with interior gaps of
6 rows (after `y72`) and 8 rows (after `y78`). Splitting at the largest gap yields the two
logo bands:

| band | changed rows | changed columns | crop (padded even) | 4bpp bytes |
|---|---|---|---|---|
| *OUT OF THIS WORLD* | y46–78 (33) | x20–292 (273) | x19 y45, 274×34 | 4,658 |
| *HEART OF THE ALIEN* | y86–119 (34) | x10–308 (299) | x9 y86, 300×34 | 5,100 |

A single isolated changed pixel at `y78, x184` falls inside band 1 under this rule.

Both dimensions are padded to even — see [Even sides are
mandatory](#even-sides-are-mandatory). The padding grows towards the low end, so band 1
starts one row above its first changed row, at `y45`. That row is identical in both states,
which is why it was not a changed row, so drawing it from the lit frame is a no-op over the
background.

## Architecture

Three units, following the split this port already uses for `cdda_classify`: pure decision
logic in a host-testable C file, platform specifics behind a C seam, and a thin caller.

```
main.c
  initialize()
  boot_sequence()        <- new, #ifdef HOTA_SATURN
      |
      |-- bootmenu.c     pure state machine: elapsed ms + edge-triggered keys
      |                  -> screen id, highlight, music volume, start flag
      |                  no SRL, no stdio, no engine headers
      |
      |-- saturn_bootart.cxx   the only new file that includes <srl.hpp>
      |                        six TGAs -> VDP1 textures, drawn as sprites
      |                        NBG0 off for the sequence, on at exit
      |
      `-- disc.h         disc_play_track(2, 0), disc_set_music_volume(v)
  run()
```

The NBG0 hand-off is the same seam `saturn_movie.{h,cxx}` drew in Another-Saturn, and for
the same reason: a VDP2 layer can sit in front of a VDP1 sprite depending on priority, so
NBG0 is switched off for the duration rather than trusting an ordering nobody has verified
on hardware. The engine uses **no VDP1 at all** — `video_srl.cxx` is a pure VDP2 NBG0
bitmap — so the boot art and the game never contend for it.

Pure-logic files sit at `saturn/src/` root, matching `cdda_classify.c`, `discfmt.c` and
`cdtoc.c`; the SRL half goes in `saturn/src/system/` with the rest of the platform layer.

## Components

### `bootmenu.c` / `bootmenu.h` — the pure half

Free of SRL, stdio and every engine header, so `saturn/tests/run_tests.sh` compiles it with
the host gcc. It owns every timing decision in the sequence and makes none of the drawing
ones.

```c
typedef enum {
    BOOT_SCREEN_LEGAL = 0,
    BOOT_SCREEN_VIRGIN,
    BOOT_SCREEN_INTERPLAY,
    BOOT_SCREEN_TITLE,
    BOOT_SCREEN_MENU
} boot_screen;

typedef enum {
    BOOT_ENTRY_OUT_OF_THIS_WORLD = 0,
    BOOT_ENTRY_HEART_OF_THE_ALIEN = 1
} boot_entry;

typedef struct {
    boot_screen screen;
    boot_entry  highlight;      /* meaningful only on BOOT_SCREEN_MENU */
    uint8_t     music_volume;   /* 0..7, the SCSP CD-DA range */
    int         music_restart;  /* 1 on the frame the track must be started */
    int         start_game;     /* 1 when run() should take over */
} boot_frame;

void bootmenu_init(bootmenu_state *st);
void bootmenu_step(bootmenu_state *st, uint32_t elapsed_ms,
                   uint32_t pressed, boot_frame *out);
```

`pressed` is an **edge-triggered** bitmask of the eight keys `input.h` exports, computed by
the caller from `key_up`…`key_select`. Edge rather than level so a button still held from
the previous screen cannot skip two screens, and cannot move the cursor every frame.

### `saturn_bootart.h` / `saturn_bootart.cxx` — the SRL half

A four-call C API, so no caller includes `<srl.hpp>` — the engine's headers wrap SGL's C
headers in `extern "C"` and mixing that with SRL's C++ headers in one translation unit is
fragile.

```c
int  boot_art_load(void);                     /* all six TGAs, once, at boot */
void boot_art_draw(int screen, int highlight);
void boot_art_present(void);                  /* one Core::Synchronize */
void boot_art_release(void);                  /* NBG0 back on */
```

`boot_art_load` allocates all six textures up front and never releases them.
`VDP1::TryAllocateTexture` is a bump allocator with no free, so an attract loop that
re-allocated on every replay would exhaust 512 KB of sprite VRAM in three passes — the
exact failure Another-Saturn documented against its movie texture.

`boot_art_draw` issues one `Scene2D::DrawSprite` for a full-screen still, or two for the
menu (background, then the lit logo band at a nearer Z). `BOOT_SCREEN_VIRGIN_2` draws
the `BOOTVIRG` texture at the same position `BOOT_SCREEN_VIRGIN` does, rather than owning a
texture of its own.

### `boot_sequence()` in `main.c`

Called between `initialize()` and `run()`, gated `#ifdef HOTA_SATURN`. Per frame: sample
`check_events()`, fold the eight key globals into an edge mask, call `bootmenu_step` with
`platform_ticks()`, act on the returned `boot_frame`, `boot_art_draw`, `boot_art_present`.
Returns when `start_game` is set, after `boot_art_release()` and `disc_stop_track()`.

If `boot_art_load()` fails it returns immediately — see [Error handling](#error-handling).

### `tools/mkbootart.py`

Reads both AVIs and writes six uncompressed colour-mapped TGAs into `saturn/cd/data/`.
It **derives geometry by measurement rather than carrying the constants in this document
as literals**: it locates the static runs by hashing frames, finds the menu bands by
diffing the two states and splitting at the largest interior gap, and computes each crop's
bounding box. The numbers tabulated here are what it currently produces, not its input.

It ends with a `verify()` that fails the build on any of:

- a palette above 16 entries for a file declared `Paletted16`, or above 64 for the title
  card;
- an odd crop width (see [Even widths are mandatory](#even-widths-are-mandatory));
- a band split that does not yield exactly two bands;
- any output that is RLE-compressed.

ffmpeg is not on PATH on this machine; the tool takes the same winget fallback constant
`mkopeningcpk.py` uses in Another-Saturn.

## Assets

Committed to `saturn/cd/data/`, regenerable with `tools/mkbootart.py`.

| File | source | pixels | colours | mode | VDP1 bytes |
|---|---|---|---|---|---|
| `BOOTLEGL.TGA` | opening frames 7–312 | 320×224 | 2 | `Paletted16` | 35,840 |
| `BOOTVIRG.TGA` | opening frames 313–618 | 320×224 | 8 | `Paletted16` | 35,840 |
| `BOOTTITL.TGA` | opening frames 926–1233 | 320×224 | 21 | `Paletted64` | 71,680 |
| `MENUBG.TGA` | synthesised, see below | 320×224 | 15 | `Paletted16` | 35,840 |
| `MENUOOTW.TGA` | menu frame A, band 1 | 274×34 | ≤16 | `Paletted16` | 4,658 |
| `MENUHOTA.TGA` | menu frame B, band 2 | 300×34 | ≤16 | `Paletted16` | 5,100 |

**188,958 bytes of VDP1 texture VRAM**, against 512 KB, all of it otherwise unused by this
port. On disc the figure is different — see [TGA pixel data is 8bpp on
disc](#tga-pixel-data-is-8bpp-on-disc-whatever-the-palette) — at 306,236 bytes of pixel
data plus six headers and palettes, loaded once at boot in about two seconds and resident
thereafter.

Every file is written in the exact shape of `ELF_S.TGA`, which SaturnRingLib ships and
loads: `imagetype = 1`, `colourmaporigin = 0`, `colourmapdepth = 24`, `bpp = 8`,
`descriptor = 0x20`. ffmpeg's own targa encoder cannot be used to produce them — it writes
`colourmaplength = 256` regardless of how many colours the image has, which makes
`BitmapInfo` choose `Paletted256` and doubles the VRAM figure. `IsFormatValid` additionally
rejects any paletted TGA whose colour map origin is not strictly below its length
(`srl_tga.hpp:253`), so the origin must be 0 and the length must be the count actually
used.

## Decisions worth naming

### There is no "both logos dim" frame, so the background is synthesised

Every captured menu frame has one logo lit. `MENUBG.TGA` is therefore built by taking menu
frame A and replacing band 1 with frame B's band 1, producing the both-dim state that
exists nowhere in the source.

### The overlays are opaque crops, not transparency masks

Both menu states share one backdrop, so drawing frame A's band-1 crop over the synthesised
background reproduces frame A exactly, pixel for pixel, with no transparent colour index
and no `Tga::LoaderSettings` involvement. Highlighting is then two sprite draws with no
per-pixel work on the SH-2, and the cursor responds within a frame.

### Even sides are mandatory

`Tga::DecodePaletted`'s 16-colour path packs two horizontally adjacent pixels per byte,
advancing `xLocation` twice per iteration and terminating on `xLocation == xLoop.End`
(`srl_tga.hpp:353–371`). An odd width steps over the terminator and never matches. 320 is
even; both crops are widened by one column to 274 and 300.

Heights are padded to even for a different reason: `Scene2D::DrawSprite` positions a sprite
by its centre, so an odd height puts that centre on a half pixel. Band 1's 33 changed rows
become 34.

### Uncompressed, never RLE

`Tga::DecodeRlePaletted` allocates `width * height` bytes unconditionally and performs no
4bpp packing (`srl_tga.hpp:404`). An RLE-compressed 16-colour TGA therefore silently
doubles its VRAM footprint and loads as 8bpp. RLE would compress these frames very well —
they are mostly flat black — but the saving is on disc where there is room, and the cost is
in VRAM and correctness where there is not.

### TGA pixel data is 8bpp on disc whatever the palette

`DecodePaletted` reads one byte per source pixel and masks it (`buffer[location] & 0x0f`);
the 4bpp packing happens in SRL's decoder, not in the file. Every 320×224 still is
therefore 71,680 bytes of pixel data on the CD regardless of having 2 colours or 21. This
is the reason the disc figure (~306 KB) is so much larger than the VRAM figure (~184 KB),
and it is not a defect to go fix.

### The second Virgin screen reuses the first's texture unchanged

The two holds differ in 570 pixels of 71,680, all of them in the top three rows of the
logo's white frame. `BOOTVIRG` is cut from the first hold, which has the complete frame,
and both state-machine slots draw it at the same position — so the logo sits still for
10.2 s rather than flickering its top edge halfway through.

Reproducing the difference exactly would cost a second 35,840-byte texture and 71,680 bytes
of disc for four rows of white that appear for five seconds in a sequence the player can
skip. `tools/mkbootart.py` fails the build if a re-capture ever pushes the two holds past
2% divergence, at which point they are genuinely different screens and this decision should
be revisited rather than silently carried.

### The title card keeps all 21 colours

Its 8 rarest colours are anti-aliasing shades on the lettering and account for 1.5% of
pixels between them. Folding them into nearest neighbours would fit 4bpp and unify the
tool's output on one format, but `Paletted64` costs 35,840 extra bytes out of a 512 KB
budget that nothing else wants. Exactness is the cheaper of the two.

### The band split uses the largest gap, not a fixed tolerance

The first draft of this design merged diff bands with an 8-row tolerance. The measured
interior gaps are 6 and 8 rows, so that rule merges **both logos into a single band** and
makes independent highlighting impossible. Splitting at the largest interior gap is one
rule with no constant to get wrong, and it places the isolated changed pixel at `y78` into
band 1 where it belongs.

### CD-DA volume has eight steps, so the fade is a staircase

`SRL::Sound::Cdda::SetVolume` wraps `SND_SetCdDaLev`, whose range is 0–7. A one-second
fade is therefore eight steps of ~125 ms, not a ramp, and it will be audible as steps if
listened for. It is specified at one second because lengthening it does not add
resolution — the step count is fixed — it only gives the ear longer to notice each one.

## Audio

**The track is `saturn/cd/music/track03.wav`, and it is 2:46 long.** That duration is the
identifying check, because the disc carries two numbering schemes one apart and the wrong
one is inaudible as a bug — it merely sounds like different music.

- **Duration.** `track03.wav.raw` is 29,312,976 bytes, which at 176,400 bytes/s is
  12,463 sectors and **166.173 s = 2:46.17**. Anything else is the wrong file.
- **Disc track number: 03.** The rip has 42 disc tracks — Track 01 is the data track and
  Tracks 02–42 are the 41 audio tracks. The wavs are named by *disc* track number, so
  `track03.wav` is disc track 3, and `saturn/cd/music/tracklist` pins the order such that
  `shared.mk` emits `TRACK 03 AUDIO` for it (`shared.mk:426`).
- **Audio ordinal: 2nd.** Disc track 3 is the *second* audio track, since audio starts at
  disc track 2. This is the trap: "audio track 3" counted as an ordinal would be
  `track04.wav`, which is 0:45 and the wrong piece.
- **Engine music index: 2.** `discfmt_cue_track_for_music` returns
  `engine_index + DISCFMT_MUSIC_FIRST_TRACK` with `DISCFMT_MUSIC_FIRST_TRACK == 1`
  (`discfmt.c:16,83`), mapping engine 1–41 onto disc 02–42. Disc track 3 is engine index 2.

The call is therefore **`disc_play_track(2, 0)`**, and the implementation should assert the
resolved cue track is 3 rather than trusting the arithmetic.

The track is started unlooped as the first still appears, and again on every attract
replay. It plays continuously across the whole sequence: **skipping the opening neither
stops nor restarts it**, because the menu the player skips to is part of the same
presentation.

The 40-second cap is mostly emergent. The full sequence is 20.4 s of stills plus 19 s of
menu idle, so the fade begins at 38.4 s and the replay happens at 39.4 s. Skipping early
shortens the music, not the menu — the menu's 19 s is fixed — which is the intended "cuts
music early". A hard guard fades out at 40,000 ms regardless of state, so a later timing
change cannot let track 03 run on into its second minute.

Volume reaches the hardware through a new `disc_set_music_volume(uint8_t)` on the disc
seam, wrapping `SRL::Sound::Cdda::SetVolume` on Saturn and a no-op on the host.

## The state machine

```
                          any of the eight keys (edge)
        ┌────────────────────────────────────────────────┐
        ▼                                                │
    OPENING                                              │
      LEGAL        0 –  5100 ms                          │
      VIRGIN    5100 – 10200 ms                          │
      VIRGIN_2   10200 – 15300 ms   (BOOTVIRG again)     │
      TITLE     15300 – 20400 ms                         │
        │ elapsed                                        │
        ▼                                                │
    MENU ────────────────────────────────────────────────┘
      Up / Down            toggle highlight, reset idle timer
      A / B / C on HEART OF THE ALIEN   → start_game = 1
      A / B / C on OUT OF THIS WORLD    → ignored, cursor stays
      any input            resets the 19 s idle timer
      19 s idle            → fade 1 s → OPENING, music restarts
```

The cursor starts on *OUT OF THIS WORLD*, matching the first frame of the menu capture.
Screen durations are 5100 ms each rather than the measured 5100/5100/5117/5133, because the
capture's variation is frame-quantisation at 60 fps and not something the original was
doing on purpose.

## Error handling

**A missing decoration must never brick the disc.** If any TGA is absent or unreadable, if
`TryAllocateTexture` returns negative, or if `CRAM::GetFreeBank` has nothing free,
`boot_art_load()` reports failure and `boot_sequence()` returns immediately into `run()`.
A build with no boot art boots into the game.

Music needs no handling of its own: `disc_play_track` is already documented as a no-op for
a track the mounted disc does not carry (`disc.h:130`), which is exactly the case for a
`HOTA_AUDIO=none` build. Such a build shows the sequence in silence.

## Testing

`saturn/tests/test_bootmenu.c` joins the seven suites already in `run_tests.sh`, driving
`bootmenu_step` against a synthetic clock:

- each screen id at its boundaries, including the last millisecond of `TITLE` and the first
  of `MENU`;
- skip from each of the four stills, asserting `MENU` and no `music_restart`;
- an edge-triggered key held across many frames skips exactly once;
- cursor toggling on Up and Down, and that it starts on *OUT OF THIS WORLD*;
- confirm on *HEART OF THE ALIEN* sets `start_game`; confirm on *OUT OF THIS WORLD* sets
  nothing and leaves the highlight where it was;
- the idle timer resetting on input, including on a confirm that was ignored;
- the volume ramp reaching 0 exactly at the replay, and being 7 at every other time;
- the attract replay setting `music_restart` and returning to `LEGAL`;
- the 40,000 ms guard firing even when the state machine has been driven somewhere the
  normal timeline cannot reach.

`saturn_bootart.cxx` has no automated coverage and cannot have any — VDP1 and CRAM are only
observable on hardware or in Mednafen. Its correctness is an emulator run, made by the
human, per the standing convention that no tool call launches the build or the emulator.

## Build and test

- `tools/mkbootart.py` writes six files into `saturn/cd/data/` and is not run by the
  makefile. Its outputs are committed, so a clean checkout builds with neither Python nor
  ffmpeg present.
- `bootmenu.c` and `saturn_bootart.cxx` are picked up by the makefile's `find src/` globs
  with no makefile change. **New C files take `.c` and new C++ files take `.cxx`** —
  `shared.mk` defines pattern rules for only those two extensions, so a `.cpp` would be
  silently dropped from the link.
- Syntax-check with `-fsyntax-only` and the flags from `make -n src/<file>.o`, using the
  SH-2 compiler at `SaturnRingLib/Compiler/sh2eb-elf/bin/`. **Do not run `compile.bat`** —
  it opens with `rm -f` on the ISO, so an overlapping run hands Mednafen a half-written
  disc.
- Host tests: `sh saturn/tests/run_tests.sh` from the repo root.

## Out of scope

- **Running *Out of This World*.** Part I is a separate 68000 program inside `MAKE1IB.BIN`;
  the data track carries no `GAME1.BIN` and no Part I rooms, and this engine implements
  Part II only. Its menu entry renders faithfully and can be highlighted, and confirming on
  it does nothing. That is a deliberate dead end, not an oversight.
- **Save-game, options and region menus.** The capture offers two entries and nothing else.
- **Showing the legal screen during `initialize()`.** Loading behind the first still would
  hide the two-second load, but it entangles the boot art with the disc layer's read
  bracket for a gain the player experiences once per boot.
- **Host-side boot sequence.** `boot_sequence()` is `#ifdef HOTA_SATURN`; the SDL build
  keeps booting straight into the game. `bootmenu.c` itself is portable and tested there.
- **Compressing the TGAs.** Argued under [Uncompressed, never RLE](#uncompressed-never-rle).

## Acceptance

1. Booting the disc shows legal text, the Virgin logo held 10.2 s across two slots, and the
   title card, 5.1 s each, with cue track 03 playing from the first of them.
2. Any button during the opening jumps to the menu without interrupting the music.
3. The menu is pixel-identical to the capture in both highlight states.
4. Up and Down move the cursor; A, B or C on *HEART OF THE ALIEN* starts the game and stops
   the music; the same on *OUT OF THIS WORLD* does nothing.
5. Leaving the menu alone for 19 s fades the music out over 1 s and replays the opening
   with the music restarted.
6. Track 03 never plays past 40 s in any path through the sequence.
7. A disc built with the TGAs removed boots into the game rather than hanging.
8. `sh saturn/tests/run_tests.sh` passes with the new suite included.

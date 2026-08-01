# Heart of The Alien → Sega Saturn — Boot and Video Design Spec

**Date:** 2026-08-01
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Get the engine compiling for SH-2, fitting in the Saturn's 2 MB of work RAM, and
drawing the game's own pixels on real hardware.

The previous sub-project gave the engine a portable way to reach the disc (`disc.h`,
implemented on the host against bin/cue). `6d31b11 prep sega-saturn port.` then moved
all 41 engine files from `src/` to `saturn/src/`, where `saturn/makefile`'s
`find src/ -name '*.c'` glob will pick them up. Nothing has ever been compiled for
SH-2. This sub-project takes it from there to a booting disc that draws.

Done means: `compile.bat` produces an ELF, the disc boots in Mednafen, the engine
loads its blobs off the CD through `SRL::Cd::File`, and the intro renders through
VDP2. Input and sound stay stubbed.

## The finding that drives every decision

The engine's static buffers do not fit in the Saturn.

Measured with `size -A` against the host objects — this is the linker's own
accounting, not an estimate:

| Object | `.bss` | What it is |
|---|---:|---|
| `vm.o` | 1,053,248 | `memory[512*1024*2]` (`vm.c:26`) plus VM variables |
| `game2bin.o` | 409,600 | `game2bin[GAME2BIN_SIZE]` (`game2bin.c:27`), resident GAME2.BIN |
| `animation.o` | 204,896 | `screen4`, `screenX` animation scratch |
| `screen.o` | 175,168 | `huge_buf[304*192*3]` (`screen.c:31`), the three screens |
| `main.o` | 34,976 | `scratchpad[29184]`, recorded-key cache |
| `sound.o`, `sprites.o`, `debug.o`, `decode.o`, `client.o` | 4,704 | |
| **Saturn-side total** | **1,882,592** | against 2 MB of system RAM, before a byte of code |

`modules/sgl/sgl.linker` bases the program at `0x06004000` and lays `.text`, `.data`,
`.rodata`, `.bss` consecutively, with `__heap_start` immediately after `.bss` and
`__heap_end` pushed down by the SGL work area and the VDP1 command buffer. Measured
against Another-Saturn's map, `work_area_start` lands at `0x060c0000`, so HWRAM
offers **770,048 bytes** for text + data + bss + heap combined. `srl_memory.hpp:593`
hands the entire 1 MB of LWRAM at `0x00200000` to one TLSF pool and reserves nothing.

1.8 MB of BSS against 770 KB of HWRAM is not a tuning problem. It decides the
architecture.

## The preflight: `memory[]` is twice the size it needs to be

Every write into the emulated 68000 map goes through `get_memory_ptr()` (`vm.c:140`),
and there are only four call sites. Three are load addresses fixed in the code:

- `main.c:116` — `ROOMSn.BIN` at offset `0xf900`. Largest room file is 370,688 bytes,
  so the highest byte touched is **434,432**.
- `animation.c:870` — animations at `read_offset = 0x809a - fileoffset`, which is at
  most `0x809a` when `fileoffset` is zero. Largest animation file is `MAKE2MB.BIN` at
  436,224 bytes, so the highest byte touched is **469,146**.
- `game2bin.c:56` — reads into its own array; never touches the map.

The fourth, `animation.c:748`, indexes with a value that comes from game data rather
than from a constant, which is why the 512 KB figure below carries a runtime check
rather than being taken on faith.

Sizes are from the validated disc manifest in `disc_cue.c:101-119`, which the previous
sub-project verified against the real Redump rip.

So `memory[512*1024*2]` is roughly twice what the game uses. Shrinking it to
`0x80000` (524,288) leaves ~55 KB of headroom over the worst measured case.

This is load-bearing, not an optimization. At the original 1 MB, `memory[]` and
`game2bin[]` together need 1,458,176 bytes and there is no arrangement of the two
arenas that fits them.

## Decision: two arenas, split by access pattern

**HWRAM holds code and everything the rasterizer touches per pixel.** `screen.o`'s
three screens, `animation.o`'s scratch, `main.o`'s scratchpad. These are written a
byte at a time by the software renderer and belong on the 32-bit bus.

**LWRAM holds the two bulk blobs.** `memory[]` at 512 KB and `game2bin[]` at 400 KB,
933,888 bytes of the 1 MB pool, allocated through `SRL::Memory::LowWorkRam::Malloc`
as ordinary allocations. They fit inside the pool with its TLSF headers, so no
placement-by-address hack is needed and nothing has to be reserved from other users
of the low arena.

The cost is explicit: the VM fetches its bytecode from `memory[]`, which is now on
LWRAM's 16-bit bus, so every instruction fetch pays a penalty. That is the accepted
trade for this sub-project, and the escape hatch is quantified under Risks.

## Architecture

| File | Language | Responsibility |
|---|---|---|
| `saturn/src/video.h` | C header | The video seam. Nine functions, no platform types in any signature. |
| `saturn/host/video_sdl.c` | C | Host backend. Today's `render.c`, moved and unchanged in behaviour. |
| `saturn/src/system/video_srl.cxx` | C++ | Saturn backend. VDP2 NBG0 `Paletted256` bitmap, CRAM palette, hardware scroll. |
| `saturn/src/system/disc_srl.cxx` | C++ | Saturn backend for the existing `disc.h`, on `SRL::Cd::File`. |
| `saturn/src/platform.h` | C header | Timing, frame pump, quit. The non-video half of what `main.c` gets from SDL. |
| `saturn/host/platform_sdl.c` | C | Host backend: `SDL_GetTicks`, `SDL_Delay`, `SDL_PollEvent`. |
| `saturn/src/system/platform_srl.cxx` | C++ | Saturn backend: `SRL::Core::Synchronize`, SMPC clock. |
| `saturn/src/system/saturn_compat.{h,cxx}` | C++ | `malloc`/`free`/`exit` onto SRL arenas, the `FILE` shim, `printf` to the debug layer. |
| `saturn/src/system/saturn_filestub.c` | C | Always-failing `fopen`/`fread`/… so never-executed stdio paths still link. |
| `saturn/src/system/saturn_new.cxx` | C++ | Global `operator new`/`delete`, in a TU that does not include `<srl.hpp>`. |
| `saturn/src/vm.c` | C | Edited: `memory[]` becomes a pointer behind a platform guard. |
| `saturn/src/game2bin.c` | C | Edited: `game2bin[]` becomes a pointer behind the same guard. |
| `saturn/src/render.c` | — | Deleted; becomes `saturn/host/video_sdl.c`. |
| `saturn/src/host/disc_cue.c` | — | Moved to `saturn/host/disc_cue.c`, out of the Saturn glob. |

Everything under `saturn/src/system/` is Saturn-only and everything under
`saturn/host/` is host-only. The split is enforced by directory, not by `#ifdef`,
because `saturn/makefile` globs `src/` recursively and `saturn/host/` sits outside it.

## Components

### Memory placement

`vm.c:26` and `game2bin.c:27` each change from a static array to a pointer, behind a
platform guard so the host build keeps its static array verbatim and the host's
`.bss` layout is untouched:

- `get_memory_size()` (`vm.c:149`) currently returns `sizeof(memory)`. With `memory`
  a pointer, `sizeof` silently becomes 4. It must return a named constant instead.
  This is the single most dangerous edit in the sub-project: it compiles clean and
  fails at runtime by handing every caller a 4-byte buffer. The constant is defined
  once in `vm.h` and used by both platforms so host and Saturn cannot drift.
- Allocation happens once at startup, before `game2bin_init()`, from
  `SRL::Memory::LowWorkRam`. Failure is fatal and reported through `panic()`.
- `MEMORY_SIZE` becomes `0x80000` on both platforms, so the host build exercises the
  same bound the Saturn does and the runtime check below is meaningful.

### `video.h` — the seam

`render.h` is already the right interface: nine functions, no SDL types in any
signature. The seam formalizes it in `disc.h`'s style — banner comment carrying the
rationale, an explicit ordering contract, one backend per platform:

```
int  video_init(void);
int  video_create_surface(void);
void video_render(char *src);
void video_set_palette(int which);
void video_set_palette_rgb12(unsigned char *rgb12);
int  video_get_current_palette(void);
void video_set_scroll(int scroll);
int  video_get_scroll_register(void);
void video_toggle_fullscreen(void);
```

The ordering contract: `video_init` before anything else, `video_create_surface`
before the first `video_render`, and a palette set before the first render or the
frame comes out black. `video_toggle_fullscreen` is a documented no-op on Saturn
rather than an error, because the engine calls it from a key handler that will exist
on both platforms.

### Host backend — `video_sdl.c`

Today's `render.c` moved to `saturn/host/`, with the SDL window/renderer/texture code
unchanged. The only edits are the renames to the `video_` prefix. Behaviour must be
identical: this file is the reference a wrong Saturn frame gets compared against, so
changing it and the Saturn backend in the same sub-project would destroy its value.

### Saturn backend — `video_srl.cxx`

On `SRL::VDP2::NBG0`, declared `BmpScreen<NBG0, scnNBG0, NBG0ON>` (`srl_vdp2.hpp:937`).

- **Format.** The engine's screens are 8-bit paletted and SRL offers
  `CRAM::TextureColorMode::Paletted256`, so there is no pixel conversion at all —
  `video_render` is a copy, not a transform.
- **The copy.** VDP2 bitmaps are fixed-size, so 304×192 lives in a 512×256 layer:
  source pitch 304, destination stride 512, 192 per-line copies. One `memcpy` will
  not do. The image is centred in the 320×224 NTSC display with a border; it is not
  scaled, because VDP2 cannot scale a bitmap layer for free and the engine's pixel
  doubling is a host-only comfort.
- **Scroll.** `render.c:78-111` fakes the scroll register by shifting every row as it
  blits, in three separate branches. On VDP2 that is the layer's scroll position
  register: the pixels do not move and the entire branch disappears. `video_set_scroll`
  writes the register; `video_get_scroll_register` returns the shadow value, because
  the engine reads it back.
- **Palette.** `set_palette_rgb12` receives 16 entries of 4-bit-per-channel RGB. Each
  channel maps to RGB555 by one left shift. Another-Saturn established that the "565"
  comment in the equivalent Another World code is wrong and that the nibble order
  already matches; both findings are re-verified here rather than assumed, because
  HOTA is a different codebase with the same Sega-CD-era heritage.

### Saturn disc backend — `disc_srl.cxx`

Implements the existing five-function `disc.h` on `SRL::Cd::File` (`Open()`,
`Read(size, destination)`, `Size.Bytes`). Only `disc_read_file` does real work in this
sub-project:

- The manifest's names are already 8.3 uppercase, so they pass to GFS unchanged.
- `max_size` is honoured as the bound it is. The `disc.h` contract exists because
  `read_file`'s callers pass raw pointers into the emulated map with no bounds
  checking of their own.
- `disc_play_track` and `disc_stop_track` are no-ops, documented as such at the call
  site, until the audio sub-project.
- `disc_open` takes the cue path on the host; on Saturn there is no cue, so it
  initialises GFS and ignores the argument. The signature does not change.

Another-Saturn's `saturn_cdfile.cxx` is the working reference.

### `platform.h` — timing and the frame pump

The non-video half of what `main.c` gets from SDL: `SDL_Init`/`SDL_Quit`,
`SDL_GetTicks`, `SDL_Delay`, and the `SDL_PollEvent` loop. The seam is five functions
— `platform_init`, `platform_quit`, `platform_ticks`, `platform_delay`, and
`platform_frame` — the last because on Saturn the frame pump is
`SRL::Core::Synchronize()` and the engine's loop has to call it or nothing is ever
displayed. Event polling rides on `platform_frame`, which returns whether a quit was
requested; input is deliberately out of scope, so the Saturn implementation always
returns "keep running" and the intro plays on its timer.

### libc and linkage shims

The five traps from Another-Saturn's `srl-libc-shadowing`, which fire on the first
build and cost more time than anything Saturn-specific. They are adapted, not
rediscovered: `<cstdio>` cannot be included at all; prefer C headers over `<cXXX>`
wrappers; SGL's `stdlib.h` has no `malloc` and no `exit`; SGL headers have no
`extern "C"` guard so their includes must be wrapped; global `operator new`/`delete`
must be defined in a TU that does not include `<srl.hpp>`. Plus the bonus trap: do not
define `fflush`, make it a no-op macro, or newlib's own stdio collides.

The constraint that falls out of routing `malloc` onto an SRL arena: no global or
static object whose constructor allocates may exist in the build.

### Pure subtraction

`decode.c`, `animation.c`, `scale2x.c` and `scale3x.c` include `<SDL.h>` and use zero
SDL symbols. Those four includes are deleted. This is verified, not assumed — with
only that edit, 14 of the 19 engine TUs already compile clean for SH-2.

## Data and control flow

```
main()
  ├─ SRL::Core::Initialize                 (Saturn only, before any allocation)
  ├─ platform_init                          timing, frame pump
  ├─ vm_alloc_memory                        memory[]  ← LWRAM, 512 KB
  ├─ game2bin_alloc                         game2bin[] ← LWRAM, 400 KB
  ├─ disc_open                              GFS init  /  cue parse on host
  ├─ game2bin_init      → disc_read_file    GAME2.BIN → LWRAM
  ├─ video_init, video_create_surface       NBG0 Paletted256 bitmap in VDP2 VRAM
  └─ game loop
       ├─ load_room     → disc_read_file    ROOMSn.BIN → memory[0xf900]
       ├─ play_animation→ disc_read_file    INTROn.BIN → memory[0x809a - off]
       ├─ engine renders into huge_buf      HWRAM, software rasterizer
       ├─ video_set_palette_rgb12           12-bit RGB → CRAM RGB555
       ├─ video_render(screen)              192 line-copies → VDP2 VRAM
       └─ platform_frame                    SRL::Core::Synchronize
```

The allocation steps must precede `game2bin_init`, which is the first thing that
reads from the disc, which is the first thing that writes into either buffer.

## Memory

**HWRAM** — `0x06004000` to `work_area_start` at `0x060c0000`, **770,048 bytes**:

| | bytes |
|---|---:|
| text + rodata + data + SLPROG | ~125,000 |
| `.bss` — `screen.o` 175,168, `animation.o` 204,896, `main.o` 34,976, rest ~5,900 | 420,944 |
| **heap remaining** | **~224,000** |

The code figure is corroborated two ways: 14 engine TUs measured at 24,495 bytes of
text+rodata+data compiled for SH-2 at the SDK's own `-O2 -m2`, plus roughly 12 KB for
the ported `main.c` and the video backend, plus the SRL/SGL runtime — against
Another-Saturn's measured total of 123,612 bytes for a comparable engine.

**LWRAM** — 1,048,576-byte TLSF pool: `memory[]` 524,288 + `game2bin[]` 409,600 =
**933,888**, leaving ~114,000.

**VDP2 VRAM** — the 512×256 `Paletted256` display bitmap is 131,072 bytes of the
512 KB VDP2 has. It never touches work RAM; the display buffer is free.

Heap demand is supply, not proven surplus: `sound.c`'s allocations are deferred to the
audio sub-project and `SRL::Cd::File` keeps a work buffer whose size is not yet
measured.

## Deferred / stubbed

- **Input.** `platform_frame` reports no quit and no keys on Saturn. Pad mapping is
  sub-project 3.
- **Sound.** `sound_init` is a no-op, `play_sample` returns without playing.
  `disc_play_track`/`disc_stop_track` are no-ops. Sub-project 5.
- **Saves and key recording.** `main.c`'s `record_fp` stdio path links against
  `saturn_filestub.c` and always fails, which is the correct behaviour for now.
- **`scale2x.c` / `scale3x.c`.** Host-only pixel doublers; excluded from the Saturn
  build rather than ported.

## Build and test

`saturn/makefile:55` globs `find src/ -name '*.c'`, which today swallows
`saturn/src/host/disc_cue.c` and would compile the SDL host backend for SH-2. Moving
host backends to `saturn/host/` puts them outside the glob; `saturn/src/Makefile` is
retargeted at `../host/`. Both builds must keep working — that is the point of the
dual-backend decision, and a broken host build removes the reference this sub-project
depends on.

Three levels of verification:

1. **Host unit tests.** `saturn/tests/run_tests.sh` keeps passing unchanged.
2. **The 512 KB bound.** A bounds assert in `get_memory_ptr()`, compiled into the host
   build where `assert` is real, exercised by a full playthrough. This is what turns
   the static three-call-site analysis into evidence. Until it has run, 512 KB is a
   hypothesis. `animation.c:748` indexes from game data and is the specific reason
   this check exists.
3. **The disc boots.** `compile.bat` produces an ELF, and Suinevere runs Mednafen.
   Never launched from a tool call.

The host build stays the bisection reference: any frame that looks wrong on Saturn is
compared against the same frame from `video_sdl.c` before the Saturn backend is
suspected.

## Risks and mitigations

**The VM's bytecode is on the slow bus.** `memory[]` in LWRAM means every instruction
fetch pays 16-bit-bus latency. Mitigation, quantified: the SGL work area is 171,120
bytes, computed from `modules/sgl/SRC/workarea.c` and matching Another-Saturn's map
exactly, and it is sized entirely by `SGL_MAX_POLYGONS` and `SGL_MAX_VERTICES`, which
`saturn/makefile` inherits at 1500 and 2500. HOTA draws no VDP1 polygons, so trimming
those recovers roughly 155 KB — enough to move `memory[]` back to HWRAM. Beyond that,
`huge_buf` into VDP2 VRAM frees another 171 KB. Neither is needed to fit; both exist
if the frame rate says otherwise.

**`get_memory_size()` returning `sizeof` a pointer.** Compiles clean, fails at
runtime, hands every caller a 4-byte buffer. Mitigated by the named constant in `vm.h`
shared by both platforms, and caught immediately by the host bounds assert.

**The 512 KB figure is analysis, not measurement.** Three of four call sites are
constants; the fourth is data-driven. Mitigated by test level 2 above, which must run
before the number is trusted on hardware.

**Unaligned access.** Another-Saturn found that the SH-2 raises an address error on
unaligned word and long loads, and that its engine read words from byte-stepped
pointers as a matter of course. HOTA's `get_long`/`get_byte` on `memory[]` are the
same shape. This must be checked early, not discovered as a crash: the engine reads
`get_long(0x809e)` and `get_long(0x80a6 + (pattern << 2))` from data-derived offsets.

**Heap exhaustion.** ~224 KB of HWRAM heap against unmeasured demand. Mitigated by
measuring the linker map after the first successful link, when the number stops being
an estimate.

## Out of scope

Pad input, sound of any kind, saves, the copy-protection wheel, `scale2x`/`scale3x`,
VDP2 VRAM framebuffers, SGL work-area trimming, and any change to `discfmt.c` or the
disc manifest — the previous sub-project settled those and they are not reopened here.

## Acceptance

- `compile.bat` produces `BuildDrop/Heart of the Alien (USA).{elf,iso,bin,cue,map}`.
- `saturn/src/Makefile` still builds a working host binary against the same sources.
- `saturn/tests/run_tests.sh` passes.
- The host bounds assert survives a full playthrough at `MEMORY_SIZE = 0x80000`.
- The linker map shows `.bss` and heap inside HWRAM with the LWRAM allocations
  succeeding at startup.
- The disc boots in Mednafen and the intro draws.

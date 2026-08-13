---
name: 2026-08-13-boot-sequence-build
description: The boot sequence (four opening stills, a two-entry menu, cue track 03) is complete and reviewed across seven tasks on branch port/boot-sequence, unmerged and unpushed. Its black screen is solved and was never the feature: the HWRAM heap is 366 bytes too small to stage SDDRVS.TSK, so SGL's sound driver initialises from the boot ROM. The SRL traps recorded here matter beyond this feature: Bitmap::TGA cannot be used in this port at all, and TryLoadTexture refuses paletted bitmaps without a handler.
metadata:
  type: project
---

## Where the work is

Branch **`port/boot-sequence`**, cut from `260c5df` on `main`. No worktree: `saturn/cd/music/*.wav` and `saturn/cd/data/*.bin` are gitignored, so a worktree would carry neither game data nor music and could not be built.

Live ledger, which is the authoritative resume map:
`.superpowers/sdd/2026-08-13-hota-saturn-boot-sequence/progress.md` (gitignored).

Spec: `docs/superpowers/specs/2026-08-13-hota-saturn-boot-sequence-design.md`
Plan: `docs/superpowers/plans/2026-08-13-hota-saturn-boot-sequence.md`

| Task | State |
|---|---|
| 1 — `tools/mkbootart.py` + six TGAs | complete, review clean |
| 2 — `bootmenu.{c,h}` + tests | complete, review clean |
| 3 — menu-half tests | complete, review clean |
| 4 — `disc_set_music_volume` | complete, review clean, no findings |
| 5 — `saturn_bootart.{h,cxx}` | complete at `2315367`, review clean after two fix rounds |
| 6 — `boot_sequence()` in `main.c` | complete at `839bc03`; its two fixes were later confirmed correct by the whole-branch review |
| 7 — pre-packed `.ART` assets replacing the TGA path | complete at `9306ae6`, review clean, no blocking findings |

The whole-branch review ran on the strongest model and is what found the Critical below. Twenty-eight commits, `260c5df..9306ae6`, **unmerged and unpushed**.

Only one gap remains in the process: Task 7's final two-line fix (a CRAM bank release and a comment correction) carries controller self-verification rather than an independent re-review, on the grounds that both findings were non-blocking on an already-clean review.

## The black screen is the HWRAM heap running out from under SGL's sound driver

**Solved, by arithmetic against the map and the disassembly of the shipped ELF.** Two
earlier diagnoses were wrong and are recorded here so neither is re-run: it is not the TGA
heap arithmetic (the machine never reaches it), and it is not static-initialiser order in
`saturn_bootart.cxx` (nothing corrupts anything).

`SRL::Sound::Hardware::Initialize()` (`srl_sound.hpp:28-50`) stages the SGL sound driver
through the HWRAM heap:

```cpp
Cd::File program("SDDRVS.TSK");                              // 26,610 bytes
uint8_t* programBuffer = new uint8_t[program.Size.Bytes];    // <- returns NULL
program.LoadBytes(0, program.Size.Bytes, programBuffer);
SND_INI_PRG_ADR(init) = (uint16_t*)programBuffer;
SND_Init(&init);
```

The heap cannot hold it, and nothing on the path says so:

| | bytes |
|---|---|
| `__heap_start 0x060b8d00` .. `__heap_end 0x060c0000` | 29,440 |
| less `sizeof(control_t)` — TLSF, `FL_INDEX_COUNT 24` × `SL_INDEX_COUNT 32` | −3,188 |
| less `tlsf_pool_overhead()` | −8 |
| **largest single allocation the heap can serve** | **26,244** |
| `SDDRVS.TSK` | 26,610 |
| **shortfall** | **366** |

`tlsf_malloc` returns 0; `operator new` in `saturn_new.cxx` is a bare `return malloc(size)`
so there is no `bad_alloc`; `program.Exists()` was true so the guarded block is entered
anyway. `SND_Init` then memcpys `prg_sz` bytes **from address 0** — the boot ROM — into
SCSP RAM, and reads its own pointer table back out of the garbage it just wrote.

The disassembly of `_SND_Init` in the shipped ELF closes the loop exactly:

```
6018148:  mov.l  @r2,r5          ! r5 = init->prg_adr  == NULL
601814c:  jsr    @r1             ! copy(dst=0x25A00000, src=NULL, len=26610)
6018154:  mov.l  @r1,r1          ! r1 = [0x25A00400]   read back out of sound RAM
6018156:  add    r8,r1           ! + 0x25A00000
6018158:  mov.l  r1,@r2          ! [0x060b6014] = 0xF8B8E4FF
...
60181d0:  mov.l  @(8,r1),r4      ! FAULTS: 0xF8B8E507 is odd
```

`0x060181D0 + 4 = 0x060181D4`, the faulting PC in the exception frame. The "32-byte island
of garbage in otherwise-zeroed BSS" at `0x060B6014`–`0x060B6034` was never a rogue write:
the map has `.bss 0x060b6010 0x28 LIBSND.A(snd_main.o)`, and every store between
`0x06018150` and `0x0601817a` lands inside it. It is the sound driver's own pointer table,
computed from the boot ROM.

**Why now.** The margin was always ~400 bytes. `saturn_bootart.o` links 10,989 bytes;
`__heap_start` is the end of BSS, so the branch pushed it past the cliff. Nothing about the
boot art is wrong — any 400 bytes of new code anywhere would have done this.

**Do not run the "decisive experiment" recorded in the previous version of this file.**
Removing `saturn_bootart.cxx` *would* have booted, by freeing 11 KB of heap, and would have
confirmed a static-initialiser hypothesis that is false.

**Fixed by growing the heap, not by shrinking the feature.** `sgl.linker:81-85` sets
`__heap_end = work_area_start = ALIGN(command_buf_start - SIZEOF(WORK_AREA_DUMMY), 0x1000)`
with `command_buf_start` fixed at `0x060EA000`, and `WORK_AREA_DUMMY` carries a trailing
`. = ALIGN(0x1000)` so its SIZEOF is always the struct rounded up to 4 KB. `workarea.c:10-13`
sizes that struct from `SGL_MAX_POLYGONS` and `SGL_MAX_VERTICES`: 84 bytes of heap per
polygon, 16 per vertex. `saturn/makefile` now sets both to **256**, down from 1500/2500.

Measured, not modelled — `workarea.c` recompiled with the new flags into the scratchpad:

| | before | after |
|---|---|---|
| `WORK_AREA_DUMMY` section | `0x29e70` | `0x7a00` |
| link-rounded `SIZEOF` | `0x2a000` | `0x8000` |
| `__heap_end` | `0x060c0000` | `0x060e2000` |
| heap | 29,440 | 168,704 |
| largest single allocation | 26,244 | 165,508 |
| `SDDRVS.TSK` (26,610) | fails by 366 | fits, 138,898 spare |

`__heap_start` does not move: `.bss`/`.data`/`.rodata` in `workarea.o` are byte-identical
either way (`0xc8`/`0x28`/`0x18`), because `EventBuf`/`WorkBuf` key off `SGL_MAX_EVENTS`
and `SGL_MAX_WORKS`, which stayed at 1.

The 256 ceiling is 128× what the port asks for. The game renders entirely into VDP2 NBG0
bitmap memory by `slDMACopy`; the only VDP1 sprites in the tree are `saturn_bootart.cxx`'s
two.

**`workarea.o` is only rebuilt because `all` runs `clean-preserve-audio` first**
(`shared.mk:518-520`, `rm -f $(SGLLDIR)/../SRC/*.o`). The pattern rule is a plain
`%.o : %.c` timestamp dependency with no dependency on the flags, so a bare `make build`
after changing these constants would silently relink the stale object and change nothing.
`make -n` shows exactly that, because `-n` does not run the `rm`.

Still open, and worth doing: `Sound::Hardware::Initialize` fails silently — the heap margin
is invisible and will drift back down as code grows. A build-time check comparing
`__heap_end - __heap_start` in the map against the size of `cd/data/SDDRVS.TSK` would turn
the next recurrence into a failed build instead of a day of save-state archaeology.

## The boot art comes from ordered screengrabs now. Do not go back to the AVIs

The disc boots and the menu works. Three defects showed up on the first run and all three
are fixed; two of them were one root cause.

**The Kega AVIs in `tools/assets/avi` are unusable and are no longer read by anything.**
`tools/mkbootart.py` now reads the six ordered 640x480 screengrabs in `tools/assets/TGA`.
The AVIs are still committed but nothing references them; delete them when you are sure.

What the capture got wrong, measured from the decoded stream:

- The opening is **four distinct stills** — legal, Virgin, **Interplay**, title — each held
  ~5.1 s. The kgv1 stream never updated the Interplay slot: it decoded as the Virgin logo
  again, differing from the real Virgin hold by 570 pixels. That is why the logo appeared
  to sit for 10.2 s. There was never a "second Virgin hold"; the old `BOOT_SCREEN_VIRGIN_2`
  and mkbootart's drift check were both built on that artifact.
- The title slot then updated only partly, leaving the Virgin logo's red square and white
  bar composited into `BOOTTITL.ART`, with dissolve streaks through the letters. The
  emulator screenshot matched that file pixel for pixel, so nothing was wrong in the
  drawing path — the corruption was in the asset.
- **ffmpeg reported `0 decode errors` both times.** Do not treat a clean decode as a clean
  picture.

Five of the seven .ART files came out byte-identical from the screengrabs, which is the
cross-check that the AVI was wrong only where measured.

**VDP1 sprite width must be a multiple of 8.** CMDSIZE stores horizontal size in 8-dot
units, so a 274-wide band was read as 272: VDP1 walked 136 bytes per row through data
written 137 bytes per row, and the picture sheared two pixels further right on every line.
`mkbootart.py`'s `pad_to` now pads widths to 8 and heights to 2, and `verify()` fails the
build on a width that is not a multiple of 8. Nothing in SRL or SGL rejects the size — it
only ever shows up on screen. Any future VDP1 sprite in this port needs the same.

`BOOTPLAY.ART` is the Interplay screen. Seven textures now, ~185 KB of the 512 KB sprite
VRAM, seven of the 128 Paletted16 CRAM banks.

## The boot art load path was rewritten. Read this before touching Bitmap::TGA anywhere

The final whole-branch review found two Criticals, both confirmed by measurement. **`SRL::Bitmap::TGA` is unusable in this port** and the fix is a design change, not a tweak.

**The heap is 16,064 bytes.** `__heap_start = 0x060bc140`, `__heap_end = 0x060c0000`, from this branch's own build map. `TGA::LoadData` (`srl_tga.hpp:687`) opens with `autonew uint8_t[file->Size.Bytes + 1]` — the entire file — and then allocates the decoded pixels beside it while that buffer is still live. `BOOTLEGL.TGA` alone wants 71,705 + 35,840. It fails on the **first** file, every boot, and the `GetData() == nullptr` guard added during Task 5 turns that into a silent skip: the disc boots into the game with no opening and no menu.

LWRAM is not an escape. By the time `boot_sequence()` runs, `vm_alloc_memory` has taken 524,288 and `game2bin_alloc` 409,600 of the 1 MB, leaving ~114 KB against `BOOTTITL`'s 143 KB peak.

**`GFS_Load` writes sector-rounded into a byte-sized buffer.** `LoadBytes` hands it `Size.Bytes + 1`; every one of the six files has a partial final sector, so every one overruns — `BOOTLEGL` by 2,023 bytes. **This port already found and documented this**: `disc_srl.cxx`'s `disc_read_file_body` banner names it as "precisely the overrun refused above", which is why the disc layer reads whole sectors into a caller-owned destination. Task 5 reintroduced it by reaching for `Bitmap::TGA` instead of the port's own seam.

**This is fixed** (Task 7, `2562cb7`..`9306ae6`). `mkbootart.py` now emits a pre-packed `.ART` format — 8-byte header, `HighColor` palette, 4bpp pixels, big-endian — read whole through `disc_read_file` into one 36,864-byte LWRAM staging buffer and handed to `VDP1::TryLoadTexture`'s raw overload. No decoder, no whole-file allocation, nothing on the HWRAM heap. The title card dropped 21 colours to 16 so every screen is `Paletted16` and one buffer serves all six.

`.ART`, not `.BIN`: `.gitignore` carries `saturn/cd/data/*.bin` and git matches it case-insensitively on Windows, so `.BIN` assets would silently fail to commit.

One detail that removes a whole class of risk: `CRAM::Palette::Load` is a raw `slDMACopy`, so palette bytes never pass through the C++ bitfield — the big-endian bytes on disc land in CRAM verbatim.

Three Importants are real but moot until the above is resolved: `key_select` is never written on Saturn so `BOOT_KEY_SELECT` is dead; the 40 s music cap has no reset except the attract replay, so an active player gets permanent silence; and the attract restart snaps volume 0→7 in the same frame as `disc_play_track`, clicking at the seam the fade exists to hide.

## Nothing here has run on hardware

No build, no link, no emulator, for the whole of this work. `bootmenu.c` has 12 host-compiled tests; everything touching VDP1, CRAM or CD-DA has a `-fsyntax-only` compile and nothing else. Treat the first emulator run as the real test.

Two things are unverified by design and are one-line fixes if wrong:

- **Sprite Z ordering.** `slSetSprite` sorts far to near, so the smaller value should draw on top. `BOOT_ART_Z_BACK` is 500 and `BOOT_ART_Z_FRONT` 490. If the lit menu logo never appears, swap them.
- **Whether the six TGAs load at all.** Their headers match `ELF_S.TGA`, a file SRL ships and loads, and one was round-tripped through ffmpeg to `average:inf`. But SRL's loader has only been read, not run.

The Virgin logo sitting still for 10.2 s across two state-machine slots is **correct**, not a stuck frame. See below.

## SRL traps found by reading its source, all of which compile clean

These cost nothing to hit and everything to diagnose, because each produces wrong output rather than a diagnostic.

**`VDP1::TryLoadTexture(bitmap)` refuses every paletted bitmap.** With no palette handler it returns `-1` rather than allocating a CRAM bank itself (`srl_vdp1.hpp:246-252`). All six boot TGAs are paletted, so the original code would have failed on the first file every time and booted straight into the game with no opening and no menu — compiling cleanly and passing every check. The fix is `bootArtLoadPalette` in `saturn_bootart.cxx`: `CRAM::GetFreeBank` → `SetBankUsedState` → `CRAM::Palette::Load`, passed as `TryLoadTexture`'s second argument. **Any future paletted texture in this port needs the same.**

**`SRL::Bitmap::TGA`'s constructor does not fail on a missing file.** It calls `Debug::Assert`, which without `-DDEBUG` expands to a function whose whole body is `#ifdef DEBUG` (`srl_debug.hpp:278`) — a complete no-op. It then returns a fully constructed object whose `width` and `height` were never assigned; they are not in the initialiser list (`srl_tga.hpp:810`). Garbage dimensions then reach `slDMACopy` with a null source. `new` succeeded, so a null check on the pointer sees nothing wrong. **Check `tga->GetData() == nullptr` after construction** — `imageData` starts null and only a successful load sets it.

**`Fxp`'s floating-point constructor is `consteval`.** A helper taking `double` parameters cannot construct one, because a function parameter is never a constant expression. `bootArtSprite` takes `int16_t`. Consequence worth knowing: a fractional offset is now truncated at the call site rather than refused, which is why `mkbootart.py` pads both crop dimensions to even and why the offset constants are integers.

**`Tga::DecodeRlePaletted` skips the 4bpp packing entirely** (`srl_tga.hpp:404`) and always allocates one byte per pixel. An RLE-compressed 16-colour TGA silently doubles its VRAM and loads as 8bpp. The boot TGAs are uncompressed for this reason.

**`Tga::DecodePaletted`'s 16-colour path never terminates on an odd width.** It walks two pixels per byte and tests `xLocation == xLoop.End` (`srl_tga.hpp:353-371`). All crops are even-width.

**`BitmapInfo` picks the texture colour mode from the colour map's *length*, not its used entries.** ffmpeg's targa encoder always writes 256, which would force `Paletted256` and double VRAM on a 13-colour image. `mkbootart.py` writes TGA itself for this reason; do not replace it with ffmpeg.

## The Virgin screens are not a 3 px shift

The spec originally claimed opening screens 2 and 3 were the same art 3 px apart, inferred from ink bounding boxes `y26–214` and `y29–214`. That inference was wrong and the shared bottom edge should have given it away — a translation moves both edges. They differ in **570 pixels across four rows** (`y26–29`, `x76–243`) and are identical elsewhere: the first hold carries three rows of white along the top of the logo's frame, the second does not.

Both slots draw one texture, cut from the first hold. `mkbootart.py` fails the build if a re-capture pushes them past 2% divergence.

## The music track, by its duration

`saturn/cd/music/track03.wav`, **2:46.17** (29,312,976 raw bytes / 12,463 sectors). That duration is the identifying check because the disc carries two numberings one apart: it is disc/cue track **3**, but only the **second** audio track, since audio starts at disc track 2. Engine music index is **2** (`discfmt_cue_track_for_music` adds 1). `test_bootmenu.c` asserts the mapping.

## Verification lessons worth carrying forward

Three "checks" in the plan verified nothing, and each looked like coverage:

- `verify()` in `mkbootart.py` claimed to catch anything that would load wrong but never checked the TGA origin byte. A flipped `header[17]` draws every sprite upside down and still reports six clean files.
- A step asserting the host suite proves `disc.h` still parses — **no host test includes `disc.h`**. Replaced with `gcc -fsyntax-only -x c saturn/src/disc.h`, which also keeps that header self-contained now it has a `uint8_t` parameter.
- `test_held_button_skips_once` passed `pressed = 0` for a "held" button, which is the caller's edge arithmetic done by hand. It proved only that a frame with no edges does nothing.

The countermeasure that worked: **mutation testing**. Task 3 reversed the confirm condition, gutted `boot_ramp`, and deleted the idle-expiry block, confirming the predicted tests failed each time. Before that, all three of those breakages passed the suite green. Use it on any test written after its implementation.

## Environment

- The SH-2 compiler and SaturnRingLib are at the **repo root**, not under `saturn/`. Syntax checks run from `saturn/` with `../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe` and the flags `make -n src/<file>.o` prints.
- `make -n` is permitted (a dry run that writes nothing). Real `make`, `compile.bat` and Mednafen are not — see [[2026-08-13-death-fade-shape-and-history-squash]] and the standing convention that the human runs the build.
- ffmpeg is not on PATH; `mkbootart.py` carries the winget fallback.

## Open

- Task 6, then the final whole-branch review.
- Three minors deferred for the final whole-branch review: if `VDP1::TryLoadTexture` fails *after* `bootArtLoadPalette` has already filled a bank, SRL does not unmark that bank, so a retry through that rare double-failure path leaks CRAM banks — a pre-existing gap in SRL, not introduced here; `mkbootart.py`'s function banners omit `Dependencies`/`Globals` rather than marking them `N/A` (systemic, ~18 headers, and the repo's own practice is looser than CLAUDE.md read literally); and `verify()`'s banner says "two" unreachable checks where there are three.
- Nothing on this branch has been pushed.

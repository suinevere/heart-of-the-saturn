# Heart of The Alien → Sega Saturn — Disc Read Speed Design Spec

**Date:** 2026-08-06
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Make `disc_read_file` read at something close to what the drive can do.

Every blob the game needs arrives through one call. `game2bin_init` blocks on
409,600 bytes before the title appears, `play_animation` blocks on a whole
animation before a single frame of it decodes, and `load_room` blocks on a room
file at every room change. Those reads currently take six to seven seconds each,
which is most of what a player experiences as the port being slow.

Done means: `INTRO1.BIN` drops from 7,183 ms to roughly 1,500 ms, every room and
animation still renders correctly, and the temporary timing probe is out of the
tree.

Nothing above `disc.h` changes. No new seam, no new engine call, no change to
what any of the three call sites pass.

## The measurement that drives every decision

Measured on the emulator 2026-08-06 with the timing probe added in `be08f5f`
(`saturn/src/system/disc_srl.cxx:568`), which prints `probe read: '<name>' took
N ms` around `disc_read_file_body`:

| File | Bytes | Sectors | Measured | Throughput |
| --- | --- | --- | --- | --- |
| `GAME2.BIN` | 409,600 | 200 | 6,300 ms | 65 KB/s |
| `INTRO1.BIN` | 432,128 | 211 | 7,183 ms | 60 KB/s |
| `ROOMS1.BIN` | 370,688 | 181 | 5,800 ms | 64 KB/s |

A Saturn drive is 2x — about 307 KB/s. Single speed is 153.6 KB/s. So the port
is running at roughly a fifth of what the drive is rated for, and well under half
of single speed. That is not a drive problem and it is not a decode problem; the
probe brackets nothing but the read.

**The cause is the shape of `SRL::Cd::File::Read`, not the amount of data.**
`srl_cd.hpp:430` loops `GFS_Fread(handle, SectorsToReadAtOnce, workBuffer,
10240)` — five sectors per request, `SectorsToReadAtOnce` fixed at
`srl_cd.hpp:209` — and copies the work buffer into the caller's destination one
byte at a time between requests (`srl_cd.hpp:466`, `srl_cd.hpp:488`). Divide each
measurement by its request count and the numbers collapse onto a constant:

| File | Requests (`ceil(sectors / 5)`) | Per request |
| --- | --- | --- |
| `GAME2.BIN` | 40 | 157 ms |
| `INTRO1.BIN` | 43 | 167 ms |
| `ROOMS1.BIN` | 37 | 157 ms |

Every request costs about 160 ms regardless of anything else, while the 10,240
bytes it actually moves take 33 ms at 2x. Five sixths of every read is the drive
losing its place between requests and re-acquiring it. The byte-by-byte copy is
wasteful too, but it is noise next to this: the fix is to stop issuing forty
requests, not to copy faster.

`SRL::Cd::File::ReadSectors` (`srl_cd.hpp:509`) already does the right thing —
one `GFS_Fread` for the whole request, straight into the caller's destination,
no work buffer, no copy (`srl_cd.hpp:519`). It is in the SDK, unused by this
port, and it is what this sub-project switches to.

## Architecture

| File | Language | Responsibility |
| --- | --- | --- |
| `saturn/src/discsec.h` / `discsec.c` | C — compiles for host and SH-2 | The whole-sectors / tail-bytes split, and the maximum sector size the tail buffer is built for. Pure integer arithmetic over a byte count and a sector size. |
| `saturn/src/system/disc_srl.cxx` | C++ — the only file that touches SRL | `disc_read_file_body` issues the reads: `ReadSectors` for the whole-sector body, one more into an aligned bounce buffer for the tail. Owns the bounce buffer. |
| `saturn/src/disc.h` | C header | The `max_size` contract, strengthened to say what it now has to say. |
| `saturn/tests/test_discsec.c` | C — host only, plain `gcc` | Pins the split, including `ROOMS7.BIN`'s. |
| `SaturnRingLib/` | — | **Not modified.** |

```
disc_read_file_body
  |
  |-- discsec_whole_sectors / discsec_tail_bytes    pure, host-tested
  |
  |-- SRL::Cd::File::ReadSectors(whole, out)        one GFS_Fread, straight to the engine
  '-- SRL::Cd::File::ReadSectors(1, g_tailSector)   one GFS_Fread into 2 KB of .bss
        + memcpy of exactly the remaining bytes
```

**`SaturnRingLib` is a submodule and stays untouched.** It already shows as
modified in `git status` for reasons unrelated to this work, and adding to that
would make the existing divergence harder to unpick later. Everything here lives
in the port, calling `ReadSectors` and — only in the fallback described under
Risks — raw GFS through the public `File::Handle` member (`srl_cd.hpp:241`).

## Components

### `discsec.{c,h}` — the split (new)

A sibling of `cdtoc.{c,h}` and `cdda_classify.{c,h}`, and deliberately their
twin: pure integer arithmetic over numbers somebody else fetched, free of SRL,
stdio and every engine header, so it compiles for both host and SH-2 and gets
tested off-target in milliseconds. It carries the same `extern "C"` guard those
two do, because its only Saturn caller is C++.

Interface:

- `discsec_whole_sectors(bytes, sector_size)` — complete sectors in `bytes`
- `discsec_tail_bytes(bytes, sector_size)` — the `0 .. sector_size - 1`
  remainder
- `DISC_MAX_SECTOR_BYTES` — 2,048, the size the bounce buffer is built for

Both functions return 0 for a non-positive `sector_size` or a negative byte
count, and a caller that gets 0 from both reads nothing and fails its own length
check rather than proceeding on a guess.

This is four lines of arithmetic and it still earns its own file, for a reason
that is not obvious: **SRL already reports `Size.Sectors` and
`Size.LastSectorSize`** (`srl_cd.hpp:197`), so the split could be read straight
off the file object. It must not be. `disc_read_file_body` bounds-checks
`Size.Bytes` against `max_size` and nothing else; if it then derived the read
length from a *different* pair of numbers, a `GFS_GetFileSize` that reported
`Sectors` and `LastSectorSize` inconsistent with `Bytes` would write past the
bound that was just checked. Deriving the split from `Size.Bytes` — the one
number `max_size` was compared against — closes that, and cross-checking the
result against SRL's own pair turns any disagreement into a refusal instead of
an overrun. That cross-check is the second reason the arithmetic is worth
isolating: it is the thing a host test can pin, and a listening test cannot.

### `disc_srl.cxx` — the read (changed)

`disc_read_file_body` keeps every existing guard — `g_discOpened`,
`normalize_name`, `Exists()`, the `max_size` bound, `Open()` — and replaces the
single `file.Read(file.Size.Bytes, out)` at `disc_srl.cxx:538` with:

1. Refuse `Size.Bytes <= 0` explicitly. `File::Read` used to swallow this by
   returning −1; nothing downstream should have to infer it from a length
   mismatch.
2. Refuse `Size.SectorSize > DISC_MAX_SECTOR_BYTES`. The sector size is a
   runtime value and it indexes a fixed-size buffer; a Mode 2 disc would
   otherwise `memcpy` 2,336 bytes into 2,048. The mounted disc is Mode 1 and
   always will be, which is exactly why this guard has to be written down rather
   than assumed.
3. `whole = discsec_whole_sectors(...)`, `tail = discsec_tail_bytes(...)`.
   Refuse unless `whole + (tail ? 1 : 0) == Size.Sectors` and
   `(tail ? tail : Size.SectorSize) == Size.LastSectorSize`.
4. If `whole > 0`, `ReadSectors(whole, out)` — one request, straight into the
   engine's pointer.
5. If `tail > 0`, `ReadSectors(1, g_tailSector)` then copy exactly `tail` bytes
   to `(uint8_t *)out + whole * Size.SectorSize`.
6. `Close()`, and check the total against `Size.Bytes` as today.

The sector counts come from the file's own size and never from `max_size` or a
guess, and that is load-bearing rather than tidy: `ReadSectors` clamps its
*buffer size* to the sectors actually remaining but forwards the *unclamped*
`sectorCount` to `GFS_Fread` as the sector count (`srl_cd.hpp:515-519`). Ask it
for more sectors than the file has and GFS is handed a count larger than the
buffer it was told about. This design never can, because both counts are derived
from the size that was validated two steps earlier.

`Seek` is not used, and neither is `LoadBytes`. `Seek` (`srl_cd.hpp:538`)
allocates the same 10,240-byte work buffer this change exists to remove.
`LoadBytes` (`srl_cd.hpp:399`) would be one `GFS_Load` for the whole file — even
fewer requests — but it closes and reopens the handle around the call, and
`GFS_Load`'s destination write is sector-rounded, which is precisely the
overrun this design refuses to accept. `ReadSectors` gives the same single
request with an explicit sector count.

`disc_read_file`'s suspend/restore bracket is untouched. Nothing about CD-DA
changes here.

### The tail, and why it needs a buffer at all

Verified against `saturn/src/disc_manifest.h`: 18 of the 19 manifest blobs are
exact multiples of 2,048. `ROOMS7.BIN` (`disc_manifest.h:44`) is 160,826 bytes =
78 whole sectors + 1,082 bytes. Its last sector on the disc holds those 1,082
bytes followed by 966 bytes of whatever the mastering tool put there.

A whole-sector read of 79 sectors straight into the engine's pointer would write
all 966 of them into the emulated 68000 map, 966 bytes past the end of the room
data, at `ROOMS_LOAD_BASE + 160,826`. `disc.h` promises that will not happen.
None of the three call sites — `main.c:118`, `animation.c:919`, `game2bin.c:104`
— bounds-checks the pointer it hands in; two of them are raw addresses into a
512 KB map and the third is a fixed static array. That guarantee is the entire
reason `disc_read_file` has a `max_size` parameter, and it does not get weaker
because whole-sector reads are faster.

Hence the bounce: the final sector lands in a buffer this file owns, and exactly
`tail` bytes are copied out of it. One extra request and 1,082 bytes of copying,
for one file, once per read of that file.

### `disc.h` — the seam banner

`disc_read_file`'s banner already says why `max_size` exists. It gains one
sentence saying what a backend may do about it: `max_size` bounds **bytes
written to `out`**, not sectors read off the disc, and a backend that reads
whole sectors for speed must land the partial final sector somewhere else. That
is the non-obvious constraint a third implementer would otherwise discover the
way this one nearly did.

`disc_srl.cxx`'s own `disc_read_file_body` banner currently argues the opposite
— that `File::Read` "cannot overshoot `max_size` the way a raw `LoadBytes` into
a tight buffer could" (`disc_srl.cxx:489-492`). That sentence stops being true
the moment this change lands and must be rewritten, not left standing as a
comment that quietly contradicts the code beneath it.

## Data and control flow

`INTRO1.BIN`, 432,128 bytes, 211 sectors, no tail:

```
play_anm                                             main.c:529
  disc_play_track(31, loop=0)
  play_animation("INTRO1.BIN", 0)                    animation.c:917
    read_offset = 0x809a - 0 = 0x809a
    disc_read_file(name, memory + 0x809a, ...)
      cdda_suspend                                   unchanged
      whole = 211, tail = 0
      ReadSectors(211, memory + 0x809a)              ONE GFS_Fread
      cdda_restore                                   unchanged
```

`ROOMS7.BIN`, 160,826 bytes, 78 whole sectors + 1,082:

```
load_room(7)                                         main.c:117
  disc_read_file("ROOMS7.BIN", memory + 0xf900, ...)
    whole = 78, tail = 1082
    78 + 1 == Size.Sectors (79)          ok
    1082 == Size.LastSectorSize          ok
    ReadSectors(78, memory + 0xf900)                 GFS_Fread #1, 159,744 bytes
    ReadSectors(1,  g_tailSector)                    GFS_Fread #2, one sector
    memcpy(memory + 0xf900 + 159744, g_tailSector, 1082)
    total 160,826 == Size.Bytes          ok
```

Expected timings, from the 160 ms-per-request constant and 307 KB/s transfer:
`INTRO1.BIN` about 1,570 ms against 7,183 today; `ROOMS7.BIN` about 840 ms.

## Memory

`File::Read` allocates `SectorSize * SectorsToReadAtOnce` = 10,240 bytes on the
HWRAM heap on the first `Read` of every file (`srl_cd.hpp:441`) and frees it at
close. The checked-in linker map puts that heap at 0x060b02e0..0x060c0000 —
64,800 bytes — and `saturn_compat.cxx`'s `malloc` deliberately refuses to fall
back to LWRAM, so a caller holding 55 KB across a read turns it into a NULL
rather than a slow read. (`disc_srl.cxx`'s banner still quotes 69,440 from an
earlier build; it should be corrected to the current map figure while this file
is being edited anyway.)

Approach A removes that 10,240-byte transient entirely and replaces it with a
2,048-byte tail buffer.

**The buffer is a file-scope static, not a stack array**, declared as
`uint32_t g_tailSector[DISC_MAX_SECTOR_BYTES / 4]`. Three reasons, in order of
weight:

- **The stack cannot absorb it.** `disc_read_file` is reached through
  `main → play_anm → play_animation → disc_read_file`, several frames deep
  already, and nothing in this tree states the SH-2 master stack size. SGL's
  convention puts it below the 0x06004000 load address `sgl.linker:4` gives
  `PRELOADER`, which is single-digit kilobytes. A 2 KB frame against an
  unstated budget is not a detail; it is an overflow that would present as
  memory corruption somewhere else entirely.
- **A static is visible.** It appears in the map as `.bss` and comes out of the
  same HWRAM the heap figure above is computed from, so the cost is subtracted
  once and stays subtracted. A stack frame is invisible to the map and gets
  argued about instead of measured.
- **`uint32_t` guarantees the alignment.** GFS transfers into this buffer, and a
  `char[2048]` local has no alignment contract beyond whatever the compiler
  happens to give it. Declaring it as words makes 4-byte alignment a property of
  the type.

Net effect on peak HWRAM during a read: 2,048 bytes permanently resident instead
of 10,240 bytes transient, so the high-water mark drops by 8,192 bytes.

## Risks and mitigations

**The destination is frequently not 4-byte aligned, and whether the default GFS
transfer mode tolerates that is UNVERIFIED. This is the largest risk in the
design and it is not being claimed away.**

`ANIMATION_LOAD_BASE` is `0x809a` (`disc_manifest.h:56`), and the LWRAM block
holding the emulated map is 4-byte aligned (`srl_memory.hpp:322-328`), so an
animation read with `fileoffset 0` — `INTRO1.BIN` through `INTRO4.BIN`,
`END1.BIN` through `END4.BIN`, `MID2.BIN` — lands at `base + 2 (mod 4)`.
`MAKE2MB.BIN` uses `fileoffset 0x109a` (`main.c:62`), so its read_offset is
`0x7000` and it *is* aligned; `ROOMS_LOAD_BASE` is `0xf900`
(`disc_manifest.h:66`), so every room read is aligned too. Both cases genuinely
occur. Today's `File::Read` hides this entirely, because GFS only ever writes
into SRL's own aligned work buffer and the byte-by-byte copy that follows does
not care where it lands.

SRL calls `GFS_Init` (`srl_cd.hpp:629`) and never calls `GFS_SetTmode`, so GFS
runs in its default transfer mode. `sega_gfs.h:115-120` defines `GFS_TMODE_SCU`
(SCU DMA), `GFS_TMODE_SDMA0`, `GFS_TMODE_SDMA1`, `GFS_TMODE_CPU` and
`GFS_TMODE_STM`. Whether the default writes correctly to a 2-mod-4 destination
is not documented in this tree and has never been exercised by this port.

The alignment is a property of `ANIMATION_LOAD_BASE`, not of where the read
starts, so no amount of splitting the request fixes it — every sector of an
animation lands at the same offset mod 4.

*How the first emulator run settles it:* `INTRO1.BIN` is the first thing the
game loads into an unaligned destination, on the way into the first intro
animation, before any input is needed. That run either produces a correct
animation or an obviously broken one, immediately, with no ambiguity and nothing
to drive the game into first. **Do not claim this works until that run has
happened.**

*Mitigation if it does not:* `GFS_SetTmode(file.Handle, GFS_TMODE_CPU)`
(`sega_gfs.h:423`) before the body read, using the public `Handle` member
(`srl_cd.hpp:241`), trading DMA for a software transfer that still costs one
request instead of forty-three. That is a two-line change inside `disc_srl.cxx`,
still touches no submodule file, and is why this risk is survivable rather than
fatal to the approach.

**Emulator fidelity.** Whether Mednafen models per-request drive latency the way
a real CD block does is unknown. The 160 ms constant is what Mednafen charges;
real hardware may charge more or less. The design does not depend on the exact
number — it depends on the cost being per-request, which is the part the three
independent measurements agree on.

**Coverage past the intro.** The intro animations and `GAME2.BIN` are reachable
on every boot. Rooms need the game driven into them, which the input
sub-project has only recently made possible, so room reads and `ROOMS7.BIN`'s
tail in particular are reasoned about here and proven later. Stated rather than
implied.

**`ReadSectors` returning 0 rather than an error.** `srl_cd.hpp:511` returns 0
for a closed file, EOF, or a clamped-to-nothing request, which is
indistinguishable from a legitimate zero-length read. The total-versus-`Size.Bytes`
check at the end catches all of them, which is why that check stays exactly where
it is rather than being replaced by per-call error handling.

## Build and test

Verification, in ascending cost:

1. **`saturn/tests/test_discsec.c`**, a sibling of `test_cdda_classify.c`, added
   to `saturn/tests/run_tests.sh` in the same shape as the four suites already
   there: exact multiples of the sector size, one-byte files, `ROOMS7.BIN`'s
   real 160,826 → 78 + 1,082 split pinned by literal, a size one byte under and
   one byte over a sector boundary, and the degenerate `sector_size <= 0` and
   negative-byte-count guards. `ROOMS7.BIN`'s row is the one that matters — it
   is the only manifest file with a tail, so if the split ever silently becomes
   a round-up, that row is what fails.
2. **Host build unaffected.** `disc_cue.c` is untouched; `make -C saturn/src` and
   `sh saturn/tests/run_tests.sh` still pass. `rm -f saturn/src/*.o` between the
   host and Saturn builds — the standing object-collision trap.
3. **Saturn link and map check.** This adds 2,048 bytes of `.bss` and removes a
   10,240-byte heap transient; confirm the map's `.bss` line moved by about
   2,048 and `__heap_start`/`__heap_end` by the same, so the two numbers in
   `disc_srl.cxx`'s banner can be updated from the map rather than estimated.
4. **On the emulator, run by Suinevere.** The probe's `probe read` lines answer
   the whole question directly: `INTRO1.BIN` should fall from 7,183 ms toward
   roughly 1,500 ms, and every animation must still render correctly — the
   alignment question above is decided by this run and by nothing else.
5. **A room load.** `ROOMS7.BIN` is the only file that exercises the tail path;
   any room whose contents render correctly proves the whole-sector path, and
   `ROOMS7.BIN` specifically proves the bounce.

**The probe must not survive this sub-project.** `cdda_probe_wait`
(`disc_srl.cxx:206`) spins on `CDC_GetCurStat` for up to four seconds inside
`disc_play_track` and inside `cdda_restore`, and that blocking wait is known to
distort game-loop pacing — `main.c:459`'s `rest()` sprints to catch up after the
stall, so the frames immediately following a track change do not run at the rate
they will once the probe is gone. Any timing conclusion drawn with the probe in
place is a conclusion about a game loop that no longer exists. Both the
`cdda_probe_wait` calls and the `probe read` timing in `disc_read_file` come out
before this is considered done; the last emulator run before removal is the one
that records the numbers.

Diagnostics use `printf`, never `fprintf` — `fprintf` renders nothing on Saturn
and the cause is still unknown. One new message, not four: the split-versus-SRL
disagreement refusal. Everything else this function can fail at already prints.

New C files go beside `discfmt.c`, `cdtoc.c` and `cdda_classify.c` in
`saturn/src/`, matching the layout the port already uses. The SDK forces the
extension: `shared.mk:239` and `shared.mk:242` define pattern rules for `%.c`
and `%.cxx` only, and `saturn/makefile:50-59` records what happens to anything
else — a `.cpp` maps to no object, is silently dropped from the link, and fails
at the first missing symbol rather than at the file that was never compiled. C
for portable logic, C++ only where it touches SRL.

Every new file and every non-trivial symbol opens with the house banner —
`Description` carrying the constraint or the bug avoided rather than restating
the signature, `Author: suinevere`, `Dependencies` on files, `Globals` /
`Params` / `Returns` on symbols, `N/A` where a field does not apply.

## Deferred / stubbed

- **`GFS_TMODE` selection is not tuned.** The design runs in whatever GFS
  defaults to and only reaches for `GFS_SetTmode` as the alignment fallback.
  Whether SCU DMA versus cycle-steal versus CPU transfer is measurably faster
  for a 432 KB request is a separate question that this sub-project does not ask.
- **Streaming, overlapped or background reads.** `GFS_NwFread` (`sega_gfs.h`)
  returns immediately and would let a read overlap with decode, but every caller
  here preloads and then plays from RAM, so there is nothing to overlap with
  until an animation decoder is rewritten to consume the file as it arrives.
- **Read-ahead or caching across calls.** `ROOMS*.BIN` are re-read on every room
  change. Caching them is a memory-budget question, not a throughput one.

## Out of scope

- **The residual CD-DA start gap.** Measured this session: `CDC_ST_PLAY` is
  reported 500 ms after the play command for cue 33 and 167 ms for cue 4, but
  waiting for that flag did **not** put the intro in sync — the flag is raised
  before audio is audible, so it is the wrong signal. The likely right one is
  `SRL::Sound::Cdda::Analysis` (`srl_sound.hpp:220`), which reports real CD-DA
  output level. Its own sub-project.
- **The discarded first `PlaySingle`.** `main.c:529`'s `play_anm` calls
  `disc_play_track` and then immediately loads a whole file, so that play command
  is always killed by the read; the drive reaches `CDC_ST_PLAY` on cue 33 in
  500 ms before being cancelled. Removing it needs `play_animation` split into
  load and play halves, which this sub-project does not touch. Faster reads
  shorten the window it is wasted in but do not remove it.
- **Physical audio track reordering** — decoupling the engine music index from
  the cue track so latency-sensitive tracks sit next to the data track.
- The host backend `disc_cue.c` is not touched, so the host read path carries no
  regression risk and is not re-tested by hand.
- The `SaturnRingLib` submodule is not modified, and the `File::Read` bug it
  contains is not fixed there.

## Acceptance

1. `sh saturn/tests/run_tests.sh` passes, including the new `test_discsec` cases,
   with `ROOMS7.BIN`'s 78 + 1,082 split pinned by literal.
2. `make -C saturn/src` still builds. `disc_cue.c` is byte-identical.
3. The map shows the 10,240-byte heap transient gone and 2,048 bytes of new
   `.bss`, and `disc_srl.cxx`'s banner quotes the map's current heap figure
   rather than the stale 69,440.
4. On the emulator, `probe read: 'INTRO1.BIN'` reports roughly 1,500 ms, and all
   four intro animations render correctly — which is what settles the alignment
   question, since they are the unaligned case.
5. A room loads and renders correctly, `ROOMS7.BIN` included.
6. No `cdda_probe_wait`, no `probe read` printf, and no `platform_ticks` call
   remains in `disc_srl.cxx`.
7. No file under `SaturnRingLib/` is modified by this sub-project.

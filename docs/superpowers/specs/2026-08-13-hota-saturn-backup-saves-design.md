# Heart of The Alien → Sega Saturn — Backup RAM Saves Design Spec

Date: 2026-08-13
Status: proposed
Supersedes: nothing
Followed by: the save/load and pause menu spec, which owns every screen

## Goal

Give the Saturn build real saves in backup RAM: a slot the player can write, a
power cycle, and a load that returns both the game state and the music that was
playing.

The engine already serialises its whole state. `quicksave()` and `quickload()`
in `main.c`, with `quicksave_sprites()` and `quickload_sprites()` in
`sprites.c`, came across from the host port intact and are bound to F5 and F7
there (`saturn/host/input_sdl.c:184-189`). On Saturn they are dead in two
specific ways, and only two: `fopen` is a stub that always fails
(`saturn/src/system/saturn_filestub.c`), and nothing calls them, because the
calls lived in the SDL keyboard handler that `input_srl.cxx` replaced.

This spec makes that existing code live rather than replacing it.

## Scope

In:

- A backup RAM wrapper over SGL's BUP vector table.
- A RAM-backed `FILE` so `quicksave()`/`quickload()` reach a buffer unchanged.
- RLE compression over the staged payload.
- Slot metadata: naming, header, probing, device defaulting.
- A Saturn trailer carrying the CD-DA track, and the `disc_srl.cxx` state it
  reads from.
- A temporary pad chord to drive save and load on hardware.
- Four host test suites and a backup stub.

Out:

- Every screen. The sub-title menu, the slot list, the confirm prompt and the
  pause menu belong to the following spec, which deletes the pad chord when it
  lands.
- Chapter names. The header stores a room id; how a slot row renders it is a
  display decision for that spec.

## Prior art

`..\Another-Saturn` shipped this whole system for Another World and is the
reference. What transfers, and what does not:

| Their file | Here |
| --- | --- |
| `system/saturn_backup.{h,cxx}` | Copied near-verbatim. It is engine-agnostic by construction — `stdint.h` in, BUP out. |
| `savedata.{h,cxx}` | Adapted. Same shape; room id replaces `GAME_PART*`, and `savedataChapterName` is deferred to the menu spec. |
| `menu_state`, `menu_art`, `menu_draw`, `menu.cxx` | Not this spec. |
| `Engine::saveSlot` / `loadSlot` | The model for `savegame.c`, not a port — HOTA has no `Serializer`, it has `quicksave()`. |
| `pageRleEncode` | The precedent for compressing rather than storing raw. |

Two things in the reference are not to be inherited:

**Its `SAVE_MAX_BYTES` is declared twice with different values.** `savedata.h`
says 8192; `saturn_backup.cxx` says 2048 while its comment claims the two
"mirror" each other. That constant is the `BUP_Stat` datasize probe, so free
space is computed there for a save four times smaller than the format permits.
Here the constant is defined once and used everywhere.

**Its buffers are all in HWRAM.** `s_bupWork[0x1000]` plus an 8 KB
`s_saveBuf` is 24 KB of HWRAM BSS. This port cannot afford that; see Memory.

## Architecture

Five units, split along the line the host test harness draws: anything a test
reaches is pure C99 with no `srl.hpp` and no `sega_bup.h`.

```
run() frame top ──> savegame.c ──> saturn_savebuf.c ──> quicksave()/quickload()
                        │                                (unmodified upstream)
                        ├────────> saverle.c
                        ├────────> savedata.c ──┐
                        │                       ├──> saturn_backup.cxx ──> BUP
                        └───────────────────────┘
                        └────────> disc_srl.cxx  (track getter)
```

| Unit | Role | Host-testable |
| --- | --- | --- |
| `system/saturn_backup.{h,cxx}` | Raw BUP wrapper. The only file including `sega_bup.h`. | Date arithmetic only |
| `system/saturn_savebuf.{h,c}` | RAM-backed `FILE` over a staging buffer. | Fully |
| `saverle.{h,c}` | RLE encode/decode with an explicit decline. | Fully |
| `savedata.{h,c}` | Slot naming, header pack/unpack, probing, device defaulting. | Fully, minus BUP calls |
| `savegame.{h,c}` | Orchestration and the trailer. | Fully, against a stub |
| `disc_srl.cxx` (edit) | Remembers last track index and loop flag. | No |

## The save format

```
offset 0    [ header 48 B ]  magic, format version, room id, BUP date,
                             payload flags (raw | RLE), payload length
offset 48   [ payload     ]  raw or RLE of:
                               upstream quicksave   6150 B
                               Saturn trailer          n B
```

The uncompressed payload is 6150 bytes of upstream state plus the trailer. That
6150 is fixed and known:

| Field | Bytes |
| --- | --- |
| room, backdrop, palette | 3 |
| 256 main VM variables | 512 |
| 64 aux banks × 32 variables | 4096 |
| 64 tasks × (pc, new_pc, enabled, new_enabled) | 512 |
| 64 sprites × 16 B + 3 B list header | 1027 |

`SAVE_MAX_BYTES` is `48 + 6150 + sizeof(trailer)`, defined once in
`savedata.h`, and is the size of both staging buffers, the capacity checked
before a read, and the `BUP_Stat` datasize.

### The upstream bytes stay a contiguous prefix

The trailer is appended, never interleaved. `fputw` writes explicit big-endian
byte pairs (`common.c:68`), so those 6150 bytes are byte-identical to what the
host port's `quicksave` file contains. A host save is exactly the middle of a
Saturn save; either can be converted to the other by adding or stripping a
header and a trailer. More importantly, `quicksave()` and `quickload()` never
learn that backup RAM exists, so they stay upstream code rather than a fork.

### RLE is allowed to decline

The payload is dominated by two blocks that are mostly zeros — the 4096 bytes
of aux task variables, which is two thirds of the whole save and of which only
the tasks in `enabled_tasks` are live, and the 1024 bytes of sprite records,
most of which sit on the free list. RLE should cut this hard.

It is not guaranteed to. If the encoded form is not smaller, the flag says raw
and the uncompressed bytes are stored. There is no path where compressing makes
a save larger than not compressing.

The actual sizes are not asserted anywhere in this spec. They will be whatever
they are; `BUP_Stat` reports real free space at runtime and the write path
treats a full device as a normal outcome.

### The trailer

Carries the CD-DA track index and its loop flag, read from a new getter in
`disc_srl.cxx` that records what `disc_play_track` was last asked for. It is
also where any future Saturn-only state goes, which is the reason it is a
trailer and not three fields bolted onto the header.

## Memory

`saturn_compat.h:187-192` records that the 68000 map and the resident 400 KB
`GAME2.BIN` already do not fit — 933,888 bytes wanted against 770,048 of HWRAM
— which is why the LWRAM pool exists at all. The boot sequence then missed
SDDRVS.TSK by 366 bytes, silently. New static allocations here are not free.

**BUP work area — HWRAM BSS.** The BIOS library retains the pointer and writes
through it on its own schedule; LWRAM's 16-bit bus is the wrong place for
memory the BIOS touches without our involvement. Start at `0x1000` words, the
size the reference ships and is known to work, and shrink it only if the link
map says HWRAM cannot carry it — a measured change, never an assumed one.

**Both staging buffers — LWRAM pool.** Raw payload and compressed output, each
`SAVE_MAX_BYTES`, roughly 12.5 KB together. Each is touched exactly twice per
save: filled, then handed on. `saturn_compat.h:192` says LWRAM is for bulk
blobs and never for anything hot, which is precisely this.

Net new HWRAM cost is the work area alone.

## Where the calls happen

`sat_bup_init()` goes in `main()` immediately after `platform_init()` and
before `disc_open()`. It needs SRL up and nothing else, and placing it there
means the following spec's title menu can probe devices without a second init
point.

A save or a load happens **only at the top of a frame in `run()`**, alongside
`check_events()` at `main.c:741`, never inside the task loop. This is a
correctness requirement, not a preference: `quicksave()` opens with
`toggle_aux(0)` under the comment *"must be ran out of thread loop, so no
active thread"*, and `quickload()` does the same before calling `load_room()`.
The pad chord is sampled at that same point, so the menu spec inherits a save
point already proven correct.

### Save path

1. Stamp the header with magic, version, `current_room` and the BUP date.
2. `quicksave()` fills the raw LWRAM buffer through the RAM-backed `FILE`.
3. Append the trailer from `disc_srl`'s track getter.
4. RLE into the second buffer; keep raw if it does not shrink.
5. `sat_bup_write` with overwrite.

### Load path

1. `sat_bup_read` into the compressed buffer.
2. Validate magic and format version.
3. Decompress into the raw buffer.
4. `quickload()` reads it back through the RAM-backed `FILE`.
5. Re-issue `disc_play_track` from the trailer.

Step 5 is last for a reason. `quickload()` calls `load_room()` and
`load_room_screen()`, which read from the CD; `disc.h` requires an idle drive
and records that a read restarts a looping track. Restoring the music before
those reads would have the reads kill it.

## Error handling

Every condition below is a normal outcome that returns a code. Nothing here
panics.

| Condition | Result |
| --- | --- |
| Device absent, unformatted, write-protected | `SAT_BUP_ERR_*` through; nothing written |
| Insufficient free space | `SAT_BUP_ERR_NO_SPACE`; slot keeps its previous contents |
| Bad magic or wrong format version | Load refused; engine state untouched |
| Stored file exceeds `SAVE_MAX_BYTES` | Refused before the read, not after |
| Malformed or truncated RLE | Load refused; decode never runs past its buffer |
| RLE output not smaller than raw | Not a failure; store raw |

**The ordering rule:** nothing mutates engine state until the header validates
and the payload decompresses. A corrupt slot leaves the running game exactly as
it was, rather than half-overwritten with no way back. `test_savegame.c` exists
mainly to hold this rule in place.

## Testing

Four new suites, appended to `saturn/tests/run_tests.sh` and built the same way
as the existing eight: host gcc, `-std=c99 -Wall -Wextra -Werror -O1 -g`.

| Suite | Covers |
| --- | --- |
| `test_saverle.c` | Round-trip on all-zeros, no-runs, alternating and boundary-length inputs; encode declining on the expansion case; decode rejecting truncated and malformed input rather than overrunning |
| `test_savebuf.c` | Sequential write then read-back; `fgetc` at EOF; a write that would overrun failing rather than scribbling |
| `test_savedata.c` | Header pack/unpack round-trip; magic rejection; version rejection; slot filename generation; the device-defaulting truth table |
| `test_savegame.c` | Full save→load round-trip against `stub_saturn_backup.c`, plus every failure row above, each asserting engine-visible state is unchanged after a refused load |

`stub_saturn_backup.c` is a host stand-in for the BUP layer, modelled on the
reference repo's `tests/stub_saturn_backup.cxx`, with settable failure
injection so the table above is reachable.

### What no test reaches

`saturn_backup.cxx` past its date arithmetic, the LWRAM placement, and the pad
chord. The reference repo's ledger is blunt that every bug in its equivalent
files was found by reading or by running, never by a test. So these are
deliverables with pass criteria, not a vague "test on hardware":

1. Save to slot 0, **power-cycle**, reload. Game state and music both return.
2. Save with the internal device full. Refuses cleanly; the existing slot is
   intact and still loadable.
3. Save and load with a RAM cart present, and again with it absent.
4. Confirm from the link map that the BUP work area is in HWRAM and both
   staging buffers are in LWRAM. The SDDRVS.TSK shortfall proved that budget
   failures here are silent.
5. The save appears in the Saturn BIOS Backup Manager with a sane name, size
   and date.

## Build and test

Host tests: `sh saturn/tests/run_tests.sh` from the repo root.

Saturn build: unchanged. New `.c` files join the engine sources; the single new
`.cxx` joins the SRL backend alongside the other `system/*.cxx`.

## Decisions worth naming

### The RAM-backed `FILE` rather than a serializer refactor

`quicksave()` writes strictly forward through `fputc`/`fputw` and never seeks,
so a cursor over a buffer satisfies it completely. The alternative — replacing
`FILE*` with a stream struct across `main.c` and `sprites.c` — edits upstream
engine files, diverges the format from the host port, and rewrites code that
already works. `saturn_filestub.c`'s own header anticipates this exact
resolution: the stubs exist "only so that the engine's quicksave, key-record
and cue-lookup paths compile and link."

### The trailer rather than a wider header

Audio state could have been three more header fields. Making it a trailer keeps
the header fixed at 48 bytes across format versions and gives future
Saturn-only state somewhere to go that does not move the payload offset.

### Room id in the header, names deferred

Another World had eight named parts. HOTA has `current_room`, a byte with no
name table anywhere in the engine. Inventing one is a display decision that
belongs with the screen that renders it, so the header stores the id and the
menu spec decides whether a slot row says `ROOM 23` or something better.

### The pad chord is throwaway

It exists so the write path can be proven against a real BIOS and a real power
cycle before any menu work starts. The following spec deletes it. It is
recorded here as scope so that its removal is a planned task rather than a
forgotten one.

## Out of scope

- Every menu screen, and the artwork for them.
- Autosave of any kind. When to write is a product decision for the menu spec.
- Migrating or importing host `quicksave` files from a CD or elsewhere. The
  format makes it possible; nothing in this spec does it.
- Compressing anything other than the save payload.

## Acceptance

- `sh saturn/tests/run_tests.sh` passes, twelve suites.
- On hardware or in Mednafen: save, power-cycle, load, and both the game state
  and the CD-DA track return.
- A refused load — wrong version, corrupt payload, or missing slot — leaves the
  running game playable and unchanged.
- The link map shows the BUP work area in HWRAM and both staging buffers in
  LWRAM.
- `quicksave()`, `quickload()`, `quicksave_sprites()` and `quickload_sprites()`
  are byte-for-byte unmodified from their current state.

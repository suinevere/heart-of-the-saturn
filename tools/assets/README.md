# Heart-of-the-Saturn: asset kit

Heart of the Alien's data is not committed and is not ours to give out. These
scripts extract it from your own copy of the Sega CD disc, and package a disc
for people who do not have a checkout.

`README-kit.md` is the end-user-facing copy; CI drops it into the release zip
as `README.md`. This file is the source-checkout side.

## In a checkout

Put a rip of your disc in `tools/assets/assets/` — a `.cue` with its `.bin`
tracks beside it, or a `.7z`/`.zip` holding one — then:

```
sh tools/build.sh          # builds tools/extract_disc, once
cd tools/assets
update-build.bat           # Windows: double-click, or run from cmd
bash update-build.bat      # Linux / macOS
```

That fills `saturn/cd/data/` with the 19 blobs off the data track and
`saturn/cd/music/` with the 41 CD-DA tracks plus the `tracklist` that pins
their order — the CD skeleton `shared.mk` authors the ISO from — and then adds
Part I on top of it: `ANOTHER.BIN` from Another-Saturn's published release, and
`bank01`..`bank0d` and `memlist.bin` from the DOS release. Then build as usual:

```
cd saturn && compile.bat            # or: bash compile.bat
```

Pass `-f` to re-extract over an existing install; without it every step is a
no-op once its own files are present, so this is safe to run before each build.

Three sources, because a two-game disc has three. The makefile covers two of
them on its own — `data.bat` hangs off every build, `fetch.sh` off
`HOTA_PART1=1` — and covers the third with nothing, so a local disc could carry
Part I's program with no data behind it. That is a chainload into an engine
that panics, rather than a menu row that stays unconfirmable, and closing it is
what this script is for.

`data.bat` on its own is still the Part II step and `fetch.sh` is Part I's
program. Part I's bank files are copied into `saturn/cd/data` by hand, and
`update-build.bat` runs the first two and then checks for them.

The released kit has none of these. It ships one `run-me.bat` that installs both
games out of the two folders it carries and injects them into a prebuilt disc —
see `README-kit.md`.

## Where the game data comes from

`data.bat` looks for a `.cue` in `ASSET_DIR`, then in `RIP_DIR`. `GAME_URL`
ships blank, so finding neither it stops and says it will not download game
data. You supply the rip yourself.

Setting `GAME_URL` locally makes it fetch, unpack and cache into `ASSET_DIR`, so
the download happens once. That fetch writes to a `.part` sibling and renames
only on a clean exit — an interrupted download must not look like a complete
archive to the next run.

A source checkout never reaches the download: `RIP_DIR` points at `<repo>/cd`,
so the disc already in the tree wins. The expected shape either way is the
Redump layout `extract_disc` reads — one `.bin` per track, one `.cue` naming
them in order.

## What gets installed

The 19 blobs listed in `saturn/src/disc_manifest.h` — `INTRO1..4`, `ROOMS1..8`,
`END1..4`, `GAME2`, `MID2`, `MAKE2MB` — and the 41 audio tracks. Everything
else on the disc is either boot header or files the build supplies itself
(`0.bin`, `SDDRVS.*`, the ISO9660 boilerplate `.TXT`), so extraction leaves
those alone.

`0.bin` is deliberately excluded from the install count: it is the SEGA boot
header the build owns, not a game blob.

## Two ways the data reaches a disc

| | |
|---|---|
| **Checkout** | `update-build.bat` stages both games' files into `saturn/cd`, then the makefile authors the ISO with them already in it. No injection. |
| **Released kit / `run-me.bat`** | The disc is already built and has no data in it, so `lib/inject.sh` (or `inject.ps1`) adds the files to the existing image with `xorriso` and re-emits the MODE1/2352 track with `iso2raw`. The audio tracks are laid beside it and a matching cue is written. |

The kit's cue is per-track — one `FILE` per bin, each at `INDEX 01 00:00:00` —
rather than one interleaved image. That is the layout the source rip already
has, and it keeps the cue matched to the bins without sector arithmetic.

Injection holds the first 16 sectors — SEGA's IP.BIN boot header — across the
rewrite and verifies them afterwards; xorriso clobbers that area on commit, and
a disc without it will not boot. Rock Ridge stays off for the same reason
`shared.mk` passes `--norock`: its SUSP/PX fields bloat the directory records
past what the Saturn CD block's ISO9660 parser tolerates.

## Configuration

`CONFIG.ME` holds `ASSET_DIR`, `RIP_DIR`, `GAME_URL`, `GAME_MD5`, `CD_DIR` and
`EXTRACT_DISC` — the two search directories above among them. Relative paths
resolve against `tools/assets/`, not the caller's cwd, because `data.bat` cds to
its own directory before reading them. The kit has no `CONFIG.ME` at all —
`run-me.bat` carries its own paths, because the kit's layout is something the
release build fixes rather than something the player configures.

`GAME_MD5` is the digest of whatever `GAME_URL` fetched, checked before the
download is renamed into the cache so a truncated or substituted archive fails
there rather than later as a puzzling extractor error. Leave it blank and the
check is skipped. Both ship blank: this repository names no game data and
downloads none.

## Notes

`data.bat`, `run-me.bat` and `update-build.bat` are polyglots: `cmd.exe` reads
the leading `:` lines as labels and falls through to the Windows block, while a
POSIX shell reads `:` as the no-op builtin and runs the rest of each line. They
must stay LF-only — `.gitattributes` pins that.

The bundled Windows `xorriso` is a Cygwin build and cannot parse `C:\...`
paths; `inject.ps1` converts everything it hands over to `/cygdrive/...` form.

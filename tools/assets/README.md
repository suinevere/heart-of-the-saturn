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
data.bat                   # Windows: double-click, or run from cmd
bash data.bat              # Linux / macOS
```

That fills `saturn/cd/data/` with the 19 blobs off the data track and
`saturn/cd/music/` with the 41 CD-DA tracks plus the `tracklist` that pins
their order — the CD skeleton `shared.mk` authors the ISO from. Then build as
usual:

```
cd saturn && compile.bat            # or: bash compile.bat
```

Pass `-f` to re-extract over an existing install; without it the script is a
no-op once all 19 blobs are present.

`update.bat prep` is the same step under the name the build calls it by.
`update.bat` with no argument is the released-kit flow, not the checkout one —
it expects a prebuilt base ISO next to it.

## Where the game data comes from

`data.bat` looks for a `.cue` in `ASSET_DIR`, then in `RIP_DIR`. Finding
neither, it fetches `CONFIG.ME`'s `GAME_URL` into `ASSET_DIR`, unpacks it and
caches it there, so the download happens once. The fetch writes to a `.part`
sibling and renames only on a clean exit — an interrupted download must not
look like a complete archive to the next run.

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
| **Checkout** | `data.bat` stages the files into `saturn/cd`, then the makefile authors the ISO with them already in it. No injection. |
| **Released kit / `update.bat`** | The disc is already built and has no data in it, so `lib/inject.sh` (or `inject.ps1`) adds the files to the existing image with `xorriso` and re-emits the MODE1/2352 track with `iso2raw`. The audio tracks are laid beside it and a matching cue is written. |

The kit's cue is per-track — one `FILE` per bin, each at `INDEX 01 00:00:00` —
rather than one interleaved image. That is the layout the source rip already
has, and it keeps the cue matched to the bins without sector arithmetic.

Injection holds the first 16 sectors — SEGA's IP.BIN boot header — across the
rewrite and verifies them afterwards; xorriso clobbers that area on commit, and
a disc without it will not boot. Rock Ridge stays off for the same reason
`shared.mk` passes `--norock`: its SUSP/PX fields bloat the directory records
past what the Saturn CD block's ISO9660 parser tolerates.

## Configuration

`CONFIG.ME` holds `ASSET_DIR`, `GAME_URL`, `CD_DIR` and `EXTRACT_DISC`, and —
for the kit — `BASE_ISO`, `DISC_NAME` and `OUTPUT_DIR`. Relative paths resolve
against `tools/assets/`, not the caller's cwd.

## Notes

`data.bat` and `update.bat` are polyglots: `cmd.exe` reads the leading `:` lines
as labels and falls through to the Windows block, while a POSIX shell reads `:`
as the no-op builtin and runs the rest of each line. They must stay LF-only —
`.gitattributes` pins that.

The bundled Windows `xorriso` is a Cygwin build and cannot parse `C:\...`
paths; `inject.ps1` converts everything it hands over to `/cygdrive/...` form.

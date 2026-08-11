# Heart of the Alien for Sega Saturn — setup kit

<!-- Shipped as README.md inside the release zip. See tools/assets/README.md
     for the source-checkout side of these scripts. -->

The disc in this kit boots but has no game in it. Heart of the Alien's data
files are not ours to give you, so `update.bat` takes them from your own copy
of the Sega CD disc and folds them into the disc on your machine.

## What you need first

Nothing, if you have a working connection — `update.bat` fetches the disc image
from `GAME_URL` in `CONFIG.ME` into `assets/` and caches it there, so it only
downloads once.

To use your own copy instead, put it in `assets/` before running: one `.bin`
per track and a `.cue` naming them in order, either unpacked or still as a
`.7z`/`.zip`. Anything already in `assets/` wins over the download.

## Setup

Unzip somewhere with ~1.5 GB free, then:

- **Windows** — double-click `update.bat`
- **Linux / macOS** — `bash update.bat`

It extracts the data, injects it into the disc, lays the audio tracks beside
it, and leaves you with:

```
Heart of the Alien (USA) - Complete/
    Heart of the Alien (USA).cue            <- burn or mount this
    Heart of the Alien (USA) (Track 01).bin
    Heart of the Alien (USA) (Track 02).bin
    ...                                     <- 41 audio tracks
```

Re-running is a no-op once the data is in place; pass `-f` to redo it.

## Requirements

Windows needs nothing extra — `curl` and `tar` ship with Windows 10+, and
`xorriso` and `iso2raw` are bundled in `bin/`.

Linux and macOS need `xorriso` installed (`sudo apt-get install xorriso` /
`brew install xorriso`); `iso2raw` is bundled. Unpacking a `.7z` needs `7z` or
a `bsdtar`/`tar` built with libarchive.

## What's in here

| Path | |
|---|---|
| `update.bat` | Does everything below, in order |
| `data.bat` | Extracts the game files from your rip in `assets/` |
| `CONFIG.ME` | Paths and the `GAME_URL` the disc image is fetched from |
| `assets/` | Download cache — drop your own copy here to use it instead |
| `lib/` | Injection logic (`inject.sh` for POSIX, `inject.ps1` for Windows) |
| `bin/` | Bundled `xorriso` / `iso2raw` — see `bin/README.md` for licenses |
| `Heart of the Alien (USA)/` | The engine-only disc image |

`update.bat` and `data.bat` are polyglot scripts: the same file runs under
`cmd.exe` and under a POSIX shell.

## Notes

The kit only ever adds the 19 game blobs to the disc, at the volume root, and
writes the 41 audio tracks alongside as their own bins. The disc's SEGA boot
header is preserved byte-for-byte through the rebuild — the scripts abort
rather than emit an image that would not boot.

The cue lists one `FILE` per bin, which is the same shape your rip already has.

Avoid paths containing `'` characters; the bundled Windows `xorriso` is a
Cygwin build and its argument handling does not survive them.

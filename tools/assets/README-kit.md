# Heart of the Alien: Parts I and II for Sega Saturn

This is included as a standalone disc image and will not run on its own.
You will need to provide:

 - Redump version of the Sega CD release of Heart of the Alien: Parts I and II (multi-bin)
 - PC DOS release of Out of This World

**Please support the original authors of the games by buying the remaster's of Another World.**

## Setup

### Multi-bin rip of the Sega CD game
`Heart of the Alien comes from the Sega CD release. Put a rip of that disc in
`(put sega multi-bin and cue here)`: one `.bin` per track, plus the `.cue` that
lists them.

Out of This World comes from the PC DOS release. Put these fourteen files in
`(put bank and memlist files here)`:

```
bank01  bank02  bank03  bank04  bank05  bank06  bank07
bank08  bank09  bank0a  bank0b  bank0c  bank0d  memlist.bin
```

## Build it

- Windows: double-click `run-me.bat`
- Linux and macOS: `bash run-me.bat`

It writes the finished disc to:

```
Heart of the Alien (USA) - Complete/
    Heart of the Alien (USA).cue
    Heart of the Alien (USA) (Track 01).bin
    Heart of the Alien (USA) (Track 02).bin
    ...                                     <- 41 audio tracks
````

## Requirements

Windows needs nothing else installed.

Linux and macOS need `xorriso` installed.

## What's in here

| Path | |
|---|---|
| `run-me.bat` | The only thing you run |
| `(put sega multi-bin and cue here)/` | Your Sega CD rip goes here |
| `(put bank and memlist files here)/` | Your PC DOS data files go here |
| `Heart of the Alien (USA)/` | The disc image, before your data is added |
| `bin/` | Bundled tools. See `bin/README.md` for licenses |
| `lib/` | Disc building |
| `data/`, `music/` | Working directories |

## Acknowledgements

**hkzlab** — for the original idea.

**M-HT** — for Heart of The Alien Redux, the engine this port is built on, and
**Gil Megidish** for the original Heart of The Alien engine behind it.

**Gregory Montoir** — for the Another World engine reimplementation Part I is
built on, and **Fabien Sanglard** for the cleanup and the write-ups.

**ReyeMe** — for SaturnRingLib, which this is built against.

**The SegaXtreme forums** — for the Saturn hardware knowledge that made it
possible.

Heart of the Alien was published by Interplay. Another World, released as Out of
This World outside Europe, was created by Eric Chahi. Neither game's data is
included here.

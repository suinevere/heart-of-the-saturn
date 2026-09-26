# Heart of the Alien Redux

An open-source rewrite of the *Heart of the Alien* engine, ported to the Sega Saturn.

The Saturn build authors one disc carrying both halves of the story. *Heart of the Alien* (Part II) runs from this engine; *Out of This World* (Part I) is a separate program built from [Out of this World for Sega Saturn](https://github.com/suinevere/out-of-this-saturn) and chainloaded from the boot menu. Part I's data is optional and a disc built without it still plays Part II.

Be aware of what that costs, though. The boot menu gates the Part I row on Part I's *program*, `ANOTHER.BIN`, and not on its data: every disc built here carries the program, so the row stays selectable even with no bank files behind it and confirming it chainloads into an engine with no resources. Supply the DOS files if you want that row to lead anywhere.

Original homepage: http://hota.sourceforge.net/

The host build uses SDL2 and still runs on Linux, macOS and Windows.

## If you only want to play it

Download the setup kit from [Releases](https://github.com/suinevere/heart-of-the-saturn/releases), unzip it, put your own game files in the two folders named for them, and run `run-me.bat`. The kit ships a prebuilt disc image and injects your data into it, so none of the rest of this file applies. Nothing below is needed unless you are changing the code.

## You supply the game data

This repository carries no game data, and neither does the setup kit. You need your own copy of both releases:

- **Part II** comes from a Sega CD rip of *Heart of the Alien* as a `.cue` with its `.bin` tracks beside it. The data is not a flat archive: all 19 blobs sit on the data track and the music is 41 CD-DA tracks, so the asset step extracts them from the image rather than unzipping anything.
- **Part I** comes from the PC DOS release of *Out of This World*: `bank01` through `bank0d` and `memlist.bin`, fourteen files.

## Requirements

- Git, with SSH access to GitHub. Both submodules are pinned over SSH.
- A POSIX shell, or `cmd.exe` on Windows. The asset and build scripts are polyglots that run under both.
- A host `gcc` for the unit tests and the disc extractor
- Around 1.25 GB of disk: the filled CD skeleton is about 810 MB and the build artifacts about 420 MB, on top of the toolchain

The SH-2 cross-compiler is not a prerequisite. Step 2 below installs it.

## Setup

### 1. Clone with the submodule

```sh
git clone --recurse-submodules git@github.com:suinevere/heart-of-the-saturn.git
cd heart-of-the-saturn
```

If you already cloned without `--recurse-submodules`:

```sh
git submodule update --init --recursive
```

There are two submodules:

- [SaturnRingLib](https://github.com/ReyeMe/SaturnRingLib) at `SaturnRingLib/` supplies the SDK and the toolchain.
- [Out of this World for Sega Saturn](https://github.com/suinevere/out-of-this-saturn) is Part I's source, pinned at the commit this repository was last built with. It checks out at `Another-Saturn/`, which is the older name of that project and is still what the scripts and `ANOTHER_SATURN_DIR` expect. Only `compile_both.bat` uses it (see [Build](#build)); `compile.bat` and the release take Part I from its published release instead. A recursive clone also pulls its own SaturnRingLib, but not its toolchain, which is fine: `compile_both.bat` builds Part I with this repository's toolchain.

### 2. Install the toolchain

```sh
cd SaturnRingLib
./setup_compiler.bat      # or setup_compiler.bat on Windows
cd ..
```

This downloads the SH-2 GCC 14.2.0 cross-compiler, `iso2raw` v0.2.2 and `ftx` v0.98 into `SaturnRingLib/Compiler`. It takes a few minutes and only needs doing once.

### 3. Fill the CD skeleton

```sh
cd tools/assets
./update-build.bat        # or update-build.bat on Windows
cd ../..
```

This runs three installs, because a two-game disc has three sources:

1. Part II's blobs and music, extracted from your Sega CD rip
2. Part I's program, fetched from Another-Saturn's published release
3. A check that Part I's bank files from the DOS release are in `saturn/cd/data`

Each step is a no-op once its own files are present, so the script is safe to run before every build. Pass `-f` to force all three to refresh.

Step 1 looks for your rip in `tools/assets/assets`, then in `cd/` at the repository root. Step 3 is the optional one: copy `bank01` through `bank0d` and `memlist.bin` into `saturn/cd/data` yourself; if they are missing it says so and carries on, leaving you a Part II disc.

Paths are configured in `tools/assets/CONFIG.ME`, one `KEY=VALUE` per line. Relative paths there resolve against that file rather than your working directory. Its `GAME_URL` and `GAME_MD5` ship blank: this repository names no game data and downloads none, so step 1 reads the rip you put in one of those two directories or stops and tells you it found nothing.

## Build

```sh
cd saturn
./compile.bat             # debug, the default
./compile.bat release
./compile.bat clean
```

Artifacts land in `saturn/BuildDrop` as `Heart of the Alien (USA).elf`, `.iso`, `.bin`, `.cue` and `.map`. The `.cue` and `.bin` pair is the disc: point an emulator or a burner at the `.cue`.

Two things about `compile.bat` worth knowing before you trust its output:

**It sets `HOTA_PART1=1` unless you already have,** which downloads Part I's program from Another-Saturn's `releases/latest` on every build. If you are working on Part I locally, that download will overwrite your build with the released one. Use `compile_both.bat` below instead, or stage `ANOTHER.BIN` into `saturn/cd/data` yourself and call `make` directly. `compile.bat` is also what puts the toolchain on `PATH`, so a direct `make` has to do that too:

```sh
cd saturn
export SRL_INSTALL_ROOT=../SaturnRingLib
CDIR=$PWD/../SaturnRingLib/Compiler
export PATH="$CDIR/sh2eb-elf/bin:$CDIR/msys2/usr/bin:$CDIR/Other Utilities:$PATH"
make all HOTA_PART1=0
```

**Rebuilding over a full `BuildDrop` is safe.** Mastering replaces rather than appends: `iso2raw` truncates the `.bin` it writes, and `shared.mk` opens the `.cue` with `>` before laying the audio tracks into it. A second `compile.bat release` over a finished build leaves a disc of the same size with the same 41 audio tracks, so nothing needs deleting first. `compile.bat clean` is still how you force the SH-2 objects to rebuild.

### Building Part I from source

```sh
cd saturn
./compile_both.bat        # debug, the default
./compile_both.bat release
./compile_both.bat clean
```

`compile_both.bat` builds Part I from the `Another-Saturn/` submodule instead of downloading it. It runs that repository's own `saturn/compile.bat` with the same target, copies the program it produced, `Another-Saturn/saturn/cd/data/0.bin`, to `saturn/cd/data/ANOTHER.BIN`, then runs `compile.bat` with `HOTA_PART1=0` so nothing is fetched over it. It stops with an error if the submodule is missing, its build fails, or it produced no `0.bin`.

- **Toolchain.** If the submodule has no toolchain of its own, the script points its build at this repository's SaturnRingLib, through `SRL_INSTALL_ROOT=../../SaturnRingLib` and `SRL_COMPILER_DIR`, which Another-Saturn's `compile.bat` accepts. That is only sound while both repositories pin the same SaturnRingLib commit. If they drift apart, install a toolchain inside `Another-Saturn/SaturnRingLib` as in step 2, and the script uses that one instead.
- **Another checkout.** Set `ANOTHER_SATURN_DIR` to build Part I from a checkout other than the submodule, such as one beside this repository. That checkout has to have its own toolchain installed.
- **Moving the pin.** The submodule builds whatever its working tree holds. After changing Part I, commit and push in `Another-Saturn/`, then commit the new pointer here with `git add Another-Saturn`.
- **After changing SaturnRingLib,** run `./compile_both.bat clean` before building. SaturnRingLib keeps a cached `modules/tlsf/tlsf.o` and the makefiles do not track header changes, so without a clean build a Part I can come out differing from one built in Another-Saturn itself. After a clean build the two are byte-identical.

To check that a built disc carries the programs you think it does, extract `0.BIN` and `ANOTHER.BIN` from the `.iso` and hash them against `saturn/cd/data`. Parse the ISO9660 directory to find them; grepping the image finds false matches.

## Tests

```sh
cd saturn/tests
./run_tests.sh
```

These cover the pure logic: sector arithmetic, the ISO9660 record walk, the music track mapping, the save and menu state machines, the keymap, the checkpoint and death-site tables, and the bytecode decoder. They build with the host `gcc` and need neither the SH-2 toolchain nor a disc.

`run_tests.sh` uses `set -e`, so a compile error aborts the whole script. An aborted run and a clean run look alike if you only grep for `FAIL`. Read the exit status.

The disc extractor builds separately:

```sh
./tools/build.sh
```

## Layout

```
saturn/          Saturn port: engine sources, host tests, makefile, CD skeleton
saturn/src/      Engine and platform backends, by concern
saturn/cd/       What the disc is authored from; filled by update-build.bat
saturn/BuildDrop Build artifacts
SaturnRingLib/   SDK submodule and, after setup, the toolchain
Another-Saturn/  Part I's source, the submodule compile_both.bat builds
tools/           Asset scripts, the disc extractor, and analysis tools
```

## License

GPL-2.0-or-later: the GNU General Public License, version 2 or, at your option, any later version. The full text is in [LICENSE.md](LICENSE.md).

That covers the tree as a whole. Not every file repeats it inline: the sources inherited from Gil Megidish's original engine carry the notice in their own headers, as do the vendored GNU `getopt` files and the Scale2x/Scale3x derivations, which keep their upstream attribution. Files written for this port do not restate it.

## Credits

The engine began as Gil Megidish's *Heart of the Alien* and carries later work by M-HT, whose commits are preserved in this history. The Saturn port builds on ReyeMe's SaturnRingLib.

Buy the official remasters of *Another World* and support the people who made the originals.

# CD-DA audio on the built disc. `full` lays all 41 tracks (~425 MB, ~41 min);
# `none` builds a data-only disc of about 12 MB.
#
# Default full since the CD-DA backend landed and the data-only disc was
# verified to boot unchanged: music is now part of what this disc is for, and
# a build without it cannot be listened to. Set none for a fast iteration loop
# on video, input or gameplay -- the Saturn backend's TOC guard makes a
# data-only disc silently music-free rather than undefined, so that stays a
# safe build rather than a broken one.
#
# Matched exactly against the string "full" (below: ifneq ($(HOTA_AUDIO),full))
# -- must be lowercase, no surrounding whitespace. Anything other than "full"
# builds the 12 MB data-only disc rather than erroring, so a mistyped value
# here silently downgrades a music build to a mute one. That now costs more
# than it used to: before the default flipped, a typo landed on the intended
# behaviour anyway.
HOTA_AUDIO ?= full

# Configuration
#
# SRL_MAX_TEXTURES was 100 until the menu layer landed. The boot art holds 7
# slots permanently (saturn_bootart.cxx) and saturn_menuart.cxx holds 67 more
# -- 64 font glyphs plus three panels -- for 74 in total. 100 would still fit,
# but it leaves no room for another screen and the table costs only one
# descriptor per slot, so the ceiling goes up rather than being spent.
SRL_MAX_TEXTURES = 128          # Number of VDP1 texture slots
SRL_MODE = NTSC                 # Valid options are PAL or NTSC
SRL_HIGH_RES = 0                # 480i mode
SRL_FRAMERATE = 1               # Framerate control (0=dynamic, 1=< 60/value)
SRL_MAX_CD_BACKGROUND_JOBS = 1  # Maximum number of files GFS can open at once
SRL_MAX_CD_FILES = 256          # Maximum number of files on a CD
SRL_MAX_CD_RETRIES = 5          # Number of times to retry on unsuccessful read

# Sound driver specific configuration
SRL_USE_SGL_SOUND_DRIVER = 1    # Set to 1 if you want to use SGL sound driver, this will copy necessary files into the CD folder
SRL_ENABLE_FREQ_ANALYSIS = 0    # Set to 1 if you want to enable frequency analysis for CD audio, this will load a DSP program into effect slot 1, SGL sound driver must be enabled

# SGL configuration
#
# These two set the HWRAM heap size, which is the non-obvious part. workarea.c
# sizes SortList, SpriteBuf and Pbuffer from them; sgl.linker rounds that struct
# up to 4 KB and places it at __heap_end = 0x060EA000 - SIZEOF(WORK_AREA_DUMMY),
# so every polygon costs 84 bytes of heap and every vertex 16. At the stock
# 1500/2500 the work area is 0x2a000 and the heap is 29,440 bytes, of which TLSF
# takes 3,196 for its control block and pool header -- leaving 26,244, which is
# 366 bytes short of the 26,610-byte SDDRVS.TSK that
# SRL::Sound::Hardware::Initialize stages through `new` during Core::Initialize.
# That allocation returns NULL, nothing on the path checks it, and SND_Init
# copies the sound driver out of the boot ROM at address 0 and takes an address
# error. The symptom is a black screen before any of this port's code runs.
#
# 256/256 gives a 168,704-byte heap. The game itself still draws no sprites at
# all -- it renders entirely into VDP2 NBG0 bitmap memory by slDMACopy -- but
# the menu layer does, and SGL_MAX_POLYGONS is what bounds it.
#
# workarea.c sizes SortList[12 * (SGL_MAX_POLYGONS + 6)] and
# SpriteBuf[36 * ((SGL_MAX_POLYGONS + 6) * 2)] from this value, which at 256
# gives 262 sorted primitives per frame. saturn_menuart.cxx queues at most one
# backdrop plus MENU_LAYOUT_MAX_ITEMS (menu_layout.h, 200) = 201 of them, so it
# fits by construction; the measured worst case is nearer 130.
#
# That coupling is the thing to remember: MENU_LAYOUT_MAX_ITEMS is what has to
# stay under this ceiling, not any count in the engine. SGL does not report an
# overflow -- it drops or corrupts primitives silently -- so raise
# SGL_MAX_POLYGONS before raising MENU_LAYOUT_MAX_ITEMS, and check the heap
# against SDDRVS.TSK in the map when you do.
SGL_MAX_VERTICES = 256          # Number of vertices that can be used
SGL_MAX_POLYGONS = 256          # Number of polygons that can be used
SGL_MAX_EVENTS = 1             	# Number of events that can be used
SGL_MAX_WORKS = 1             	# Number of works that can be used

# Disk name. Drives every BuildDrop artifact basename (.elf/.iso/.bin/.cue/.map)
# and the cue's FILE line, so the build output is release-named as-is.
# Safe despite the spaces/parens: shared.mk never uses BUILD_* as a rule target,
# only inside recipes where each reference is already double-quoted.
CD_NAME = Heart of the Alien (USA)

# Directory build will be placed into
BUILD_DROP = ./BuildDrop

# SRL installation directory
SRL_INSTALL_ROOT ?= ../SaturnRingLib

# Find all .c and .cxx files.
#
# The extensions are not a style choice: shared.mk derives object names with
# $(SOURCES:.c=.o) then :.cxx=.o, and defines pattern rules for only
# `%.o : %.c` and `%.o : %.cxx`. A .cpp file would neither map to an object
# name nor have a rule to build one, so it would be dropped from the link
# without an error. The engine's upstream .cpp files were renamed .cxx when
# they moved into src/ for exactly this reason -- keep new C++ files on .cxx
# and new C files on .c.
SOURCES  = $(patsubst ./%,%,$(shell find src/ -name '*.c'))
SOURCES += $(patsubst ./%,%,$(shell find src/ -name '*.cxx'))

# Host-only translation units. scale2x/scale3x type their buffers as SDL's
# Uint8 and sound.c is built on Mix_Chunk, so none of the three compiles for
# SH-2. They stay under src/ because the host build needs them, which means
# the glob above finds them and they have to be removed by name. sound.c
# stays out because it is built on Mix_Chunk; src/system/sound_srl.cxx is the
# Saturn implementation of the same header. The pixel doublers have no Saturn
# equivalent at all.
SOURCES := $(filter-out src/scale2x.c src/scale3x.c src/sound.c,$(SOURCES))

# BYPASS_PROTECTION starts the game at GAME_PART2 (the intro) instead of
# GAME_PART1, which is the original copy-protection wheel screen -- unplayable
# without the physical code wheel. The old host CMake build defined this; the
# engine's default without it (engine.cxx: `uint16_t part = GAME_PART1`) is the
# protection screen, so leaving it undefined looks like a broken boot.
SRL_CUSTOM_CCFLAGS += -DBYPASS_PROTECTION

# Selects the Saturn arm of the allocation seam in vm.c and game2bin.c, which
# routes the two bulk buffers to LWRAM instead of leaving them in .bss. Without
# it the SH-2 build compiles cleanly and then fails to link, because the
# 1,882,592 bytes of .bss do not fit the 770,048 HWRAM offers.
#
# main.c reads it too, to drop the command-line and cue-lookup paths: a Saturn
# has no argv for getopt_long to walk and no cue sheet for opendir to find.
SRL_CUSTOM_CCFLAGS += -DHOTA_SATURN

# The vendored GNU getopt (1998, src/getopt.{c,h}) declares getopt through the
# three-state HAVE_DECL_GETOPT contract its own comment documents: undefined
# means "no autoconf run, declare it without arguments", 0 means "checked and
# not found, declare it fully prototyped". Undefined is what the host build
# gets away with, because the host never compiles getopt.c -- glibc supplies
# getopt_long. SH-2 does compile it, and under -std=c2x an empty parameter
# list means (void), so the argument-less declaration and getopt.c's own
# three-parameter definition are a hard type conflict. 0 is also the truthful
# answer here: newlib declares getopt in <getopt.h> and <unistd.h>, neither of
# which this build ever includes.
SRL_CUSTOM_CCFLAGS += -DHAVE_DECL_GETOPT=0

# src/ is flat today but will be split into per-concern subfolders as the
# Saturn backend grows. Put src/ and every subfolder on the include path so the
# engine's bare "#include \"foo.h\"" keep resolving across folders without
# per-file path edits. SRL_CUSTOM_CCFLAGS is appended to CCFLAGS by shared.mk
# for both C and C++ TUs.
SRL_CUSTOM_CCFLAGS += $(patsubst %,-I%,$(shell find src -type d))

# One-click self-repair. The 19 game blobs are not in the repository, and a disc
# authored without them boots to disc_open's "19 missing" panic rather than to
# the game. data.bat is a no-op once they are present, so hanging it off every
# build costs a directory count and removes a step people have to remember.
#
# Declared BEFORE the include on purpose: make records prerequisites in the
# order it first sees them, and shared.mk's own `all:` lines (42 and 535) would
# otherwise schedule the repair after the build that depends on it.
#
# The extractor is built here too when absent, because a fresh clone has the
# source but not the binary, and "one click" has to include that click.
#
# The self-repair is right for a checkout and wrong for a release build, which
# must author a disc with no game data in it at all. Defaults on, so nothing
# about a normal build changes; release.yml passes 0.
HOTA_ASSETS ?= 1

ifeq ($(HOTA_ASSETS),1)
all: game_assets
endif

.PHONY: game_assets
game_assets:
	@[ -x ../tools/extract_disc ] || [ -x ../tools/extract_disc.exe ] || sh ../tools/build.sh
	@sh ../tools/assets/data.bat

# Part I's program, when asked for. Declared before the include for the reason
# game_assets is: make records prerequisites in the order it first sees them,
# and shared.mk's own `all:` lines would otherwise schedule the fetch after the
# xorrisofs run that consumes what it produces.
#
# Opt-in rather than default: it reaches the network for a second game's
# published build, which is not what somebody iterating on this one wants on
# every make. Cached after the first fetch, so the cost is paid once.
HOTA_PART1 ?= 0

all: part1_assets

.PHONY: part1_assets
part1_assets:
ifeq ($(HOTA_PART1),1)
	@sh ../tools/another/fetch.sh
endif

# Include shared makefile
SDK_ROOT = $(SRL_INSTALL_ROOT)/saturnringlib
include $(SDK_ROOT)/shared.mk

.PHONY: part1_clean
part1_clean:
	rm -f $(ASSETS_DIR)/ANOTHER.BIN $(ASSETS_DIR)/OPENING.CPK

clean: part1_clean

# shared.mk:146 sets MUSIC_DIR = ./cd/music with `=`, and every use of it is
# inside a recipe, so a later assignment wins -- the same "append after the
# include" idiom this makefile already documents for LIBS. Pointing it at a
# directory whose tracklist is empty makes shared.mk report "No audio files
# found" and lay a data-only disc, with no patch to the SDK.
ifneq ($(HOTA_AUDIO),full)
MUSIC_DIR = ./cd/music-off
endif

# SEGA's PCM stream driver, for gapless audio streaming. Appended AFTER the
# include on purpose: shared.mk assigns LIBS with `=` (line 14) and would
# discard anything set before it, while the link recipe expands LIBS lazily --
# so appending here lands the library after the objects that reference it and
# inside the COFF bracket the other SGL libraries sit in.
#
# The SDK ships LIBPCM.A but does not link it, and nothing in SaturnRingLib
# uses it. See sound_srl.cxx for why the port needs it: slPCMOn is one-shot
# and cannot be driven gaplessly from frame-polled code.
LIBS += $(SGLLDIR)/LIBPCM.A

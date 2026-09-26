HOTA_AUDIO ?= full

SRL_MAX_TEXTURES = 128
SRL_MODE = NTSC
SRL_HIGH_RES = 0
SRL_FRAMERATE = 1
SRL_MAX_CD_BACKGROUND_JOBS = 1
SRL_MAX_CD_FILES = 256
SRL_MAX_CD_RETRIES = 5

SRL_USE_SGL_SOUND_DRIVER = 1
SRL_ENABLE_FREQ_ANALYSIS = 0

SGL_MAX_VERTICES = 256
SGL_MAX_POLYGONS = 256
SGL_MAX_EVENTS = 1
SGL_MAX_WORKS = 1

CD_NAME = Heart of the Alien (USA)

BUILD_DROP = ./BuildDrop

SRL_INSTALL_ROOT ?= ../SaturnRingLib

SOURCES  = $(patsubst ./%,%,$(shell find src/ -name '*.c'))
SOURCES += $(patsubst ./%,%,$(shell find src/ -name '*.cxx'))

SOURCES := $(filter-out src/scale2x.c src/scale3x.c src/sound.c,$(SOURCES))

SRL_CUSTOM_CCFLAGS += -DBYPASS_PROTECTION

SRL_CUSTOM_CCFLAGS += -DHOTA_SATURN

SRL_CUSTOM_CCFLAGS += -DHAVE_DECL_GETOPT=0

SRL_CUSTOM_CCFLAGS += $(patsubst %,-I%,$(shell find src -type d))

HOTA_ASSETS ?= 1

ifeq ($(HOTA_ASSETS),1)
all: game_assets
endif

.PHONY: game_assets
game_assets:
	@[ -x ../tools/extract_disc ] || [ -x ../tools/extract_disc.exe ] || sh ../tools/build.sh
	@sh ../tools/assets/data.bat

HOTA_PART1 ?= 0

all: part1_assets

.PHONY: part1_assets
part1_assets:
ifeq ($(HOTA_PART1),1)
	@sh ../tools/another/fetch.sh
endif

SDK_ROOT = $(SRL_INSTALL_ROOT)/saturnringlib
include $(SDK_ROOT)/shared.mk

.PHONY: part1_clean
part1_clean:
	rm -f $(ASSETS_DIR)/ANOTHER.BIN

clean: part1_clean

ifneq ($(HOTA_AUDIO),full)
MUSIC_DIR = ./cd/music-off
endif

LIBS += $(SGLLDIR)/LIBPCM.A

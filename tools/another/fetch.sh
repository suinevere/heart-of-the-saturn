#!/bin/sh
# Puts Part I's program and its data step into place. Follows tools/build.sh's
# convention -- plain sh, invoked as `sh ../tools/another/fetch.sh`, because
# the makefile already calls its helpers that way and this is never
# double-clicked the way the asset kit's polyglot .bat files are.
#
# Downloads Another-Saturn's published build rather than compiling it. The two
# programs share no linkage -- one overwrites the other at 0x06004000 and jumps
# to it -- so nothing was ever gained by building them together, while building
# cost a second SaturnRingLib checkout: shared.mk compiles preloader.cxx to
# modules/sgl/SRC/preloader.o inside the SDK tree, and our SGL_MAX_POLYGONS of
# 256 against Part I's 1500 would otherwise share one object that make declines
# to rebuild. Taking the artifact makes that hazard cease to exist rather than
# be managed, and drops the SH-2 toolchain from this path entirely.
#
# Deliberately does NOT run data.bat: our disc image must carry no gated data.
# Part I's bank files reach a player's disc through the setup kit, not through
# this script -- this only stages the script that installs them.
set -eu
cd "$(dirname "$0")"

cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
PART1_URL=$(cfg PART1_URL)
PART1_CACHE=$(cfg PART1_CACHE)

DEST=$(cd ../../saturn/cd/data && pwd)
PART1_KIT_DEST="../assets/part1"

# -f re-downloads over a populated cache, matching tools/assets/data.bat's flag
# for the same purpose. Without it this is a no-op once the files are present.
FORCE=0
case "${1:-}" in -f|--force) FORCE=1 ;; esac

mkdir -p "$PART1_CACHE"

# Written to a .part sibling and renamed only after curl exits clean, so an
# interrupted download cannot look like a complete file to the next run -- the
# same discipline tools/assets/data.bat keeps for its own fetch. --fail makes
# curl exit non-zero on a 404 instead of writing the error page to disk.
for f in 0.bin OPENING.CPK data.bat CONFIG.ME SHA256SUMS; do
    if [ "$FORCE" = "1" ] || [ ! -f "$PART1_CACHE/$f" ]; then
        echo "Part I: fetching $f"
        curl -fsSL -o "$PART1_CACHE/$f.part" "$PART1_URL/$f"
        mv -f "$PART1_CACHE/$f.part" "$PART1_CACHE/$f"
    fi
done

# The whole reason SHA256SUMS is published. Another-Saturn's release workflow
# uploads with --clobber, so an asset can be replaced in place and a tag alone
# is not a reproducible pin; the digests are. A mismatch means the release
# moved under us, which is a thing to look at rather than build through.
( cd "$PART1_CACHE" && sha256sum -c SHA256SUMS )

cp -f "$PART1_CACHE/0.bin"       "$DEST/ANOTHER.BIN"
cp -f "$PART1_CACHE/OPENING.CPK" "$DEST/OPENING.CPK"

echo "Part I: ANOTHER.BIN $(wc -c < "$DEST/ANOTHER.BIN") bytes, OPENING.CPK $(wc -c < "$DEST/OPENING.CPK") bytes"

# Stage Part I's data step into the setup kit's part1/, so update.bat has one
# place to find it whether this is a source checkout or the released kit --
# CI stages this same part1/ verbatim into the kit later, rather than each
# reaching into a source of its own. data.bat is copied unchanged, since
# tracking upstream verbatim is the point of not forking it; CONFIG.ME is
# copied with only DATA_DIR rewritten, because upstream's own value assumes
# data.bat sits at a project's own tools/assets/, two levels above that
# project's own saturn/cd/data -- staged one level deeper at
# tools/assets/part1/, it needs three ../ instead of two to land in ours.
mkdir -p "$PART1_KIT_DEST"
cp -f "$PART1_CACHE/data.bat" "$PART1_KIT_DEST/data.bat"
{
    printf '# DATA_DIR below is overridden by tools/another/fetch.sh when it\n'
    printf '# stages this file into part1/: upstream assumes tools/assets/ as\n'
    printf '# its own directory, two levels above its own saturn/cd/data; this\n'
    printf '# copy lives one level deeper, at tools/assets/part1/.\n'
    sed 's|^DATA_DIR=.*|DATA_DIR=../../../saturn/cd/data|' "$PART1_CACHE/CONFIG.ME"
} > "$PART1_KIT_DEST/CONFIG.ME"

echo "Part I: staged data step into $(cd "$PART1_KIT_DEST" && pwd)"

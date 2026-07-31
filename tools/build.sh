#!/bin/sh
# Builds the disc extractor with the host gcc. No SDL: extract_disc is a
# build-time tool, not part of the engine, and only needs discfmt.c's
# stdio-free format arithmetic -- see extract_disc.c's own banner comment
# for why it links that file directly instead of disc_cue.c. Mirrors
# saturn/tests/run_tests.sh's committed-recipe convention for the same kind
# of host-gcc-plus-discfmt.c build.
set -e
cd "$(dirname "$0")/.."
gcc -std=c99 -Wall -Wextra -O2 -Isrc -o tools/extract_disc tools/extract_disc.c src/discfmt.c

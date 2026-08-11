#!/bin/sh
# Host unit tests for the pure disc-format logic. Nothing here opens the disc,
# links SDL, or needs the SH-2 toolchain -- that is the point. Sector
# arithmetic, the ISO9660 record walk and the music track mapping all fail
# plausibly rather than loudly, producing data that decodes far enough to look
# like a decoder bug, so they are checked here in milliseconds instead of by
# playing the game.
set -e
cd "$(dirname "$0")"
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests test_discfmt.c ../src/discfmt.c
./run_tests
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_vm test_vm_memory.c ../src/vm.c
./run_tests_vm
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_cdtoc test_cdtoc.c ../src/cdtoc.c
./run_tests_cdtoc
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_cdda_classify test_cdda_classify.c ../src/cdda_classify.c
./run_tests_cdda_classify
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_discsec test_discsec.c ../src/discsec.c
./run_tests_discsec
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_sfxconv test_sfxconv.c ../src/sfxconv.c ../src/vm.c
./run_tests_sfxconv
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_fadecalc test_fadecalc.c ../src/fadecalc.c
./run_tests_fadecalc

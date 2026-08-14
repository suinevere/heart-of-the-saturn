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
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_bootmenu test_bootmenu.c ../src/bootmenu.c ../src/discfmt.c
./run_tests_bootmenu
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_saverle test_saverle.c ../src/saverle.c
./run_tests_saverle
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_savebuf test_savebuf.c ../src/savebuf.c
./run_tests_savebuf
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system \
    -o run_tests_savedata test_savedata.c stub_saturn_backup.c ../src/savedata.c
./run_tests_savedata
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system \
    -o run_tests_savegame test_savegame.c stub_saturn_backup.c \
       ../src/savegame.c ../src/savedata.c ../src/saverle.c
./run_tests_savegame
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system -I../src/menus \
    -o run_tests_menustate test_menu_state.c ../src/menus/menu_state.c
./run_tests_menustate
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system -I../src/menus \
    -o run_tests_menulayout test_menu_layout.c stub_saturn_backup.c \
       ../src/menus/menu_layout.c ../src/menus/menu_state.c ../src/savedata.c
./run_tests_menulayout
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/menus \
    -o run_tests_menuclock test_menu_clock.c ../src/menus/menu_clock.c \
       ../src/discfmt.c
./run_tests_menuclock

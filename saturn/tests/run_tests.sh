#!/bin/sh
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
    -o run_tests_bupdevmap test_bup_devmap.c ../src/system/bup_devmap.c
./run_tests_bupdevmap
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
    -o run_tests_menustate test_menu_state.c ../src/menus/menu_state.c \
       ../src/keymap.c ../src/checkpoints.c
./run_tests_menustate
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/system -I../src/menus \
    -o run_tests_menulayout test_menu_layout.c stub_saturn_backup.c \
       ../src/menus/menu_layout.c ../src/menus/menu_state.c ../src/savedata.c \
       ../src/keymap.c ../src/checkpoints.c
./run_tests_menulayout
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src -I../src/menus \
    -o run_tests_menuclock test_menu_clock.c ../src/menus/menu_clock.c \
       ../src/discfmt.c
./run_tests_menuclock
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_keymap test_keymap.c ../src/keymap.c
./run_tests_keymap
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_checkpoints test_checkpoints.c ../src/checkpoints.c
./run_tests_checkpoints

gcc -std=c99 -Wall -Wextra -O1 -g \
    -I../src \
    -o run_tests_deathsites test_deathsites.c ../src/deathsites.c
./run_tests_deathsites
gcc -std=c99 -Wall -Wextra -O1 -g \
    -I../src -I../src/system -DENABLE_DEBUG -DHOTA_SATURN \
    -o run_tests_decode test_decode_operands.c ../src/decode.c ../src/vm.c ../src/deathsites.c
./run_tests_decode

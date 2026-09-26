#!/bin/sh
set -e
cd "$(dirname "$0")/.."
gcc -std=c99 -Wall -Wextra -O2 -Isaturn/src -o tools/extract_disc tools/extract_disc.c saturn/src/discfmt.c

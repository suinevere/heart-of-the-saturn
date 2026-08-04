/*----------------------
 | stdio.h
 | Description: Shadows SaturnRingLib's modules/dummy/stdio.h for this project
 |   only, by sitting on an -I directory that saturn/makefile puts ahead of the
 |   SDK's. The dummy header is one line -- `#define printf(...) ((void)0)` --
 |   which declares no FILE, no fopen and no fprintf, and silently erases every
 |   printf call site in the build, panic() included. Every engine translation
 |   unit already says `#include <stdio.h>`; this makes that line mean the real
 |   shim instead, with no edit to the engine.
 |
 |   It shadows nothing on the host build: saturn/src/Makefile compiles with
 |   -I. -Ihost, and this directory is on neither.
 | Author: suinevere
 | Dependencies: saturn_compat.h
 ----------------------*/
#ifndef SATURN_STDIO_H
#define SATURN_STDIO_H

#include "saturn_compat.h"

#endif /* SATURN_STDIO_H */

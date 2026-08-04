/*----------------------
 | dirent.h
 | Description: Shadows newlib's <dirent.h>, which for a bare sh-elf target is
 |   a header whose only content is `#error "<dirent.h> not supported"` -- so
 |   main.c's `#include <dirent.h>` is a hard compile failure on SH-2 before
 |   any shim can help. This declares just enough of the API for
 |   find_cue_path() to compile; the stubs in saturn_filestub.c report an
 |   empty, unopenable directory, which is the truth on a machine whose only
 |   storage is a CD reached through disc.h.
 |
 |   Consequence for the Saturn entry point: find_cue_path() therefore returns
 |   NULL, and main() panics on "no disc cue file given" unless the Saturn arm
 |   supplies a cue path (any string -- disc_open ignores it) or skips the
 |   lookup. That is Task 12's business, not this header's.
 |
 |   It shadows nothing on the host build: saturn/src/Makefile compiles with
 |   -I. -Ihost, and this directory is on neither.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef SATURN_DIRENT_H
#define SATURN_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | DIR / struct dirent
 | Description: An opaque directory handle and the one entry field the engine
 |   reads. d_name is sized to hold an ISO 9660 name with its version suffix,
 |   which is the largest thing a Saturn could ever produce here.
 | Author: suinevere
 ----------------------*/
typedef struct HOTA_DIR DIR;

struct dirent
{
	char d_name[256];
};

/*----------------------
 | opendir / readdir / closedir
 | Description: Directory traversal, stubbed in saturn_filestub.c. opendir
 |   always fails, so readdir and closedir exist only to satisfy the compiler
 |   at call sites the failed opendir already guards.
 | Author: suinevere
 ----------------------*/
DIR *opendir(const char *name);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_DIRENT_H */

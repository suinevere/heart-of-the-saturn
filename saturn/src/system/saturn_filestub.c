/*----------------------
 | saturn_filestub.c
 | Description: The always-failing halves of the C runtime: stdio file access,
 |   directory traversal, atexit and getenv. A Saturn has no writable host
 |   filesystem and no environment -- game data comes off the CD through
 |   disc.h and saves belong in backup RAM -- so these exist only so that the
 |   engine's quicksave, key-record and cue-lookup paths compile and link.
 |   Nothing on a working boot reaches them; they are keyboard-only host
 |   features and a command-line fallback.
 |
 |   Plain C with no SRL dependency, which is why it is not folded into
 |   saturn_compat.cxx.
 | Author: suinevere
 | Dependencies: saturn_compat.h, dirent.h
 ----------------------*/
#include "saturn_compat.h"
#include "dirent.h"

/*----------------------
 | fopen / fclose / fread / fwrite / fseek / ftell / rewind / fgetc / fputc
 | Description: Failing stdio stubs -- fopen returns NULL, reads and writes
 |   report zero bytes, fseek and ftell return -1, fgetc reports EOF and the
 |   rest do nothing. Every caller in the engine checks fopen's result, so a
 |   NULL here surfaces as a clean "could not open" rather than a crash.
 | Author: suinevere
 ----------------------*/
FILE  *fopen(const char *path, const char *mode)           { (void)path; (void)mode; return (FILE *)0; }
int    fclose(FILE *s)                                     { (void)s; return 0; }
size_t fread(void *p, size_t sz, size_t n, FILE *s)        { (void)p; (void)sz; (void)n; (void)s; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *s) { (void)p; (void)sz; (void)n; (void)s; return 0; }
int    fseek(FILE *s, long off, int wh)                    { (void)s; (void)off; (void)wh; return -1; }
long   ftell(FILE *s)                                      { (void)s; return -1L; }
void   rewind(FILE *s)                                     { (void)s; }
int    fgetc(FILE *s)                                      { (void)s; return EOF; }
int    fputc(int c, FILE *s)                               { (void)c; (void)s; return EOF; }

/*----------------------
 | opendir / readdir / closedir
 | Description: Failing directory traversal. opendir returns NULL, which is
 |   what main.c's find_cue_path checks first, so readdir and closedir are only
 |   ever reachable through a handle that cannot exist.
 | Author: suinevere
 ----------------------*/
DIR *opendir(const char *name)   { (void)name; return (DIR *)0; }
struct dirent *readdir(DIR *dir) { (void)dir; return (struct dirent *)0; }
int closedir(DIR *dir)           { (void)dir; return -1; }

/*----------------------
 | atexit
 | Description: Accepts a handler, drops it, reports success. exit() halts in a
 |   Synchronize() loop without unwinding, so a registered handler could never
 |   run; taking newlib's atexit instead would drag its reent exit machinery in
 |   to maintain a list nothing ever walks.
 | Author: suinevere
 ----------------------*/
int atexit(void (*fn)(void)) { (void)fn; return 0; }

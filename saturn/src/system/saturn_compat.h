/*----------------------
 | saturn_compat.h
 | Description: The C-runtime surface the engine needs on SH-2 that the
 |   SaturnRingLib include path does not provide. shared.mk puts modules/dummy,
 |   modules/sgl/INC and modules/danny/INC on -I ahead of newlib, so <stdio.h>,
 |   <stdlib.h>, <string.h> and <assert.h> are not the headers they look like:
 |   the dummy <stdio.h> is nothing but `#define printf(...) ((void)0)` with no
 |   FILE and no fopen, and SGL's <stdlib.h> offers only atoi/atol/abs/qsort
 |   with no malloc and no exit. This header declares everything those two
 |   omit, and stdio.h in this directory shadows the dummy so that ordinary
 |   engine code including <stdio.h> gets this instead.
 |
 |   Corollary that binds every future file: <cstdio>, <cstring> and the rest
 |   of the <cXXX> wrappers must never be included. libstdc++'s <cstdio> dies
 |   on `using ::FILE;` before it reaches any engine code, and <cstring> hoists
 |   the entire standard set into std:: and hard-fails on strcoll/strerror/
 |   strtok/strxfrm, none of which this libc has. Use the C headers.
 |
 |   The heap, the diagnostic sinks and the LWRAM pool are defined in
 |   saturn_compat.cxx onto SRL; the file and directory stubs in
 |   saturn_filestub.c.
 | Author: suinevere
 | Dependencies: stddef.h, stdarg.h, string.h, stdlib.h, savebuf.h
 ----------------------*/
#ifndef SATURN_COMPAT_H
#define SATURN_COMPAT_H

#include <stddef.h>
#include <stdarg.h>
#include "savebuf.h"

/*----------------------
 | <string.h> / <stdlib.h> pull-in
 | Description: SGL's headers carry no extern "C" guard of their own. Included
 |   from a .cxx they declare strlen/memset/memcpy with C++ linkage, every call
 |   site then emits a mangled `memset(void*, int, unsigned int)` that no SGL
 |   library defines, and the link fails across nearly every object. Pulling
 |   them in here, wrapped, means any Saturn backend that includes this header
 |   gets them with the right linkage and never has to remember the wrapper.
 |   SRL does the same thing for itself in srl_base.hpp; this covers the files
 |   that do not include <srl.hpp>.
 | Author: suinevere
 ----------------------*/
#ifdef __cplusplus
extern "C" {
#include <string.h>
#include <stdlib.h>
}
#else
#include <string.h>
#include <stdlib.h>
#endif

/*----------------------
 | EOF
 | Description: The end-of-stream sentinel fgetc reports. The dummy <stdio.h>
 |   defines no such thing and main.c's read_keys_from_record compares against
 |   it.
 | Author: suinevere
 ----------------------*/
#ifndef EOF
#define EOF (-1)
#endif

/*----------------------
 | SEEK_SET / SEEK_CUR / SEEK_END
 | Description: The fseek whence constants the dummy <stdio.h> omits. Nothing
 |   in the engine seeks today, but fseek is part of the FILE surface below and
 |   a partial one would be a trap for whoever needs it next.
 | Author: suinevere
 ----------------------*/
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

/*----------------------
 | fflush
 | Description: Compiled away, deliberately as a macro rather than a function.
 |   Defining an fflush() symbol collides at link time: newlib really has one
 |   and its own stdio pulls that object in, so two strong definitions meet
 |   ("multiple definition of `fflush'"). As a macro, main.c's fflush(stdout)
 |   disappears at the call site instead, and newlib's version is never handed
 |   one of the fake FILE handles below.
 | Author: suinevere
 ----------------------*/
#define fflush(stream) ((void)0)

/*----------------------
 | getenv
 | Description: Always NULL -- there is no environment on a Saturn -- and a
 |   macro rather than a function for two reasons. newlib's getenv resolves
 |   through an `environ` nothing in this build populates. And getopt.c, its
 |   only caller, carries the 1998 GNU declaration `extern char *getenv ();`
 |   behind an `#ifndef getenv` guard; C23 reads that empty parameter list as
 |   (void), so a real prototype here collides with it and the one-argument
 |   call below it fails. As a macro the guard suppresses that declaration and
 |   the POSIXLY_CORRECT lookup folds to NULL at the call site, which is the
 |   correct answer anyway.
 | Author: suinevere
 ----------------------*/
#define getenv(name) ((char *)0)

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | FILE + fopen/fclose/fread/fwrite/fseek/ftell/rewind/fgetc/fputc
 | Description: A minimal FILE type and an always-failing file API, so that
 |   common.h's fgetw/fputw, sprites.h's quicksave/quickload and main.c's
 |   record-and-replay compile and link on Saturn. The Saturn has no writable
 |   host filesystem: game data comes off the CD through disc.h and saves
 |   belong in backup RAM, so every one of these fails by design (stubs in
 |   saturn_filestub.c). They exist to keep those code paths intact until a
 |   backup-RAM save backend replaces them -- nothing on a working boot should
 |   reach them, because quicksave and record are keyboard-only host features.
 | Author: suinevere
 ----------------------*/
typedef struct HOTA_FILE FILE;
FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int    fseek(FILE *stream, long offset, int whence);
long   ftell(FILE *stream);
void   rewind(FILE *stream);
int    fgetc(FILE *stream);
int    fputc(int c, FILE *stream);

/*----------------------
 | stdout / stderr
 | Description: Opaque non-NULL stream handles, defined in saturn_compat.cxx.
 |   They are never dereferenced -- they exist so `fprintf(stderr, ...)` in
 |   main.c has something to pass and so a stream argument can be told apart
 |   from NULL.
 | Author: suinevere
 ----------------------*/
extern FILE *stdout;
extern FILE *stderr;

/*----------------------
 | printf / fprintf / puts / perror / sprintf / snprintf / vsprintf / vsnprintf
 | Description: The engine's diagnostic channel. sprintf/snprintf/vsprintf/
 |   vsnprintf are real and link from newlib -- they build strings rather than
 |   writing to a stream. printf/fprintf/puts/perror are defined in
 |   saturn_compat.cxx onto SRL's debug text layer, which is what makes
 |   common.c's panic() visible instead of a silent freeze. They are declared
 |   here rather than taken from the dummy <stdio.h> because that header
 |   defines printf as a do-nothing macro, which would erase every panic
 |   message in the build at its call site.
 | Author: suinevere
 ----------------------*/
int  printf(const char *fmt, ...);
int  fprintf(FILE *stream, const char *fmt, ...);
int  puts(const char *s);
void perror(const char *s);
int  sprintf(char *str, const char *fmt, ...);
int  snprintf(char *str, size_t size, const char *fmt, ...);
int  vsprintf(char *str, const char *fmt, va_list ap);
int  vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

/*----------------------
 | malloc / free / realloc
 | Description: The process-wide C heap, defined in saturn_compat.cxx onto
 |   SRL's High Work RAM arena. Declared here because SGL's <stdlib.h> shadows
 |   newlib's and offers none of them, and because newlib's own malloc needs an
 |   sbrk that shared.mk's -specs=nosys.specs does not provide.
 |
 |   Ordering constraint that falls out of this: the arena does not exist until
 |   SRL::Core::Initialize() runs at the top of platform_init(), so no global
 |   or static object whose constructor allocates may exist in this build.
 | Author: suinevere
 ----------------------*/
void  *malloc(size_t size);
void   free(void *ptr);
void  *realloc(void *ptr, size_t size);

/*----------------------
 | saturn_lwram_alloc / saturn_lwram_free
 | Description: Allocates from the 1 MB Low Work RAM pool rather than the main
 |   arena. Exists because the engine's two bulk buffers -- the 512 KB emulated
 |   68000 map and the 400 KB resident GAME2.BIN -- do not fit in HWRAM
 |   alongside the code and the framebuffers: measured .bss is 1,882,592 bytes
 |   against 770,048 of HWRAM. Together these two need 933,888 of the
 |   1,048,576-byte pool, which fits with its allocator headers, so they are
 |   ordinary allocations and nothing has to be reserved by address.
 |   LWRAM is a 16-bit bus, so only bulk blobs belong here -- never anything
 |   the rasterizer touches per pixel.
 | Author: suinevere
 ----------------------*/
void *saturn_lwram_alloc(unsigned long size);
void  saturn_lwram_free(void *p);

/*----------------------
 | saturn_savebuf_bind
 | Description: Installs the storage fopen will wrap, and whether the next
 |   open is for writing.
 | Author: suinevere
 | Dependencies: savebuf.h
 | Globals: N/A
 | Params: data -- storage, in LWRAM; cap -- its capacity
 | Returns: N/A
 ----------------------*/
void saturn_savebuf_bind(unsigned char *data, int cap);

/*----------------------
 | saturn_savebuf_stream
 | Description: The buffer behind the last stream, so a caller can read back
 |   how many bytes quicksave wrote, or whether an overflow was refused.
 | Author: suinevere
 | Dependencies: savebuf.h
 | Globals: N/A
 | Params: N/A
 | Returns: the buffer, always non-NULL
 ----------------------*/
savebuf *saturn_savebuf_stream(void);

/*----------------------
 | saturn_savebuf_set_length
 | Description: Declares the readable length for the next stream opened for
 |   reading. quickload opens for reading, and the readable length is the
 |   decompressed payload's, which only the caller knows. fopen clamps this to
 |   the bound storage's capacity, so a length longer than the buffer cannot
 |   make fgetc read past its end.
 | Author: suinevere
 | Dependencies: savebuf.h
 | Globals: N/A
 | Params: len -- readable length
 | Returns: N/A
 ----------------------*/
void saturn_savebuf_set_length(int len);

/*----------------------
 | exit
 | Description: Halts. There is no OS to return an exit status to, so the
 |   Saturn implementation parks in a Synchronize() loop forever rather than
 |   unwinding. Declared here because SGL's <stdlib.h> has no exit. common.c's
 |   panic() and main.c's leave_game() both end here. Marked noreturn so
 |   callers of panic() are understood not to continue.
 | Author: suinevere
 ----------------------*/
void exit(int status) __attribute__((noreturn));

/*----------------------
 | atexit
 | Description: Accepts a handler and drops it, in saturn_filestub.c. exit()
 |   above halts without unwinding, so a registered handler could never run;
 |   taking newlib's atexit instead would drag in its reent exit machinery to
 |   maintain a list nothing ever walks. main.c registers atexit_callback,
 |   whose disc_stop_track/disc_close/platform_quit are all no-ops or safe to
 |   skip on a machine that is being powered off rather than returned to a
 |   shell.
 | Author: suinevere
 ----------------------*/
int atexit(void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* SATURN_COMPAT_H */

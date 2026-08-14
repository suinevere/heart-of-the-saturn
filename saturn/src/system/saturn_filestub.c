/*----------------------
 | saturn_filestub.c
 | Description: The file half is real for one path: fopen("quicksave", ...)
 |   through fgetc/fputc reaches a savebuf cursor over caller-supplied storage,
 |   which is how quicksave()/quickload() reach backup RAM without being
 |   modified. Every other path fails, and directory traversal, atexit and
 |   getenv are unchanged: a Saturn has no writable host filesystem beyond
 |   this one buffer and no environment -- game data comes off the CD through
 |   disc.h.
 |
 |   Plain C with no SRL dependency, which is why it is not folded into
 |   saturn_compat.cxx.
 | Author: suinevere
 | Dependencies: saturn_compat.h, dirent.h, savebuf.h
 ----------------------*/
#include "saturn_compat.h"
#include "dirent.h"
#include "savebuf.h"

/*----------------------
 | SAVE_STREAM_NAME
 | Description: The one path fopen accepts. main.c's QUICKSAVE_FILENAME, which
 |   is the only file the engine ever opens on Saturn.
 | Author: suinevere
 ----------------------*/
#define SAVE_STREAM_NAME "quicksave"

/*----------------------
 | HOTA_FILE
 | Description: The FILE saturn_compat.h forward-declares. One instance
 |   exists, because the engine opens at most one stream at a time and does so
 |   only between frames.
 | Author: suinevere
 ----------------------*/
struct HOTA_FILE {
    savebuf buf;
    int open;
};

/*----------------------
 | s_saveStream
 | Description: The single stream fopen hands out, and the buffer
 |   saturn_saveslot.cxx reads the written length back from.
 | Author: suinevere
 ----------------------*/
static struct HOTA_FILE s_saveStream;

/*----------------------
 | s_saveStorage / s_saveStorageCap
 | Description: Where the stream's bytes go, installed by
 |   saturn_savebuf_bind before the engine opens anything. NULL until then, so
 |   an fopen before the binding fails the way the old stub did.
 | Author: suinevere
 ----------------------*/
static unsigned char *s_saveStorage;
static int s_saveStorageCap;

/*----------------------
 | saturn_savebuf_bind
 | Description: Installs the storage fopen will wrap, and whether the next
 |   open is for writing.
 | Author: suinevere
 | Dependencies: savebuf.h
 | Globals: s_saveStorage, s_saveStorageCap
 | Params: data -- storage, in LWRAM; cap -- its capacity
 | Returns: N/A
 ----------------------*/
void saturn_savebuf_bind(unsigned char *data, int cap)
{
    s_saveStorage = data;
    s_saveStorageCap = cap;
}

/*----------------------
 | saturn_savebuf_stream
 | Description: The buffer behind the last stream, so a caller can read back
 |   how many bytes quicksave wrote, or whether an overflow was refused.
 | Author: suinevere
 | Dependencies: savebuf.h
 | Globals: s_saveStream
 | Params: N/A
 | Returns: the buffer, always non-NULL
 ----------------------*/
savebuf *saturn_savebuf_stream(void)
{
    return &s_saveStream.buf;
}

/*----------------------
 | s_saveReadLen
 | Description: How many bytes a read stream may hand out. quickload opens for
 |   reading, and the readable length is the decompressed payload's, which only
 |   the caller knows.
 | Author: suinevere
 ----------------------*/
static int s_saveReadLen;

/*----------------------
 | saturn_savebuf_set_length
 | Description: Declares that readable length before the next open.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: s_saveReadLen
 | Params: len -- readable length
 | Returns: N/A
 ----------------------*/
void saturn_savebuf_set_length(int len)
{
    s_saveReadLen = len;
}

/*----------------------
 | fopen / fclose / fread / fwrite / fseek / ftell / rewind / fgetc / fputc
 | Description: The file half of the C runtime, backed by savebuf for the one
 |   path the engine opens and failing for every other. fread, fwrite, fseek,
 |   ftell and rewind stay failing stubs: quicksave and quickload go through
 |   fgetc and fputc only, by way of common.c's fgetw and fputw.
 | Author: suinevere
 ----------------------*/
FILE *fopen(const char *path, const char *mode)
{
    int writing;
    int i;

    if (s_saveStorage == (unsigned char *)0 || s_saveStream.open) {
        return (FILE *)0;
    }
    for (i = 0; SAVE_STREAM_NAME[i] != 0; i++) {
        if (path[i] != SAVE_STREAM_NAME[i]) {
            return (FILE *)0;
        }
    }
    if (path[i] != 0) {
        return (FILE *)0;
    }

    writing = (mode[0] == 'w');
    if (writing) {
        savebuf_open_write(&s_saveStream.buf, s_saveStorage, s_saveStorageCap);
    } else {
        int len = s_saveReadLen;
        if (len > s_saveStorageCap) {
            len = s_saveStorageCap;
        }
        savebuf_open_read(&s_saveStream.buf, s_saveStorage, len);
    }
    s_saveStream.open = 1;
    return &s_saveStream;
}

int fclose(FILE *s)
{
    if (s == &s_saveStream) {
        s_saveStream.open = 0;
    }
    return 0;
}

size_t fread(void *p, size_t sz, size_t n, FILE *s)        { (void)p; (void)sz; (void)n; (void)s; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *s) { (void)p; (void)sz; (void)n; (void)s; return 0; }
int    fseek(FILE *s, long off, int wh)                    { (void)s; (void)off; (void)wh; return -1; }
long   ftell(FILE *s)                                      { (void)s; return -1L; }
void   rewind(FILE *s)                                     { (void)s; }

int fgetc(FILE *s)
{
    if (s != &s_saveStream) {
        return EOF;
    }
    return savebuf_getc(&s_saveStream.buf);
}

int fputc(int c, FILE *s)
{
    if (s != &s_saveStream) {
        return EOF;
    }
    return savebuf_putc(&s_saveStream.buf, c);
}

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

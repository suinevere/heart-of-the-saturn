#include "saturn_compat.h"
#include "dirent.h"
#include "savebuf.h"

#define SAVE_STREAM_NAME "quicksave"

struct HOTA_FILE {
    savebuf buf;
    int open;
};

static struct HOTA_FILE s_saveStream;

static unsigned char *s_saveStorage;
static int s_saveStorageCap;

void saturn_savebuf_bind(unsigned char *data, int cap)
{
    s_saveStorage = data;
    s_saveStorageCap = cap;
}

savebuf *saturn_savebuf_stream(void)
{
    return &s_saveStream.buf;
}

void saturn_savebuf_reset(void)
{
    s_saveStream.buf.len = 0;
    s_saveStream.buf.pos = 0;
    s_saveStream.buf.err = 0;
}

static int s_saveReadLen;

void saturn_savebuf_set_length(int len)
{
    s_saveReadLen = len;
}

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

DIR *opendir(const char *name)   { (void)name; return (DIR *)0; }
struct dirent *readdir(DIR *dir) { (void)dir; return (struct dirent *)0; }
int closedir(DIR *dir)           { (void)dir; return -1; }

int atexit(void (*fn)(void)) { (void)fn; return 0; }

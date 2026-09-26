#ifndef SAVEBUF_H
#define SAVEBUF_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *data;
    int cap;
    int len;
    int pos;
    int writing;
    int err;
} savebuf;

void savebuf_open_write(savebuf *b, unsigned char *data, int cap);

void savebuf_open_read(savebuf *b, const unsigned char *data, int len);

int savebuf_putc(savebuf *b, int c);

int savebuf_getc(savebuf *b);

int savebuf_len(const savebuf *b);

int savebuf_error(const savebuf *b);

#ifdef __cplusplus
}
#endif

#endif

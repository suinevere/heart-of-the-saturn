/*----------------------
 | savebuf.c
 | Description: The cursor described by savebuf.h.
 | Author: suinevere
 | Dependencies: savebuf.h
 ----------------------*/
#include "savebuf.h"

void savebuf_open_write(savebuf *b, unsigned char *data, int cap)
{
    b->data = data;
    b->cap = cap;
    b->len = 0;
    b->pos = 0;
    b->writing = 1;
    b->err = 0;
}

void savebuf_open_read(savebuf *b, const unsigned char *data, int len)
{
    b->data = (unsigned char *)data;
    b->cap = len;
    b->len = len;
    b->pos = 0;
    b->writing = 0;
    b->err = 0;
}

int savebuf_putc(savebuf *b, int c)
{
    if (!b->writing || b->pos >= b->cap) {
        b->err = 1;
        return -1;
    }
    b->data[b->pos++] = (unsigned char)(c & 0xFF);
    b->len = b->pos;
    return c & 0xFF;
}

int savebuf_getc(savebuf *b)
{
    if (b->writing || b->pos >= b->len) {
        b->err = 1;
        return -1;
    }
    return (int)b->data[b->pos++];
}

int savebuf_len(const savebuf *b)
{
    return b->len;
}

int savebuf_error(const savebuf *b)
{
    return b->err;
}

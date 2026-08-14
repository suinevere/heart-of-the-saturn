/*----------------------
 | savebuf.h
 | Description: A sequential cursor over a byte buffer, with the one-byte-at-
 |   a-time shape quicksave() and quickload() already use through fputc and
 |   fgetc. Deliberately not named after stdio: the host tests link real libc,
 |   so a module claiming fopen could not be built by run_tests.sh.
 |   saturn_filestub.c maps the stdio names onto this on Saturn.
 |
 |   Pure C with no engine, SRL or SGL dependency.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SAVEBUF_H
#define SAVEBUF_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | savebuf
 | Description: One open buffer. A buffer is either writing or reading, never
 |   both, and err latches on the first refusal so a caller may check once at
 |   the end rather than after every byte.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char *data;
    int cap;
    int len;
    int pos;
    int writing;
    int err;
} savebuf;

/*----------------------
 | savebuf_open_write
 | Description: Opens a buffer for writing from position zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer to initialise; data -- storage; cap -- its capacity
 | Returns: N/A
 ----------------------*/
void savebuf_open_write(savebuf *b, unsigned char *data, int cap);

/*----------------------
 | savebuf_open_read
 | Description: Opens a buffer for reading from position zero.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer to initialise; data -- storage; len -- how many bytes
 |         are readable
 | Returns: N/A
 ----------------------*/
void savebuf_open_read(savebuf *b, const unsigned char *data, int len);

/*----------------------
 | savebuf_putc
 | Description: Appends one byte, masked to eight bits.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer; c -- value
 | Returns: the byte written, or -1 if the buffer is full or is a read buffer,
 |          in which case err is set and nothing is written
 ----------------------*/
int savebuf_putc(savebuf *b, int c);

/*----------------------
 | savebuf_getc
 | Description: Reads the next byte.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer
 | Returns: the byte, or -1 at end of data or on a write buffer, in which case
 |          err is set
 ----------------------*/
int savebuf_getc(savebuf *b);

/*----------------------
 | savebuf_len
 | Description: How many bytes have been written, or are readable.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer
 | Returns: the length
 ----------------------*/
int savebuf_len(const savebuf *b);

/*----------------------
 | savebuf_error
 | Description: Whether any operation on this buffer has been refused.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: b -- buffer
 | Returns: non-zero once a refusal has happened
 ----------------------*/
int savebuf_error(const savebuf *b);

#ifdef __cplusplus
}
#endif

#endif /* SAVEBUF_H */

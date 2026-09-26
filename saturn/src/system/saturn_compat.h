#ifndef SATURN_COMPAT_H
#define SATURN_COMPAT_H

#include <stddef.h>
#include <stdarg.h>
#include "savebuf.h"

#ifdef __cplusplus
extern "C" {
#include <string.h>
#include <stdlib.h>
}
#else
#include <string.h>
#include <stdlib.h>
#endif

#ifndef EOF
#define EOF (-1)
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#define fflush(stream) ((void)0)

#define getenv(name) ((char *)0)

#ifdef __cplusplus
extern "C" {
#endif

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

extern FILE *stdout;
extern FILE *stderr;

int  printf(const char *fmt, ...);
int  fprintf(FILE *stream, const char *fmt, ...);

void diag_row_pin(int row);

#ifndef SATURN_DIAG
#define SATURN_DIAG 0
#endif
int  puts(const char *s);
void perror(const char *s);
int  sprintf(char *str, const char *fmt, ...);
int  snprintf(char *str, size_t size, const char *fmt, ...);
int  vsprintf(char *str, const char *fmt, va_list ap);
int  vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

void  *malloc(size_t size);
void   free(void *ptr);
void  *realloc(void *ptr, size_t size);

void *saturn_lwram_alloc(unsigned long size);
void  saturn_lwram_free(void *p);

void saturn_savebuf_bind(unsigned char *data, int cap);

savebuf *saturn_savebuf_stream(void);

void saturn_savebuf_set_length(int len);

void saturn_savebuf_reset(void);

void exit(int status) __attribute__((noreturn));

int atexit(void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif

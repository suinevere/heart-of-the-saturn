#include <srl.hpp>
#include "saturn_compat.h"

#define DIAG_COLS 40

#define DIAG_ROWS 27

static int g_diagRow = 0;

struct HOTA_FILE
{
	int tag;
};

static HOTA_FILE g_stdoutTag = { 0 };
static HOTA_FILE g_stderrTag = { 1 };

FILE *stdout = &g_stdoutTag;
FILE *stderr = &g_stderrTag;

static int g_diagPin = -1;

extern "C" void diag_row_pin(int row)
{
	g_diagPin = (row >= 0 && row < DIAG_ROWS) ? row : -1;
}

static void diag_write(char *line)
{
	for (int i = 0; line[i] != '\0'; i++)
	{
		if (line[i] == '\n' || line[i] == '\r')
		{
			line[i] = '\0';
			break;
		}
	}

	if (g_diagPin >= 0)
	{
		SRL::Debug::Print(0, (unsigned char)g_diagPin, line);
		return;
	}

	SRL::Debug::Print(0, (unsigned char)g_diagRow, line);
	g_diagRow++;

	if (g_diagRow >= DIAG_ROWS)
	{
		g_diagRow = 0;
	}
}

static void diag_emit(const char *fmt, va_list ap)
{
	char line[DIAG_COLS + 1];

	vsnprintf(line, sizeof(line), fmt, ap);
	diag_write(line);
}

extern "C" int printf(const char *fmt, ...)
{
#if SATURN_DIAG
	va_list ap;

	va_start(ap, fmt);
	diag_emit(fmt, ap);
	va_end(ap);
#else
	(void)fmt;
#endif
	return 0;
}

extern "C" int fprintf(FILE *stream, const char *fmt, ...)
{
	va_list ap;

	(void)stream;
	va_start(ap, fmt);
	diag_emit(fmt, ap);
	va_end(ap);
	return 0;
}

extern "C" int puts(const char *s)
{
	char line[DIAG_COLS + 1];
	int i;

	for (i = 0; i < DIAG_COLS && s[i] != '\0'; i++)
	{
		line[i] = s[i];
	}

	line[i] = '\0';
	diag_write(line);
	return 0;
}

extern "C" void perror(const char *s)
{
	puts(s);
}

extern "C" void *malloc(size_t size)
{
	return SRL::Memory::HighWorkRam::Malloc(size);
}

extern "C" void free(void *ptr)
{
	if (ptr == nullptr)
	{
		return;
	}

	if (SRL::Memory::LowWorkRam::InRange(ptr))
	{
		SRL::Memory::LowWorkRam::Free(ptr);
	}
	else
	{
		SRL::Memory::HighWorkRam::Free(ptr);
	}
}

extern "C" void *realloc(void *ptr, size_t size)
{
	if (ptr == nullptr)
	{
		return malloc(size);
	}

	if (SRL::Memory::LowWorkRam::InRange(ptr))
	{
		return SRL::Memory::LowWorkRam::Realloc(ptr, size);
	}

	return SRL::Memory::HighWorkRam::Realloc(ptr, size);
}

extern "C" void *saturn_lwram_alloc(unsigned long size)
{
	return SRL::Memory::LowWorkRam::Malloc((size_t)size);
}

extern "C" void saturn_lwram_free(void *p)
{
	if (p != nullptr)
	{
		SRL::Memory::LowWorkRam::Free(p);
	}
}

extern "C" void exit(int status)
{
	(void)status;

	while (true)
	{
		SRL::Core::Synchronize();
	}
}

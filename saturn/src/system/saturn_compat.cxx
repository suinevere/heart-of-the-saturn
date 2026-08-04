/*----------------------
 | saturn_compat.cxx
 | Description: Routes the process-wide C heap onto SRL's High Work RAM arena,
 |   exposes the Low Work RAM pool the engine's two bulk buffers need, and
 |   points the diagnostic sinks declared in saturn_compat.h at SRL's debug
 |   text layer.
 |
 |   Nothing here is valid before SRL::Core::Initialize() runs at the top of
 |   platform_init(): the arenas do not exist and the text layer is not up. So
 |   no global or static object whose constructor allocates may be added to
 |   this build -- it would call this malloc before there is a heap and fault.
 |
 |   <srl.hpp> comes first and saturn_compat.h second, which is the order that
 |   keeps trap 4 shut: srl_base.hpp already pulls SGL's unguarded <string.h>
 |   and <stdlib.h> inside its own extern "C", so saturn_compat.h's wrapped
 |   copies are no-ops here rather than a second, C++-linkage set.
 | Author: suinevere
 | Dependencies: srl.hpp, saturn_compat.h
 ----------------------*/
#include <srl.hpp>
#include "saturn_compat.h"

/*----------------------
 | DIAG_COLS
 | Description: Characters per diagnostic line. SRL's debug layer is a 40x28
 |   text grid in NTSC 320x224, so anything past column 40 would be written off
 |   the row.
 | Author: suinevere
 ----------------------*/
#define DIAG_COLS 40

/*----------------------
 | DIAG_ROWS
 | Description: Diagnostic lines before the log wraps back to the top. 27 of
 |   the 28 rows, leaving the last one clear of the overscan most CRTs eat.
 | Author: suinevere
 ----------------------*/
#define DIAG_ROWS 27

/*----------------------
 | g_diagRow
 | Description: Next debug-layer row diag_emit will write, cycling 0..DIAG_ROWS.
 | Author: suinevere
 ----------------------*/
static int g_diagRow = 0;

/*----------------------
 | struct HOTA_FILE
 | Description: Completes the type saturn_compat.h leaves opaque, so that the
 |   two stream tags below can be objects of it. Their addresses are then a
 |   constant expression and stdout/stderr are statically initialized -- a
 |   reinterpret_cast of an int's address would not be, and would leave the two
 |   handles to a global constructor that runs before SRL::Core::Initialize().
 |   The member exists only to give the type a size.
 | Author: suinevere
 ----------------------*/
struct HOTA_FILE
{
	int tag;
};

/*----------------------
 | g_stdoutTag / g_stderrTag
 | Description: Storage behind stdout and stderr. Two distinct objects, so the
 |   handles compare unequal to each other and to NULL. Never read.
 | Author: suinevere
 ----------------------*/
static HOTA_FILE g_stdoutTag = { 0 };
static HOTA_FILE g_stderrTag = { 1 };

FILE *stdout = &g_stdoutTag;
FILE *stderr = &g_stderrTag;

/*----------------------
 | diag_write
 | Description: Puts one already-formatted line on the debug text layer,
 |   trimming a trailing newline the engine's messages carry -- the layer has
 |   no concept of one and it would render as a stray glyph.
 | Author: suinevere
 ----------------------*/
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

	SRL::Debug::Print(0, (unsigned char)g_diagRow, line);
	g_diagRow++;

	if (g_diagRow >= DIAG_ROWS)
	{
		g_diagRow = 0;
	}
}

/*----------------------
 | diag_emit
 | Description: Formats one diagnostic line into a stack buffer and hands it to
 |   diag_write. Uses newlib's vsnprintf rather than SRL::Debug's variadic
 |   Print, which routes through SRL::string and a shared static buffer this
 |   file has no reason to depend on.
 | Author: suinevere
 ----------------------*/
static void diag_emit(const char *fmt, va_list ap)
{
	char line[DIAG_COLS + 1];

	vsnprintf(line, sizeof(line), fmt, ap);
	diag_write(line);
}

/*----------------------
 | printf
 | Description: The engine's diagnostic channel, on the debug text layer.
 |   common.c's panic() prints through here and then exits, so without this a
 |   fatal engine error is indistinguishable from a black screen. debug.h's
 |   LOG also lands here, but only under -DENABLE_DEBUG, which the Saturn
 |   makefile does not set.
 | Author: suinevere
 ----------------------*/
extern "C" int printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	diag_emit(fmt, ap);
	va_end(ap);
	return 0;
}

/*----------------------
 | fprintf
 | Description: Same sink as printf. The stream is ignored -- stdout and stderr
 |   are opaque tags with nothing behind them, and there is only one place for
 |   text to go.
 | Author: suinevere
 ----------------------*/
extern "C" int fprintf(FILE *stream, const char *fmt, ...)
{
	va_list ap;

	(void)stream;
	va_start(ap, fmt);
	diag_emit(fmt, ap);
	va_end(ap);
	return 0;
}

/*----------------------
 | puts
 | Description: Same sink, no formatting. Copied into a local buffer first
 |   because diag_write trims the trailing newline in place and main.c's help()
 |   passes string literals, which live in read-only .rodata.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | perror
 | Description: Prints the caller's message and nothing else. There is no errno
 |   worth reporting: every failing call that reaches perror in this build is
 |   one of saturn_filestub.c's stubs, which fail unconditionally rather than
 |   for a reason.
 | Author: suinevere
 ----------------------*/
extern "C" void perror(const char *s)
{
	puts(s);
}

/*----------------------
 | malloc
 | Description: The C allocator, on SRL's High Work RAM arena. Deliberately
 |   does not fall back to Low Work RAM when High is full: LWRAM is 1,048,576
 |   bytes and the two bulk buffers claim 933,888 of it, so a silent fallback
 |   would spend the 114,688-byte remainder and fragment the pool those
 |   buffers must fit in. A NULL from here is a bug worth seeing at its cause
 |   rather than one that resurfaces later as a failed saturn_lwram_alloc.
 | Author: suinevere
 ----------------------*/
extern "C" void *malloc(size_t size)
{
	return SRL::Memory::HighWorkRam::Malloc(size);
}

/*----------------------
 | free
 | Description: Returns a block to whichever arena it came from, decided by
 |   address, so that a saturn_lwram_alloc pointer handed to plain free() is
 |   still released correctly. Ignores NULL.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | realloc
 | Description: Resizes within whichever arena the block already lives in
 |   rather than migrating it between pools. Nothing in this engine calls
 |   realloc; it exists so the declared heap surface is complete.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | saturn_lwram_alloc
 | Description: Allocates from Low Work RAM specifically, for the emulated
 |   68000 map and the resident GAME2.BIN. See the banner in saturn_compat.h
 |   for the budget that forces those two out of the main arena.
 | Author: suinevere
 ----------------------*/
extern "C" void *saturn_lwram_alloc(unsigned long size)
{
	return SRL::Memory::LowWorkRam::Malloc((size_t)size);
}

/*----------------------
 | saturn_lwram_free
 | Description: Returns a Low Work RAM block. Ignores NULL, so vm_free_memory
 |   and game2bin_free stay safe on a startup that never allocated.
 | Author: suinevere
 ----------------------*/
extern "C" void saturn_lwram_free(void *p)
{
	if (p != nullptr)
	{
		SRL::Memory::LowWorkRam::Free(p);
	}
}

/*----------------------
 | exit
 | Description: Halts the machine. There is nothing to exit to, so this parks
 |   in a Synchronize() loop forever -- keeping the display alive so that
 |   whatever panic() just printed stays readable -- rather than unwinding into
 |   undefined behaviour.
 | Author: suinevere
 ----------------------*/
extern "C" void exit(int status)
{
	(void)status;

	while (true)
	{
		SRL::Core::Synchronize();
	}
}

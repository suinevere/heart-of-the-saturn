# Saturn Boot and Video Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get the Heart of The Alien engine compiling for SH-2, fitting in the Saturn's 2 MB of work RAM, and drawing the game's own pixels through VDP2.

**Architecture:** The engine keeps its software rasterizer untouched. Every platform dependency moves behind a C seam header with two implementations — one under `saturn/host/` for the SDL build, one under `saturn/src/system/` for Saturn — following the `disc.h` seam the previous sub-project established. The split is enforced by directory, because `saturn/makefile` globs `src/` recursively and `saturn/host/` sits outside that glob. The two multi-hundred-kilobyte engine buffers move to LWRAM; everything the rasterizer touches per pixel stays in HWRAM.

**Tech Stack:** C99 for portable engine code, C++ (`.cxx`) for anything touching SRL, SaturnRingLib on the SGL toolchain (`sh2eb-elf-gcc` 14.2.0), SDL2 + SDL2_mixer for the host reference build.

## Global Constraints

- **Design spec:** `docs/superpowers/specs/2026-08-01-hota-saturn-boot-and-video-design.md`. Read it before starting.
- **Banner comments are mandatory.** Every new file opens with, and every non-trivial symbol is preceded by, a banner of exactly this shape. The Description field carries rationale — the constraint, the bug avoided, the ordering requirement — never a restatement of the name:
  ```c
  /*----------------------
   | <file or symbol name>
   | Description: <what it is and, crucially, WHY>
   | Author: suinevere
   | Dependencies: <headers/modules, or "none">   <-- file banners only
   ----------------------*/
  ```
- **File extensions are not a style choice.** `shared.mk` derives objects via `$(SOURCES:.c=.o)` then `:.cxx=.o` and has pattern rules for only `%.c` and `%.cxx`. A `.cpp` file maps to no object name and no rule, so it drops out of the link **silently**. C for portable logic, `.cxx` for anything including an SRL header.
- **`MEMORY_SIZE` is `0x80000`** (524,288) on both platforms. Both builds must use the same value or the host stops being a valid reference.
- **No global or static object whose constructor allocates.** `malloc` and `operator new` route to an SRL arena that does not exist until `SRL::Core::Initialize()` runs at the top of `main()`.
- **Never define `fflush`.** newlib has one and its own stdio pulls that object in, producing "multiple definition of `fflush'". Make it a no-op *macro* so the call site vanishes instead.
- **Commit messages are one sentence.** No body, no bullets, no trailers, and never a
  mention of Claude, AI or the session. The rationale belongs in the banner comments
  and in this plan, not in the log — every task below gives the exact message to use.
- **Never launch Mednafen from a tool call.** Build the disc and hand it to Suinevere.
- **Host build and host tests must pass at the end of every task.** They are the reference the Saturn backend is bisected against; a task that breaks them is not done.

**Verification commands used throughout:**
```bash
sh saturn/tests/run_tests.sh          # host unit tests
make -C saturn/src                    # host engine build
cd saturn && ./compile.bat            # Saturn build (tasks 9+)
```

---

### Task 1: Repair the host test harness

`6d31b11` moved `src/` to `saturn/src/` but left `run_tests.sh` pointing at the old location, so the suite has been failing since the move and nobody has run it. Nothing later in this plan can be verified until this is fixed, which is why it is first.

**Files:**
- Modify: `saturn/tests/run_tests.sh:10-12`

**Interfaces:**
- Consumes: nothing
- Produces: a working `sh saturn/tests/run_tests.sh` that every later task re-runs

- [ ] **Step 1: Run the suite to see the current failure**

Run: `sh saturn/tests/run_tests.sh`

Expected: FAIL with
```
test_discfmt.c:11:10: fatal error: discfmt.h: No such file or directory
cc1.exe: fatal error: ../../src/discfmt.c: No such file or directory
```

- [ ] **Step 2: Fix the two paths**

The script `cd`s to its own directory (`saturn/tests`), so the engine is now one level up at `../src`, not two at `../../src`.

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests test_discfmt.c ../src/discfmt.c
```

- [ ] **Step 3: Run the suite to verify it passes**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS, all discfmt assertions green, exit 0

- [ ] **Step 4: Commit**

```bash
git add saturn/tests/run_tests.sh
git commit -m "Repair the host test harness paths broken by the src/ move."
```

---

### Task 2: Move host backends out of the Saturn source glob

`saturn/makefile:55` globs `find src/ -name '*.c'`. That now swallows `saturn/src/host/disc_cue.c` and would hand the SDL host backend to the SH-2 compiler. Moving host code to `saturn/host/` puts it outside the glob and establishes the directory convention every later task follows.

**Files:**
- Move: `saturn/src/host/disc_cue.c` → `saturn/host/disc_cue.c`
- Modify: `saturn/src/Makefile:21-25` (the explicit `disc_cue.o` rule)
- Delete: the now-empty `saturn/src/host/`, and the leftover `src/` and `src/host/` at the repo root

**Interfaces:**
- Consumes: Task 1's working test suite
- Produces: the `saturn/host/` convention — all host-only backends live here, outside the Saturn glob

- [ ] **Step 1: Confirm the glob currently reaches the host backend**

Run: `cd saturn && find src/ -name '*.c' | grep host`
Expected: prints `src/host/disc_cue.c` — proof the SH-2 build would compile it

- [ ] **Step 2: Move the file with git**

```bash
git mv saturn/src/host/disc_cue.c saturn/host/disc_cue.c
```

- [ ] **Step 3: Retarget the host Makefile rule**

`saturn/src/Makefile` has an explicit rule because the `.c.o` suffix rule only looks in the current directory. Keep the comment — it still explains why the rule exists — and update both paths:

```make
# disc_cue.o lives under ../host/ -- the .c.o suffix rule above only looks in
# the current directory, so a plain OBJS entry for it would silently never
# compile. Explicit rule beats the implicit suffix rule for the same target.
# It sits outside src/ because saturn/makefile globs `find src/ -name '*.c'`
# for the SH-2 build, which would otherwise compile this SDL file for Saturn.
disc_cue.o: ../host/disc_cue.c
	$(CC) $(CFLAGS) -c ../host/disc_cue.c -o disc_cue.o
```

- [ ] **Step 4: Confirm the glob no longer reaches it**

Run: `cd saturn && find src/ -name '*.c' | grep host`
Expected: no output, exit 1

- [ ] **Step 5: Remove the leftover empty directories**

`6d31b11` left `src/` and `src/host/` behind as empty husks; git does not track empty directories so they are pure clutter.

```bash
rmdir saturn/src/host src/host src
```

- [ ] **Step 6: Verify both builds and the tests**

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh
```
Expected: engine links to `saturn/src/alien.exe`, tests PASS

- [ ] **Step 7: Commit**

```bash
git add -A saturn/host saturn/src/Makefile saturn/src/host
git commit -m "Move the host disc backend out of the Saturn source glob."
```

---

### Task 3: Delete the dead SDL includes

`decode.c` and `animation.c` include `<SDL.h>` and reference no SDL symbol whatsoever. Removing those two lines is the single cheapest step toward the SH-2 build. `scale2x.c` and `scale3x.c` also name no `SDL_*` symbol but their headers type the buffers as `Uint8`, so their include is load-bearing and they stay host-only.

**Files:**
- Modify: `saturn/src/decode.c:20`, `saturn/src/animation.c:23`

**Interfaces:**
- Consumes: Task 2's directory convention
- Produces: `decode.c` and `animation.c` compile for SH-2 unmodified from here on

- [ ] **Step 1: Confirm neither file uses an SDL symbol**

Run: `cd saturn/src && grep -c "SDL_\|Mix_" decode.c animation.c`
Expected: `decode.c:0` and `animation.c:0`

- [ ] **Step 2: Delete the two includes**

Remove `#include <SDL.h>` from `decode.c:20` and from `animation.c:23`. Nothing replaces them.

- [ ] **Step 3: Prove both now compile for SH-2**

```bash
export PATH="$PWD/SaturnRingLib/Compiler/sh2eb-elf/bin:$PATH"
sh2eb-elf-gcc -m2 -c -O2 -Wno-strict-aliasing -o /dev/null saturn/src/decode.c
sh2eb-elf-gcc -m2 -c -O2 -Wno-strict-aliasing -o /dev/null saturn/src/animation.c
```
Expected: both exit 0, no diagnostics

- [ ] **Step 4: Verify the host build is unaffected**

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh
```
Expected: builds clean, tests PASS

- [ ] **Step 5: Commit**

```bash
git add saturn/src/decode.c saturn/src/animation.c
git commit -m "Drop the two dead SDL includes from decode.c and animation.c."
```

---

### Task 4: Pin the emulated map size to a named constant

`get_memory_size()` returns `sizeof(memory)`. Task 5 turns `memory` into a pointer, at which point `sizeof` silently becomes 4 and every caller receives a 4-byte buffer — it compiles clean and corrupts at runtime. Pinning the size to a constant *first*, as its own task, means Task 5 cannot introduce that bug.

**Files:**
- Modify: `saturn/src/vm.h` (add `MEMORY_SIZE`), `saturn/src/vm.c:26,149-152`
- Create: `saturn/tests/test_vm_memory.c`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: Task 1's test harness
- Produces: `#define MEMORY_SIZE 0x80000` in `vm.h`; `int get_memory_size(void)` returning `MEMORY_SIZE`; `unsigned char *get_memory_ptr(int offset)` unchanged in signature

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_vm_memory.c`:

```c
/*----------------------
 | test_vm_memory.c
 | Description: Guards the emulated 68000 map's size contract. get_memory_size()
 |   returned sizeof(memory) while memory was a static array; once it becomes a
 |   pointer, sizeof silently yields 4 and every caller gets a 4-byte buffer
 |   with no diagnostic. These tests pin the value so that regression cannot
 |   land quietly. The bound itself comes from the three fixed load sites --
 |   main.c:116 loads ROOMSn.BIN at 0xf900 (largest 370,688) and
 |   animation.c:870 loads animations at up to 0x809a (largest 436,224) --
 |   whose worst case is 469,146 bytes.
 | Author: suinevere
 | Dependencies: vm.h
 ----------------------*/
#include <assert.h>
#include <stdio.h>
#include "vm.h"

/* The worst case computed from the manifest in disc_cue.c and the two fixed
   load offsets. If a future disc or load site pushes past this, the assert
   below is where it must be noticed. */
#define WORST_CASE_HIGH_WATER 469146

static void test_size_is_the_named_constant(void)
{
	assert(get_memory_size() == MEMORY_SIZE);
	assert(MEMORY_SIZE == 0x80000);
}

static void test_size_covers_the_worst_measured_load(void)
{
	assert(get_memory_size() > WORST_CASE_HIGH_WATER);
}

static void test_size_is_not_a_pointer_width(void)
{
	/* The exact failure this file exists to catch. */
	assert(get_memory_size() != (int)sizeof(void *));
}

int main(void)
{
	test_size_is_the_named_constant();
	test_size_covers_the_worst_measured_load();
	test_size_is_not_a_pointer_width();
	printf("test_vm_memory: all passed\n");
	return 0;
}
```

- [ ] **Step 2: Add it to the harness**

Append to `saturn/tests/run_tests.sh`, after the existing `./run_tests` line:

```sh
gcc -std=c99 -Wall -Wextra -Werror -O1 -g \
    -I../src \
    -o run_tests_vm test_vm_memory.c ../src/vm.c
./run_tests_vm
```

- [ ] **Step 3: Run it to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `MEMORY_SIZE` is not declared, compile error `'MEMORY_SIZE' undeclared`

- [ ] **Step 4: Add the constant and use it**

In `saturn/src/vm.h`, above the `get_memory_size` banner:

```c
/*----------------------
 | MEMORY_SIZE
 | Description: Size of the emulated 68000 memory map, in bytes. Named rather
 |   than taken with sizeof because vm.c's backing store is a static array on
 |   the host and an LWRAM allocation on Saturn -- sizeof would yield 4 on the
 |   pointer form and hand every caller a 4-byte buffer with no diagnostic.
 |   0x80000 rather than the original 0x100000 because the game's two fixed
 |   load sites top out at 469,146 bytes: main.c:117 loads ROOMSn.BIN at
 |   0xf900 (largest 370,688) and animation.c:918 loads animations at up to
 |   0x809a (largest 436,224). The remaining 55,142 bytes are headroom over
 |   that worst case. Nothing checks the bound at runtime -- get_memory_ptr is
 |   a bare add -- so the guarantee is tests/test_vm_memory.c re-deriving it
 |   from disc_manifest.h on every run.
 | Author: suinevere
 ----------------------*/
#define MEMORY_SIZE 0x80000
```

In `saturn/src/vm.c:26`, size the array by the constant:

```c
static unsigned char memory[MEMORY_SIZE];
```

In `saturn/src/vm.c:149-152`, return it:

```c
int get_memory_size(void)
{
	return MEMORY_SIZE;
}
```

Update the `get_memory_size` banner's Description: it no longer returns `sizeof(memory)`, and the reason it does not is the point.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`
Expected: both suites PASS

- [ ] **Step 6: Verify the game still runs on the halved map**

```bash
make -C saturn/src && ./saturn/src/alien.exe
```
Expected: builds clean; the game boots from the repo root and reaches the intro. This is the first real exercise of the 512 KB bound.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/vm.h saturn/src/vm.c saturn/tests/test_vm_memory.c saturn/tests/run_tests.sh
git commit -m "Pin the emulated map size to MEMORY_SIZE and halve it to 512 KB."
```

---

### Task 4b: Relocate the delta-unpack scratch out of the emulated map

Added 2026-08-04 after the Task 4 review found that the map cannot shrink while a 144 KB
decode buffer lives inside it. `animation.c:746` sets `a0 = 0xdc000` (901,120) and
`unpack_animation_delta` writes a decoded stream through `get_memory_ptr(a0)`; the region
from there to `0x100000` is scratch, not emulated 68000 state. `animation.c:745` carries
the upstream author's own note about it: `/* XXX: move into a local array */`. Moving it
out is what lets the map shrink, and the map shrinking is what lets the port fit in 2 MB.

**Files:**
- Modify: `saturn/src/animation.c` (`anim_interesting` signature and its `a2`/`a3` reads; the call site at 740-752)
- Modify: `saturn/src/vm.h` (`MEMORY_SIZE` to `0x80000`, banner rewritten)
- Modify: `saturn/tests/test_vm_memory.c` (the scratch-base assertion no longer describes the map)

**Interfaces:**
- Consumes: `MEMORY_SIZE` from Task 4
- Produces: `static void anim_interesting(int a1, const unsigned char *a2, const unsigned char *a3, unsigned short color_mask)` — `a1` stays a map offset, `a2`/`a3` become direct pointers; `MEMORY_SIZE` becomes `0x80000`

- [ ] **Step 1: Establish that nothing else reads the scratch through the map**

Run: `cd saturn/src && grep -n "0xdc000\|get_memory_ptr" animation.c *.c`
Expected: `0xdc000` appears at exactly one place, `animation.c:746`. If it appears anywhere else, STOP and report — the relocation is not self-contained and the plan needs revisiting.

- [ ] **Step 2: Establish whether `a0`, `a2` and `a3` are read after the call site**

The block at `animation.c:744-752` currently ends by assigning `a2 = a0;` and `a3 = a0 + d4;`. These are function-scope `int`s declared at `animation.c:553` and reused throughout `play_sequence`. Read the whole of `play_sequence` and determine whether any later code reads `a0`, `a2` or `a3` before reassigning them.

Record the answer in your report. If they ARE read later, you must preserve their values; if they are not, the assignments go away with the rest.

- [ ] **Step 3: Add the scratch buffer**

In `saturn/src/animation.c`, beside the other file-scope buffers, with a banner:

```c
/*----------------------
 | delta_scratch
 | Description: Decode buffer for unpack_animation_delta. Lived at offset 0xdc000
 |   inside the emulated 68000 map until 2026-08-04, which forced the map to stay
 |   1 MB and put the Saturn port 189 KB over its total work RAM. It is not
 |   emulated state -- nothing reads it through get_byte, and the original code
 |   carried its own note to move it out. Sized 0x100000 - 0xdc000, the space the
 |   original reserved above the scratch base, because unpack_animation_delta
 |   writes a data-driven stream with no length of its own.
 | Author: suinevere
 ----------------------*/
#define DELTA_SCRATCH_SIZE (0x100000 - 0xdc000)
static unsigned char delta_scratch[DELTA_SCRATCH_SIZE];
```

- [ ] **Step 4: Change `anim_interesting` to take pointers**

`saturn/src/animation.c:227` currently reads:

```c
static void anim_interesting(int a1, int a2, int a3, unsigned short color_mask)
```

Change `a2` and `a3` to `const unsigned char *`. Leave `a1` an `int` — it indexes the loaded animation stream, which is genuine map data.

Inside the body, every `get_byte(a2)` becomes `*a2` and every `get_byte(a3)` becomes `*a3`. The `a2++` and `a3++` increments already work unchanged on pointers. Find them all:

```
cd saturn/src && grep -n "get_byte(a2)\|get_byte(a3)\|a2++\|a3++" animation.c
```

Update the function's banner to say why two of its three cursors are pointers and one is an offset — that asymmetry is the whole point and will look like an inconsistency to the next reader.

- [ ] **Step 5: Rewrite the call site**

`saturn/src/animation.c:744-752` currently reads:

```c
		/* XXX: move into a local array */
		a0 = 0xdc000;
		ptr = get_memory_ptr(a0);
		unpack_animation_delta(a2, ptr);
		a2 = a0;
		a3 = a0 + d4;
		anim_interesting(a1, a2, a3, (unsigned short)d3);
```

Replace with:

```c
		unpack_animation_delta(a2, delta_scratch);
		anim_interesting(a1, delta_scratch, delta_scratch + d4, (unsigned short)d3);
```

Note the ordering: `a2` is the *source* offset here and must be read before it would have been overwritten. The `ptr` local and the `XXX` comment both go away. If Step 2 found that `a0`/`a2`/`a3` are read later, keep whatever assignments are needed to preserve that and say so in your report.

- [ ] **Step 6: Shrink the map**

In `saturn/src/vm.h`, change `MEMORY_SIZE` to `0x80000` and rewrite its banner. The true worst case is 469,146 bytes — animations load at `ANIMATION_LOAD_BASE` `0x809a` and the largest file on the disc is `MAKE2MB.BIN` at 436,224 — leaving 55,142 bytes of headroom. Take both numbers from `disc_manifest.h`, not from memory: 432,128 is the INTRO/END size, not the largest file, and an earlier draft of this step said so and was wrong. The banner must not mention 0xdc000 as a live constraint any more, and must still not claim `get_memory_ptr` validates anything.

- [ ] **Step 7: Fix the test**

`saturn/tests/test_vm_memory.c` asserts `get_memory_size() > DELTA_UNPACK_SCRATCH_BASE`. That constant describes a region no longer in the map, so the assertion is now meaningless. Replace it with the real bound — the largest byte any load site touches, 469,146 — and compute it in the test from `DISC_MANIFEST_LIST` and the two named load bases rather than writing it down as a literal, because a literal is a claim nobody rechecks. Keep the file-header-banner-only rule for test files.

- [ ] **Step 8: Verify**

```bash
sh saturn/tests/run_tests.sh
make -C saturn/src
```

Then run the game from the repo root with a timeout: `timeout 20 ./saturn/src/alien.exe`.

**This task's verification is different from every other task's, and the difference matters.** Every other task is verified by the game still booting. This one changes animation decoding, and the boot sequence may not reach a delta-encoded frame — that is exactly how the Task 4 defect survived its smoke test. So: let the intro play far enough to show animated character movement, and report what you actually saw. If you cannot get the game to render an animation, say so plainly rather than reporting a clean boot as if it proved something.

Note that `saturn/src/Makefile` has no header dependency tracking, so a change to `vm.h` alone will not rebuild `vm.o`. Delete the objects and relink to be sure your change took effect.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/animation.c saturn/src/vm.h saturn/tests/test_vm_memory.c
git commit -m "Move the delta-unpack scratch out of the emulated map and shrink it to 512 KB."
```

---

### Task 5: Route the two bulk buffers through an allocation seam

`memory[]` (512 KB) and `game2bin[]` (400 KB) are the two allocations that must land in LWRAM on Saturn. The host keeps its static arrays so its `.bss` layout and behaviour are unchanged.

**Files:**
- Modify: `saturn/src/vm.c:26`, `saturn/src/vm.h`, `saturn/src/game2bin.c:25-27`, `saturn/src/game2bin.h`
- Modify: `saturn/src/main.c` (call the two allocators before `game2bin_init`)

**Interfaces:**
- Consumes: `MEMORY_SIZE` from Task 4
- Produces: `int vm_alloc_memory(void)` and `int game2bin_alloc(void)`, both returning 1 on success and 0 on failure; `void vm_free_memory(void)` and `void game2bin_free(void)`

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_vm_memory.c`, and call it from `main`:

```c
static void test_alloc_then_pointer_is_usable(void)
{
	unsigned char *base;

	assert(vm_alloc_memory() == 1);

	base = get_memory_ptr(0);
	assert(base != NULL);

	/* Both ends of the map must be writable -- the engine loads at 0xf900
	   and reads back at the very top, so a short allocation would pass a
	   naive smoke test and fail only in game. */
	base[0] = 0xAA;
	base[MEMORY_SIZE - 1] = 0x55;
	assert(base[0] == 0xAA);
	assert(base[MEMORY_SIZE - 1] == 0x55);

	/* Idempotent: calling twice must not leak or move the buffer. */
	assert(vm_alloc_memory() == 1);
	assert(get_memory_ptr(0) == base);

	vm_free_memory();
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `'vm_alloc_memory' undeclared`

- [ ] **Step 3: Implement the seam in vm.c**

Declare in `vm.h` with banners; implement in `vm.c`. The host form keeps the static array and the allocator is a no-op that hands back its address, so the host's memory behaviour is bit-identical to before:

```c
/*----------------------
 | vm_alloc_memory / vm_free_memory
 | Description: Acquires the MEMORY_SIZE-byte emulated 68000 map. Split from a
 |   plain static array because the Saturn build cannot afford it in HWRAM:
 |   measured .bss across the engine is 1,882,592 bytes against 770,048 of
 |   HWRAM, so this buffer and game2bin's live in LWRAM instead. The host keeps
 |   the static array -- it has the address space, and keeping its layout
 |   unchanged is what makes it a valid reference to bisect Saturn bugs
 |   against. Idempotent: a second call returns the same pointer rather than
 |   leaking, so a retried startup path cannot strand a megabyte. Returns 1 on
 |   success, 0 on failure; the caller must treat failure as fatal, because
 |   every get_memory_ptr after it would return an offset from NULL.
 | Author: suinevere
 ----------------------*/
int vm_alloc_memory(void);
void vm_free_memory(void);
```

```c
#if defined(HOTA_SATURN)
static unsigned char *memory = NULL;
#else
static unsigned char memory_storage[MEMORY_SIZE];
static unsigned char *memory = NULL;
#endif

int vm_alloc_memory(void)
{
	if (memory != NULL)
	{
		return 1;
	}

#if defined(HOTA_SATURN)
	memory = (unsigned char *)saturn_lwram_alloc(MEMORY_SIZE);
#else
	memory = memory_storage;
#endif

	return (memory != NULL);
}

void vm_free_memory(void)
{
#if defined(HOTA_SATURN)
	if (memory != NULL)
	{
		saturn_lwram_free(memory);
	}
#endif
	memory = NULL;
}
```

`saturn_lwram_alloc`/`saturn_lwram_free` are declared in `system/saturn_compat.h` and defined in Task 9. On the host they are never referenced, so the host build needs no stub.

- [ ] **Step 4: Implement the same seam in game2bin.c**

Identical shape, with `GAME2BIN_SIZE` in place of `MEMORY_SIZE` and `game2bin` in place of `memory`. Repeat the pattern rather than sharing a macro — two call sites do not justify an abstraction, and the banners differ.

- [ ] **Step 5: Call both allocators before the first disc read**

In `saturn/src/main.c`, immediately before the existing `game2bin_init()` call — `game2bin_init` is the earliest thing in the engine that writes into either buffer, so the ordering is load-bearing:

```c
	/* Both buffers must exist before game2bin_init(), which is the first
	   thing in the engine that reads from the disc and therefore the first
	   thing that writes into either of them. On Saturn these are LWRAM
	   allocations that can genuinely fail; on the host they cannot. */
	if (!vm_alloc_memory())
	{
		panic("out of memory allocating the emulated 68000 map");
	}

	if (!game2bin_alloc())
	{
		panic("out of memory allocating the GAME2.BIN buffer");
	}
```

- [ ] **Step 6: Run tests and the game**

```bash
sh saturn/tests/run_tests.sh && make -C saturn/src && ./saturn/src/alien.exe
```
Expected: tests PASS, game boots to the intro exactly as before

- [ ] **Step 7: Commit**

```bash
git add saturn/src/vm.c saturn/src/vm.h saturn/src/game2bin.c saturn/src/game2bin.h saturn/src/main.c saturn/tests/test_vm_memory.c
git commit -m "Route the two bulk buffers through an allocation seam."
```

---

### Task 6: Extract the video seam

`render.h` is already the right interface — nine functions, no SDL type in any signature — so this task formalizes rather than designs. `render.c` becomes the host backend under `saturn/host/`.

**Files:**
- Create: `saturn/src/video.h`
- Move: `saturn/src/render.c` → `saturn/host/video_sdl.c`
- Delete: `saturn/src/render.h`
- Modify: every caller of the old `render.h` names
- Modify: `saturn/src/Makefile` (OBJS: `render.o` → `video_sdl.o`, with an explicit rule)

**Interfaces:**
- Consumes: Task 2's `saturn/host/` convention
- Produces: `video.h` declaring `video_init`, `video_create_surface`, `video_render(char *src)`, `video_set_palette(int which)`, `video_set_palette_rgb12(unsigned char *rgb12)`, `video_get_current_palette(void)`, `video_set_scroll(int scroll)`, `video_get_scroll_register(void)`, `video_toggle_fullscreen(void)` — all returning `int` where `render.h` did

- [ ] **Step 1: Find every call site before moving anything**

```bash
cd saturn/src && grep -rn "render_init\|render_create_surface\|set_palette\|get_current_palette\|set_scroll\|get_scroll_register\|toggle_fullscreen\|\brender(" --include=*.c --include=*.h .
```
Expected: a list of files to update in Step 4. Record it; the rename is mechanical but must be exhaustive or the link fails.

- [ ] **Step 2: Write `saturn/src/video.h`**

Model the file banner on `disc.h` — state the seam, name both backends, and give the ordering contract, because a backend author reading only this header has to know it:

```c
/*----------------------
 | video.h
 | Description: The single platform boundary between the engine and the screen.
 |   The engine renders into its own 304x192 8-bit paletted buffers with its
 |   own software rasterizer and hands a finished one across this line; nothing
 |   above it knows what a window, a texture or a VDP2 layer is.
 |   saturn/host/video_sdl.c is the SDL implementation, retained as the
 |   reference a wrong Saturn frame is compared against.
 |   src/system/video_srl.cxx is the Saturn implementation, on a VDP2 NBG0
 |   Paletted256 bitmap.
 |
 |   Call-ordering contract: video_init before anything else here, and
 |   video_create_surface before the first video_render. A palette must be set
 |   before the first video_render or the frame comes out black -- the engine
 |   already does this, but a backend must not assume a palette exists at
 |   video_create_surface time. video_toggle_fullscreen is a documented no-op
 |   rather than an error on backends with no window, because the engine calls
 |   it from a key handler that exists on both platforms.
 |
 |   Design: docs/superpowers/specs/2026-08-01-hota-saturn-boot-and-video-design.md
 | Author: suinevere
 | Dependencies: none (opaque parameters only; each backend pulls in its own)
 ----------------------*/
#ifndef VIDEO_H
#define VIDEO_H

int  video_init(void);
int  video_create_surface(void);
void video_render(char *src);
void video_set_palette(int which);
void video_set_palette_rgb12(unsigned char *rgb12);
int  video_get_current_palette(void);
void video_set_scroll(int scroll);
int  video_get_scroll_register(void);
void video_toggle_fullscreen(void);

#endif /* VIDEO_H */
```

Give each function its own banner carrying the rationale from the spec — in particular that `video_set_scroll` is a register write on Saturn rather than the row-shifting copy the SDL backend performs, and that `video_get_scroll_register` exists because the engine reads the value back.

- [ ] **Step 3: Move render.c and rename its functions**

```bash
git mv saturn/src/render.c saturn/host/video_sdl.c
git rm saturn/src/render.h
```

In `video_sdl.c`, rename the nine definitions to their `video_` names and replace `#include "render.h"` with `#include "video.h"`. **Change nothing else.** This file's value is being a known-good reference; behaviour drift here destroys that.

- [ ] **Step 4: Update every caller**

Apply the renames found in Step 1 and switch those files' `#include "render.h"` to `#include "video.h"`.

- [ ] **Step 5: Add the explicit Makefile rule**

`video_sdl.o` lives outside the current directory, so it needs the same treatment `disc_cue.o` has. Replace `render.o` in `OBJS` with `video_sdl.o` and add:

```make
video_sdl.o: ../host/video_sdl.c
	$(CC) $(CFLAGS) -c ../host/video_sdl.c -o video_sdl.o
```

- [ ] **Step 6: Verify the host build and the game**

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh && ./saturn/src/alien.exe
```
Expected: links clean, tests PASS, and the intro looks **identical** to before — this is a pure rename

- [ ] **Step 7: Commit**

```bash
git add -A saturn/src saturn/host saturn/src/Makefile
git commit -m "Extract the video seam and move render.c to the host backend."
```

---

### Task 7: Extract the input seam

`check_events()` is 185 lines of SDL keyboard switch that writes nine file-static key variables. Input behaviour is out of scope for this sub-project, but the *seam* must exist or `main.c` cannot compile for SH-2.

**Files:**
- Create: `saturn/src/input.h`
- Create: `saturn/host/input_sdl.c` (receives `check_events` from `main.c:503-690`)
- Modify: `saturn/src/main.c:90-91` (the key statics lose `static`), `saturn/src/main.h:5`
- Modify: `saturn/src/Makefile`

**Interfaces:**
- Consumes: nothing from earlier tasks
- Produces: `void check_events(void)` implemented per platform; `extern int key_up, key_down, key_left, key_right, key_a, key_b, key_c, key_select, key_reset_record;` declared in `input.h` and defined in `main.c`

- [ ] **Step 1: Write `saturn/src/input.h`**

```c
/*----------------------
 | input.h
 | Description: The single platform boundary between the engine and the
 |   controls. check_events() drains whatever the platform calls an event
 |   queue and leaves the result in the key state below; the game loop reads
 |   that state and never asks how it got there.
 |
 |   The key variables are defined in main.c and declared here rather than
 |   living with the backend, because the game loop in main.c reads them every
 |   frame while the backend only writes them. They were file-static in main.c
 |   until the backend moved out; that is the only reason they are visible.
 |
 |   saturn/host/input_sdl.c is the SDL keyboard implementation.
 |   src/system/input_srl.cxx is the Saturn implementation and is deliberately
 |   empty for now -- pad mapping is a later sub-project, and the intro this
 |   sub-project boots to plays on a timer with no input at all.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef INPUT_H
#define INPUT_H

extern int key_up, key_down, key_left, key_right;
extern int key_a, key_b, key_c, key_select;
extern int key_reset_record;

/*----------------------
 | check_events
 | Description: Drains the platform event queue and updates the key state and
 |   cls.quit. Called once per frame from the game loop. A backend with no
 |   input source implements this as an empty function rather than refusing to
 |   link -- that is the supported way to defer input, and it leaves the key
 |   state at its initial zero so the engine simply sees nothing pressed.
 | Author: suinevere
 ----------------------*/
void check_events(void);

#endif /* INPUT_H */
```

- [ ] **Step 2: Un-static the key variables**

`saturn/src/main.c:90-91` currently reads:

```c
static int key_up, key_down, key_left, key_right, key_a, key_b, key_c, key_select;
static int key_reset_record;
```

Drop `static` from both lines and add `#include "input.h"` to `main.c`. Remove the now-duplicate `void check_events();` declaration from `main.h:5`.

- [ ] **Step 3: Move check_events into the host backend**

Create `saturn/host/input_sdl.c` with a file banner, `#include <SDL.h>`, `#include "input.h"`, `#include "client.h"` (for `cls`), and `check_events()` moved verbatim from `main.c:503-690`. Delete it from `main.c`. The body is not modified — the SDL keysym switch is host behaviour and stays exactly as it is.

- [ ] **Step 4: Wire the Makefile**

Add `input_sdl.o` to `OBJS` and the explicit rule:

```make
input_sdl.o: ../host/input_sdl.c
	$(CC) $(CFLAGS) -c ../host/input_sdl.c -o input_sdl.o
```

- [ ] **Step 5: Verify input still works in the host build**

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh && ./saturn/src/alien.exe
```
Expected: builds clean, tests PASS, and the keyboard still drives the game — arrows move, Escape quits. A pure move must change nothing.

- [ ] **Step 6: Commit**

```bash
git add -A saturn/src saturn/host saturn/src/Makefile
git commit -m "Extract the input seam and move check_events to the host backend."
```

---

### Task 8: Extract the platform seam

What remains of SDL in `main.c` after Task 7: `SDL_Init`, `SDL_Quit`, `SDL_GetTicks`, `SDL_Delay`. The audio-device setup at `main.c:158-194` is host-backend policy that `disc.h`'s banner already documents as unportable, so it moves to the host backend rather than being abstracted.

**Files:**
- Create: `saturn/src/platform.h`, `saturn/host/platform_sdl.c`
- Modify: `saturn/src/main.c:144,149,158-194,691,705-709`
- Modify: `saturn/src/Makefile`

**Interfaces:**
- Consumes: nothing from earlier tasks
- Produces: `int platform_init(void)` (1 on success, 0 on failure), `void platform_quit(void)`, `unsigned int platform_ticks(void)` (milliseconds since init), `void platform_delay(unsigned int ms)`, `void platform_frame(void)`

- [ ] **Step 1: Write `saturn/src/platform.h`**

```c
/*----------------------
 | platform.h
 | Description: Startup, shutdown, the millisecond clock and the frame pump --
 |   everything main.c used to take from SDL that is not video, input or disc.
 |
 |   platform_frame is the one function with no SDL ancestor. On the host the
 |   game loop is free-running and this is a no-op; on Saturn it is
 |   SRL::Core::Synchronize(), and the loop must call it every iteration or the
 |   VDP2 layer is never presented and the screen stays black. It is declared
 |   here rather than hidden inside video_render because it paces the whole
 |   frame, not just the blit.
 |
 |   Audio device setup is deliberately absent. main.c's Mix_OpenAudioDevice
 |   block exists solely to guarantee the host disc backend's CD-DA
 |   precondition, as disc.h's banner sets out; SRL::Sound::Cdda has no
 |   negotiated spec and no resampler question, so there is nothing to
 |   abstract. It moves into the host backend instead.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef PLATFORM_H
#define PLATFORM_H

int          platform_init(void);
void         platform_quit(void);
unsigned int platform_ticks(void);
void         platform_delay(unsigned int ms);
void         platform_frame(void);

#endif /* PLATFORM_H */
```

Give each function its own banner. `platform_ticks` must document that it returns milliseconds and that the engine's throttle at `main.c:705-709` busy-waits on it, so a backend returning a coarse or non-monotonic clock will change the game's speed.

- [ ] **Step 2: Write `saturn/host/platform_sdl.c`**

`platform_init` performs `SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)` and then the whole `Mix_OpenAudioDevice`/`Mix_QuerySpec` block from `main.c:158-194`, moved verbatim including its comments — those comments explain why `allowed_changes` is 0 and are the reason the CD-DA path is correct. Return 0 if either fails. `platform_quit` calls `SDL_Quit()`. `platform_ticks` returns `SDL_GetTicks()`. `platform_delay` calls `SDL_Delay(ms)`. `platform_frame` is empty, with a comment saying the host loop is free-running and the function exists for Saturn's `Synchronize`.

- [ ] **Step 3: Rewire main.c**

Replace `SDL_Init` and the audio block at `main.c:149-194` with a single guarded `platform_init()` call; replace `SDL_Quit()` at `144` with `platform_quit()`; replace the three `SDL_GetTicks()` and one `SDL_Delay(1)` with their `platform_` equivalents; add `platform_frame()` at the end of the game loop body. Delete `#include <SDL.h>` and `#include <SDL_mixer.h>` from `main.c` and add `#include "platform.h"`.

- [ ] **Step 4: Prove main.c is now SDL-free**

Run: `cd saturn/src && grep -c "SDL\|Mix_" main.c`
Expected: `0`

- [ ] **Step 5: Wire the Makefile and verify**

Add `platform_sdl.o` to `OBJS` with its explicit rule, then:

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh && ./saturn/src/alien.exe
```
Expected: builds clean, tests PASS, game runs at the same speed with sound and music working — the audio block moved but did not change

- [ ] **Step 6: Commit**

```bash
git add -A saturn/src saturn/host saturn/src/Makefile
git commit -m "Extract the platform seam so main.c is free of SDL."
```

---

### Task 9: libc and linkage shims, and the first SH-2 compile of every engine TU

The five include-path traps that break ordinary C under SaturnRingLib. They are adapted from Another-Saturn's `saturn_compat.{h,cxx}`, `saturn_filestub.c` and `saturn_new.cxx`, not rediscovered.

**Files:**
- Create: `saturn/src/system/saturn_compat.h`, `saturn/src/system/saturn_compat.cxx`, `saturn/src/system/saturn_filestub.c`, `saturn/src/system/saturn_new.cxx`
- Modify: `saturn/makefile` (exclude the host-only `scale2x.c`, `scale3x.c`, `sound.c` from `SOURCES`)

**Interfaces:**
- Consumes: `vm_alloc_memory`/`game2bin_alloc` from Task 5, which call `saturn_lwram_alloc`
- Produces: `void *saturn_lwram_alloc(unsigned long size)`, `void saturn_lwram_free(void *p)`, plus `malloc`/`free`/`realloc`/`exit`/`printf`/`fprintf` and a `FILE` shim

- [ ] **Step 1: Read the reference implementation completely**

Read `../Another-Saturn/saturn/src/saturn_compat.h`, `saturn_compat.cxx`, `saturn_filestub.c` and `saturn_new.cxx` in full, and `../Another-Saturn/mem/srl-libc-shadowing.md`. Do not skim — the five traps are individually non-obvious and partial understanding guarantees a broken link.

- [ ] **Step 2: Write the shims**

The five traps, each of which must be handled:

1. **`<cstdio>` cannot be included at all.** `modules/dummy/stdio.h` is nothing but `#define printf(...) ((void)0)` — no `FILE`, no `fopen` — and libstdc++'s `<cstdio>` dies on `using ::FILE;`. Declare the `FILE` API in the shim instead.
2. **Prefer C headers over `<cXXX>` wrappers everywhere.** `<cstring>` hoists the whole standard set into `std::` and hard-fails on `strcoll`/`strerror`/`strtok`/`strxfrm`, none of which this libc has.
3. **SGL's `stdlib.h` has no `malloc` and no `exit`.** Route `malloc` onto `SRL::Memory::HighWorkRam` — newlib's needs an `sbrk` that `-specs=nosys.specs` does not provide — and make `exit` a `while (true) SRL::Core::Synchronize();` halt.
4. **SGL's headers have no `extern "C"` guard.** Including them from a `.cxx` declares `strlen`/`memset`/`memcpy` with C++ linkage and the mangled symbols resolve nowhere. Wrap them: `extern "C" { #include <string.h> #include <stdlib.h> }`.
5. **Global `operator new`/`delete` are `inline` in `srl_memory.hpp`,** so they are only emitted in TUs that include it. Define them in `saturn_new.cxx`, which must **not** include `<srl.hpp>` or the compiler rejects them as redefinitions, and forward to `malloc`.

Plus the LWRAM pair this port needs, which Another-Saturn has no equivalent of:

```c
/*----------------------
 | saturn_lwram_alloc / saturn_lwram_free
 | Description: Allocates from the 1 MB Low Work RAM pool rather than the main
 |   arena. Exists because the engine's two bulk buffers -- the 512 KB emulated
 |   68000 map and the 400 KB resident GAME2.BIN -- do not fit in HWRAM
 |   alongside the code and the framebuffers: measured .bss is 1,882,592 bytes
 |   against 770,048 of HWRAM. Together these two need 933,888 of the
 |   1,048,576-byte pool, which fits with its TLSF headers, so they are
 |   ordinary allocations and nothing has to be reserved by address.
 |   LWRAM is a 16-bit bus, so only bulk blobs belong here -- never anything
 |   the rasterizer touches per pixel.
 | Author: suinevere
 ----------------------*/
void *saturn_lwram_alloc(unsigned long size);
void  saturn_lwram_free(void *p);
```

Declare both inside an `extern "C"` block in `saturn_compat.h` so `vm.c` and `game2bin.c`, which are C, can call them.

- [ ] **Step 3: Exclude the host-only TUs from the Saturn glob**

`scale2x.c`, `scale3x.c` and `sound.c` cannot compile for SH-2 — the first two type their buffers as SDL's `Uint8`, and `sound.c` is built on `Mix_Chunk`. They stay in `saturn/src/` because the host build needs them, so `saturn/makefile` must filter them out. After the two `SOURCES` lines:

```make
# Host-only translation units. scale2x/scale3x type their buffers as SDL's
# Uint8 and sound.c is built on Mix_Chunk, so none of the three compiles for
# SH-2. They stay under src/ because the host build needs them, which means
# the glob above finds them and they have to be removed by name. Sound is a
# later sub-project; the pixel doublers have no Saturn equivalent at all.
SOURCES := $(filter-out src/scale2x.c src/scale3x.c src/sound.c,$(SOURCES))
```

- [ ] **Step 4: Compile every remaining engine TU for SH-2**

```bash
cd saturn && ./compile.bat 2>&1 | tee /tmp/build.log
```
Expected: **compilation** of every TU under `src/` succeeds. The link is expected to fail at this stage with undefined references to `video_*`, `check_events` and the `disc_*` functions — Tasks 10 through 12 supply those. Compilation errors are real failures; those three groups of undefined symbols are not.

- [ ] **Step 5: Verify the host build is untouched**

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh
```
Expected: builds clean, tests PASS

- [ ] **Step 6: Commit**

```bash
git add saturn/src/system saturn/makefile
git commit -m "Add the libc and linkage shims so every engine TU compiles for SH-2."
```

---

### Task 10: Saturn disc backend

The engine's pixels come off the CD, so the disc backend has to land before anything can be drawn. Only `disc_read_file` does real work; the two music functions are no-ops until the audio sub-project.

**Files:**
- Create: `saturn/src/system/disc_srl.cxx`

**Interfaces:**
- Consumes: `disc.h`'s five-function contract, unchanged
- Produces: `disc_open`, `disc_read_file`, `disc_play_track`, `disc_stop_track`, `disc_close`, all in an `extern "C"` block

- [ ] **Step 1: Read the reference implementation**

Read `../Another-Saturn/saturn/src/system/saturn_cdfile.cxx` in full, and `saturn/src/disc.h`'s banners — particularly the ordering contract and the caveat about `cue_path`.

- [ ] **Step 2: Implement the backend**

Points the implementation must honour, each from `disc.h`'s existing contract:

- `disc_open(const char *cue_path)` **accepts and ignores** `cue_path`. A Saturn disc has no cue sheet; there is one drive and one mounted disc. The parameter stays in the signature because the seam is shared. Returns 1 on success, 0 on failure, and leaves no partial state on failure.
- `disc_read_file(const char *name, void *out, int max_size)` opens by 8.3 uppercase name through `SRL::Cd::File`, and **must** refuse to write more than `max_size` bytes. That bound is the entire reason the signature differs from the old `read_file`: all three call sites hand in raw addresses into the emulated map with no bounds check of their own. Returns 0 on success, negative on failure.
- It must also return negative if `disc_open` has not succeeded, or if `disc_close` has run since — the contract requires it and `atexit` ordering depends on it.
- `disc_play_track`/`disc_stop_track` are empty, each with a banner saying CD-DA is the audio sub-project's work and that the seam is satisfied by a silent no-op exactly as the host backend is when `cls.nosound` is set.
- `disc_close` is safe before `disc_open`, after a failed `disc_open`, and twice in a row.

Wrap the whole file's declarations in `extern "C" { }` — `disc.h` is a C header and every caller is a C translation unit.

- [ ] **Step 3: Verify it compiles for SH-2**

```bash
cd saturn && ./compile.bat 2>&1 | grep -i "disc_srl"
```
Expected: no compilation diagnostics for `disc_srl.cxx`; undefined `video_*` and `check_events` at link are still expected

- [ ] **Step 4: Verify the host build and tests are untouched**

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh
```
Expected: builds clean, tests PASS — this task adds a Saturn-only file and must not touch the host

- [ ] **Step 5: Commit**

```bash
git add saturn/src/system/disc_srl.cxx
git commit -m "Add the Saturn disc backend on SRL::Cd::File."
```

---

### Task 11: Saturn video backend

The VDP2 NBG0 `Paletted256` bitmap. The engine's 8-bit paletted screens map to hardware with no pixel conversion at all.

**Files:**
- Create: `saturn/src/system/video_srl.cxx`

**Interfaces:**
- Consumes: `video.h` from Task 6
- Produces: the nine `video_*` functions in an `extern "C"` block

- [ ] **Step 1: Confirm the SRL API surface before writing against it**

Read `SaturnRingLib/saturnringlib/srl_vdp2.hpp` around the `NBG0` declaration at line 937 and the `BmpScreen` template at 810, plus `srl_bitmap.hpp:65-104` for `BitmapInfo` and `CRAM::TextureColorMode`. Also read `../Another-Saturn/saturn/src/system/saturn_platform.cxx` for a working NBG bitmap bring-up.

- [ ] **Step 2: Implement the backend**

The five facts this file is built on, all established in the spec:

- **Format.** `CRAM::TextureColorMode::Paletted256` matches the engine's 8-bit paletted screens exactly. `video_render` is a copy, never a conversion.
- **Geometry.** The engine's buffer is 304×192. VDP2 bitmaps are fixed-size, so it lives in a 512×256 layer: **192 per-line copies of 304 bytes, source pitch 304, destination stride 512.** One `memcpy` will not do. Centre the image in the 320×224 NTSC display; do not scale, because VDP2 cannot scale a bitmap layer for free.
- **Scroll.** `video_set_scroll` writes the layer's vertical scroll position and stores a shadow value; `video_get_scroll_register` returns the shadow, because the engine reads it back. The three row-shifting branches in the SDL backend have no counterpart here — that is the point.
- **Palette.** `video_set_palette_rgb12` receives 4-bit-per-channel RGB. Each channel becomes RGB555 with one left shift. Verify the nibble order against the SDL backend's output on the first frame rather than assuming it; Another World's equivalent needed no swap, but that is a different codebase.
- **`video_toggle_fullscreen` is an empty function** with a banner saying so, not an error.

- [ ] **Step 3: Verify it compiles for SH-2**

```bash
cd saturn && ./compile.bat 2>&1 | grep -i "video_srl"
```
Expected: no compilation diagnostics for `video_srl.cxx`

- [ ] **Step 4: Verify the host is untouched**

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh
```
Expected: builds clean, tests PASS

- [ ] **Step 5: Commit**

```bash
git add saturn/src/system/video_srl.cxx
git commit -m "Add the Saturn video backend on a VDP2 NBG0 Paletted256 bitmap."
```

---

### Task 12: Saturn entry point, input stub, and first boot

Everything links. `SRL::Core::Initialize()` has to run before the first allocation, which means before `vm_alloc_memory`.

**Files:**
- Create: `saturn/src/system/input_srl.cxx`, `saturn/src/system/platform_srl.cxx`
- Modify: `saturn/makefile` (`CD_NAME` already correct; verify `SRL_CUSTOM_CCFLAGS` carries `-DHOTA_SATURN`)

**Interfaces:**
- Consumes: `platform.h` and `input.h` from Tasks 7 and 8, every backend from Tasks 9 to 11
- Produces: a linking ELF and a bootable disc

- [ ] **Step 1: Add `-DHOTA_SATURN` to the Saturn build**

Task 5's allocation seam and Task 9's shims both branch on it. In `saturn/makefile`, beside the existing `-DBYPASS_PROTECTION`:

```make
# Selects the Saturn arm of the allocation seam in vm.c and game2bin.c, which
# routes the two bulk buffers to LWRAM instead of leaving them in .bss. Without
# it the SH-2 build compiles cleanly and then fails to link, because the
# 1,882,592 bytes of .bss do not fit the 770,048 HWRAM offers.
SRL_CUSTOM_CCFLAGS += -DHOTA_SATURN
```

- [ ] **Step 2: Write the input stub**

`saturn/src/system/input_srl.cxx`: an `extern "C"` `check_events()` with an empty body and a banner explaining that pad mapping is a later sub-project and that leaving the key state at zero makes the engine see nothing pressed, which is correct for the intro this sub-project boots to.

- [ ] **Step 3: Write the platform backend**

`saturn/src/system/platform_srl.cxx`:

- `platform_init` calls `SRL::Core::Initialize()`. **This must be the first thing that runs**, before any allocation, because `malloc` and `operator new` both route to an SRL arena that does not exist until it returns. Note that in the banner.
- `platform_ticks` returns milliseconds from the vblank counter or `SRL::Timer`; document which, and that the engine's throttle busy-waits on this value.
- `platform_delay` spins on `platform_ticks` calling `SRL::Core::Synchronize()`, so a delay does not stall the display.
- `platform_frame` calls `SRL::Core::Synchronize()`.
- `platform_quit` is an empty halt-safe no-op — there is nothing to return to.

- [ ] **Step 4: Build the disc**

```bash
cd saturn && ./compile.bat
```
Expected: links, and produces `BuildDrop/Heart of the Alien (USA).{elf,iso,bin,cue,map}`

- [ ] **Step 5: Check the map against the memory budget**

```bash
grep -E "^(\.text|\.data|\.rodata|\.bss|HEAP|WORK_AREA)" "saturn/BuildDrop/Heart of the Alien (USA).map"
```
Expected: `.bss` ends below `work_area_start` at `0x060c0000`, with heap remaining. This is the moment the ~224 KB heap estimate stops being an estimate — record the real number. **Measured: `.bss` 581,200, heap 69,440**, so the estimate was out by 3.2x, mostly because 11,592 bytes of SRL/SGL/newlib `.bss` were never counted. Note also that `SRL::Cd::File` takes 10,240 heap bytes per open file (`srl_cd.hpp:441`) — 14.7% of what is left. If `.bss` overruns, the spec's two levers are trimming `SGL_MAX_POLYGONS`/`SGL_MAX_VERTICES` (recovers ~155 KB of the 171,120-byte work area) and moving `huge_buf` into VDP2 VRAM (another 171 KB).

- [ ] **Step 6: Verify the host build one last time**

```bash
make -C saturn/src && sh saturn/tests/run_tests.sh && ./saturn/src/alien.exe
```
Expected: builds clean, tests PASS, game runs — the dual-backend requirement holds at the end as it did at every step

- [ ] **Step 7: Hand the disc to Suinevere**

Do **not** launch Mednafen from a tool call. Report the BuildDrop path and the map numbers from Step 5, and ask for a boot test.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/system saturn/makefile
git commit -m "Add the Saturn entry point and input stub so the disc links and boots."
```

---

## Plan Self-Review

**Spec coverage.** Every spec section maps to a task: the memory placement to Tasks 4 and 5, `video.h` to Task 6, the host video backend to Task 6, the Saturn video backend to Task 11, the disc backend to Task 10, `platform.h` to Task 8, the shims to Task 9, the pure subtraction to Task 3, the build glob fix to Task 2, and the test-harness repair to Task 1. The spec's three verification levels appear as recurring steps rather than one task, which is deliberate — the host build and tests are re-run at the end of every task, since their value is catching the regression at the task that caused it.

**One spec item was withdrawn, not deferred.** The spec used to make "the host bounds assert survives a full playthrough" an acceptance criterion, and this paragraph used to defer it to Suinevere at Task 12 Step 7 on the grounds that a full playthrough is a human activity. The assert was never written, so there was nothing to defer: `vm.c:190` is a bare add and `vm.h` says so. The criterion is withdrawn and replaced, in the spec and in Task 4 Step 7, by `test_vm_memory.c` deriving the bound from `disc_manifest.h` on every run — which is checkable by the plan rather than by a human remembering.

**Type consistency.** `MEMORY_SIZE` is used identically in Tasks 4, 5 and 9. `vm_alloc_memory`/`game2bin_alloc` return `int` 1/0 in Tasks 5 and 9 and are called that way in Task 5 Step 5. The nine `video_*` names in Task 6 are the same nine implemented in Task 11. `check_events(void)` matches in Tasks 7 and 12. `saturn_lwram_alloc(unsigned long)` is declared in Task 9 and called in Task 5 — note the ordering, which is intentional: Task 5 compiles on the host, where the symbol is never referenced, and the Saturn arm only has to resolve once Task 9 lands.

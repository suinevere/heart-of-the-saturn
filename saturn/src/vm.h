#ifndef __VM_INCLUDED__
#define __VM_INCLUDED__

#define MAX_TASKS 64

/** Gets variable either from global memory, or from auxiliry memory
    @param var   index in memory

    Note that each variable is 16 bit. in global memory there are 256
    variables, while in auxiliry memory, there are only 32. use 
    toggle_aux() to select between these two memory sets
*/
short get_variable(int var);

void set_variable(int var, short value);

void set_aux_bank(int bank);

/** Toggles use of auxiliry memory for getters and setters
    @param toggle  non-zero to use aux memory
    @returns old toggle value
*/
int toggle_aux(int toggle);

/** Loads byte from memory offset
    @param offset    address (512K)
    @returns value
*/
unsigned char get_byte(int offset);

/** Loads word from memory offset
    @param offset    address (512K)
    @return value

    No alignment restrictions
*/
unsigned short get_word(int offset);

/** Loads long from memory offset
    @param offset    address (512K)
    @return value

    No alignment restrictions
*/
unsigned long get_long(int offset);

/** Copies variables from global set, to aux storage
    @param dst_index first element in aux storage
    @param src_index first element in global storage
    @param count     number of elements to copy
*/
void copy_global_to_tls(int dst_index, int src_index, int count);

/** Copies variables from aux storage to global set
    @param dst_index first element in aux storage
    @param src_index first element in global storage
    @param count     number of elements to copy
*/
void copy_tls_to_global(int dst_index, int src_index, int count);

unsigned char *get_memory_ptr(int offset);

/*----------------------
 | MEMORY_SIZE
 | Description: Size of the emulated 68000 memory map, in bytes. Named rather
 |   than taken with sizeof because vm.c's backing store is a static array on
 |   the host and an LWRAM allocation on Saturn -- sizeof would yield 4 on the
 |   pointer form and hand every caller a 4-byte buffer with no diagnostic.
 |   0x80000 (524,288) covers every load site with room to spare. The bound is
 |   a function of disc_manifest.h and the two named load bases, and
 |   tests/test_vm_memory.c computes it from them rather than trusting the
 |   figures below: animations load at ANIMATION_LOAD_BASE 0x809a and the
 |   largest file on the disc is MAKE2MB.BIN at 436,224 bytes, for a worst case
 |   of 469,146 and 55,142 bytes of headroom; ROOMSn.BIN loads at
 |   ROOMS_LOAD_BASE 0xf900 and the largest is 370,688, ending lower at
 |   434,432. The figures here read 432,128 and "about 59 KB" until 2026-08-04,
 |   which was wrong -- 432,128 is the INTRO/END size, not the largest file --
 |   and it reached a right answer only because MAKE2MB.BIN happens to end 154
 |   bytes below where that reasoning put the ceiling. That is why the test
 |   derives the number instead of asserting it.
 |   It was 0x100000 until 2026-08-04 only because a 144 KB delta-unpack
 |   scratch was parked at 0xdc000; that buffer is a host array in animation.c
 |   now and no longer constrains this. get_memory_ptr does not itself validate
 |   offsets against this bound; nothing enforces it at runtime beyond the map
 |   being large enough to contain it, and disc_read_file refusing a file
 |   larger than the max_size its caller derives from this constant.
 | Author: suinevere
 ----------------------*/
#define MEMORY_SIZE 0x80000

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

/*----------------------
 | get_memory_size
 | Description: Total size of the emulated 68000 memory map. Returns the
 |   named constant MEMORY_SIZE rather than sizeof(memory) -- memory is a
 |   static array on the host today but becomes an LWRAM pointer on Saturn,
 |   at which point sizeof would silently yield 4 and hand every caller a
 |   4-byte buffer with no diagnostic. disc_read_file call sites need to know
 |   how much room is left from an offset (their max_size argument) -- this
 |   lets them compute it as get_memory_size() - offset.
 | Author: suinevere
 ----------------------*/
int get_memory_size(void);

void vm_reset();

#endif

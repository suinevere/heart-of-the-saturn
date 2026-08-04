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
 |   0x80000 covers every load site with room to spare: animation.c loads
 |   animations at up to offset 0x809a and the largest is 432,128 bytes, for a
 |   worst case of 465,050, and main.c:116 loads ROOMSn.BIN at 0xf900 (largest
 |   370,688) which ends lower. That leaves about 59 KB of headroom. It was
 |   0x100000 until 2026-08-04 only because a 144 KB delta-unpack scratch was
 |   parked at 0xdc000; that buffer is a host array in animation.c now and no
 |   longer constrains this. get_memory_ptr does not itself validate offsets
 |   against this bound; nothing enforces it at runtime beyond the map being
 |   large enough to contain it.
 | Author: suinevere
 ----------------------*/
#define MEMORY_SIZE 0x80000

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

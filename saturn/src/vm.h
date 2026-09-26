#ifndef __VM_INCLUDED__
#define __VM_INCLUDED__

#define MAX_TASKS 64

#define VM_VAR_CODE_SEEN 227

#define VM_VAR_BUTTONS      254
#define VM_BUTTON_MASK_ABC  0xc0

short get_variable(int var);

void set_variable(int var, short value);

void set_aux_bank(int bank);

int toggle_aux(int toggle);

unsigned char get_byte(int offset);

unsigned short get_word(int offset);

unsigned long get_long(int offset);

void copy_global_to_tls(int dst_index, int src_index, int count);

void copy_tls_to_global(int dst_index, int src_index, int count);

unsigned char *get_memory_ptr(int offset);

#define MEMORY_SIZE 0x80000

int vm_alloc_memory(void);
void vm_free_memory(void);

int get_memory_size(void);

void vm_reset();

#endif

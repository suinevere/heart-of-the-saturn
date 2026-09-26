#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "vm.h"
#include "sprites.h"
#include "decode.h"
#include "main.h"

static int g_fail = 0;

static void expect_int(const char *what, int got, int want)
{
    if (got != want) {
        g_fail++;
        printf("FAIL %s\n  actual   = %d\n  expected = %d\n", what, got, want);
    }
}

sprite_t sprites[MAX_SPRITES];
const char *sprite_data_byte_str[16];
int first_sprite, last_sprite, sprite_count;
short task_pc[MAX_TASKS];
short new_task_pc[MAX_TASKS];
short enabled_tasks[MAX_TASKS];
short new_enabled_tasks[MAX_TASKS];
int current_room;
int next_script;
int ending_played;
int death_played;
int access_code_skip;
int access_code_answer;
unsigned int access_code_skip_at;
unsigned int access_code_load_seq;
FILE *stderr;
unsigned int platform_ticks(void) { return 0u; }
int debug_flag;

void print_sprite(int p) { (void)p; }
short get_sprite_data_word(int e, int i) { (void)e; (void)i; return 0; }
void set_sprite_data_word(int e, int i, short v) { (void)e; (void)i; (void)v; }
unsigned char get_sprite_data_byte(int e, int i) { (void)e; (void)i; return 0; }
void set_sprite_data_byte(int e, int i, unsigned char v) { (void)e; (void)i; (void)v; }
void reset_sprite_list(void) { }
void move_sprite_by(int s, int dx, int dy) { (void)s; (void)dx; (void)dy; }
void flip_sprite(int s) { (void)s; }
void mirror_sprite(int s) { (void)s; }
void unmirror_sprite(int s) { (void)s; }
void remove_sprite(int v) { (void)v; }
void draw_sprites(void) { }

void copy_screen(int a, int b) { (void)a; (void)b; }
void fill_screen(int a, int b) { (void)a; (void)b; }
void select_screen(int a) { (void)a; }
void update_screen(int a) { (void)a; }
void load_room_screen(int room, int index) { (void)room; (void)index; }
void video_set_palette(int p) { (void)p; }
void video_set_scroll(int v) { (void)v; }
void screen_arm_fade_restore(void) { }

void disc_play_track(int t, int loop) { (void)t; (void)loop; }
void disc_stop_track(void) { }
void play_sample(int a, int b, int c) { (void)a; (void)b; (void)c; }
void play_death_animation(int i, int chained) { (void)i; (void)chained; }
int vm_enter_code(int code, int *played)
{
    if (played) {
        *played = 0;
    }
    return code;
}

int extw(unsigned int b) { return (b & 0x80) ? (int)(b | 0xFFFFFF00u) : (int)b; }
int extl(unsigned short w) { return (short)w; }

void mark_opcode(unsigned char op) { (void)op; }

void *saturn_lwram_alloc(unsigned long size) { return malloc((size_t)size); }
void  saturn_lwram_free(void *p) { free(p); }

static jmp_buf g_panic;
static int g_panicked;

void panic(const char *s)
{
    (void)s;
    g_panicked = 1;
    longjmp(g_panic, 1);
}

#define SCRIPT_BASE 0x10000

extern int script_ptr;
extern int pc;

static int run_one(const unsigned char *bytes, int len)
{
    memcpy(get_memory_ptr(SCRIPT_BASE), bytes, (size_t)len);
    script_ptr = SCRIPT_BASE;
    toggle_aux(0);
    g_panicked = 0;

    if (setjmp(g_panic) != 0) {
        printf("  the decoder walked off the script and panicked at pc %d\n",
               pc);
        return -1;
    }
    return decode(0, 0);
}

static void test_a_known_opcode_measures_its_own_length(void)
{
    static const unsigned char script[] = {
        0x00, 0x1D, 0x00, 0x82,
        0x06
    };

    expect_int("a var-assign is four bytes, so yield ends at five",
               run_one(script, (int)sizeof script), 5);
}

static void test_the_harness_agrees_with_a_second_known_opcode(void)
{
    static const unsigned char script[] = {
        0x01, 0x01, 0x9D,
        0x56, 0x0A,
        0x06
    };

    expect_int("three plus two plus the yield is six",
               run_one(script, (int)sizeof script), 6);
}

static void prime_sprite_list(int occupied)
{
    memset(sprites, 0, sizeof sprites);
    sprite_count = occupied;
    first_sprite = 0;
    last_sprite = 1;

    if (occupied) {
        sprites[0].u1 = 0xFF;
    }
}

static void test_add_sprite_is_ten_bytes(void)
{
    static const unsigned char script[] = {
        0x25, 0x5F, 0x2D, 0x00, 0x01,
        0x01, 0x18, 0x00, 0xA4, 0x01,
        0x06
    };

    prime_sprite_list(0);
    expect_int("the sprite opcode is ten bytes, so yield ends at eleven",
               run_one(script, (int)sizeof script), 11);
}

static void test_add_sprite_is_ten_bytes_on_its_other_branch(void)
{
    static const unsigned char script[] = {
        0x25, 0x5F, 0x2D, 0x00, 0x01,
        0x01, 0x18, 0x00, 0xA4, 0x01,
        0x06
    };

    prime_sprite_list(1);
    expect_int("and ten with a sprite already in the list",
               run_one(script, (int)sizeof script), 11);
}

static void test_set_sprite_is_nine_bytes(void)
{
    static const unsigned char script[] = {
        0x89, 0x02, 0x2D, 0x00, 0x01,
        0x01, 0x18, 0x00, 0xA4,
        0x06
    };

    prime_sprite_list(1);
    set_variable(2, 1);
    expect_int("the set-sprite opcode is nine bytes, so yield ends at ten",
               run_one(script, (int)sizeof script), 10);
}

static void test_a_sprite_leaves_the_next_opcode_where_it_belongs(void)
{
    static const unsigned char script[] = {
        0x25, 0x5F, 0x2D, 0x00, 0x01,
        0x01, 0x18, 0x00, 0xA4, 0x01,
        0x00, 0x0A, 0x01, 0x02,
        0x06
    };

    prime_sprite_list(0);
    set_variable(0x0A, 0);

    expect_int("the sequence reaches its yield at fifteen",
               run_one(script, (int)sizeof script), 15);
    expect_int("having assigned the variable after the sprite, not inside it",
               (int)get_variable(0x0A), 0x0102);
}

static void test_the_access_code_screen_becomes_a_death_screen(void)
{
    static const unsigned char opens[] = {
        0x56, 0xE3,
        0x19, 0x00, 0x0A,
        0x06
    };
    static const unsigned char other[] = {
        0x56, 0x0A,
        0x06
    };

    access_code_skip = 0;
    access_code_answer = 0;
    death_played = 0;
    next_script = 0;

    expect_int("the frame ends on the write, before the backdrop is drawn",
               run_one(opens, (int)sizeof opens), 2);
    expect_int("and the player is owed a death screen", death_played, 1);
    expect_int("at the room the gate stands in for", next_script, 7);
    expect_int("with nothing left pending for update_keys",
               access_code_answer, 0);

    access_code_skip = 0;
    death_played = 0;
    next_script = 0;
    expect_int("zeroing another variable decodes the same way",
               run_one(other, (int)sizeof other), 3);
    expect_int("and is not a death", death_played, 0);
    expect_int("and asks for no room", next_script, 0);
}

static void test_a_skip_waiting_for_its_load_does_not_answer_a_screen(void)
{
    static const unsigned char opens[] = {
        0x56, 0xE3,
        0x06
    };

    access_code_skip = ACCESS_CODE_SKIP_ARMED;
    access_code_answer = 0;
    death_played = 0;
    next_script = 0;

    expect_int("an armed skip is not yet its own load's, so the frame ends",
               run_one(opens, (int)sizeof opens), 2);
    expect_int("and the player is owed a death screen", death_played, 1);
    expect_int("at the room the gate stands in for", next_script, 7);
    expect_int("with nothing left pending for update_keys",
               access_code_answer, 0);
}

static void test_the_screen_after_a_resume_is_skipped_instead(void)
{
    static const unsigned char opens[] = {
        0x56, 0xE3,
        0x06
    };

    access_code_skip = 1;
    access_code_answer = 0;
    death_played = 0;
    next_script = 0;

    expect_int("a skipped screen does not end the frame early",
               run_one(opens, (int)sizeof opens), 3);
    expect_int("it is not a death", death_played, 0);
    expect_int("and asks for no room", next_script, 0);
    expect_int("update_keys is left to answer the poll",
               access_code_answer, 1);
    expect_int("and the skip is spent", access_code_skip, 0);

    access_code_answer = 0;
    expect_int("so the next screen ends the frame again",
               run_one(opens, (int)sizeof opens), 2);
    expect_int("and is a death once more", death_played, 1);
}

int main(void)
{
    if (!vm_alloc_memory()) {
        printf("test_decode_operands: could not allocate the emulated map\n");
        return 1;
    }
    vm_reset();

    test_the_access_code_screen_becomes_a_death_screen();
    test_the_screen_after_a_resume_is_skipped_instead();
    test_a_skip_waiting_for_its_load_does_not_answer_a_screen();
    test_a_known_opcode_measures_its_own_length();
    test_the_harness_agrees_with_a_second_known_opcode();
    test_add_sprite_is_ten_bytes();
    test_add_sprite_is_ten_bytes_on_its_other_branch();
    test_set_sprite_is_nine_bytes();
    test_a_sprite_leaves_the_next_opcode_where_it_belongs();

    if (g_fail == 0) {
        printf("decode_operands: all tests passed\n");
        return 0;
    }
    printf("decode_operands: %d failure(s)\n", g_fail);
    return 1;
}

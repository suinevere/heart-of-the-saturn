#ifndef CHECKPOINTS_H
#define CHECKPOINTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define CHECKPOINT_COUNT 20

#define CHECKPOINT_WORD_LEN 4

#define CHECKPOINT_PROGRESS_BYTES   16
#define CHECKPOINT_PROGRESS_VERSION 1

const char *checkpoint_word(int i);

int checkpoint_code(int i);

int checkpoint_room(int i);

int checkpoint_entry(int i);

int checkpoint_find(int room, int entry);

const char *checkpoint_room_name(int room);

int checkpoint_visible(unsigned long mask, int all, unsigned char *out,
                       int cap);

void checkpoint_progress_serialise(unsigned long mask, unsigned char *out);

int checkpoint_progress_parse(const unsigned char *buf, int len,
                              unsigned long *mask);

#ifdef __cplusplus
}
#endif

#endif

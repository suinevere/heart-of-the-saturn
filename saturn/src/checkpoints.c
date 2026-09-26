#include "checkpoints.h"

typedef struct {
    const char *word;
    short       code;
    signed char room;
    signed char entry;
} Checkpoint;

static const Checkpoint CHECKPOINTS[CHECKPOINT_COUNT] = {
    { "BDXF", 17000, 1, 0 },
    { "XRCL", 17001, 1, 1 },
    { "DGBJ", 17010, 2, 0 },
    { "RLRB", 17012, 2, 2 },
    { "KTLB", 17013, 2, 3 },
    { "LKHC", 17020, 3, 0 },
    { "FFTR", 17021, 3, 1 },
    { "CXLD", 17022, 3, 2 },
    { "GCDT", 17023, 3, 3 },
    { "HJJG", 17030, 4, 0 },
    { "TTKX", 17031, 4, 1 },
    { "TBBL", 17032, 4, 2 },
    { "HLCJ", 17033, 4, 3 },
    { "GCXB", 17040, 5, 0 },
    { "CDJR", 17041, 5, 1 },
    { "FXRT", 17042, 5, 2 },
    { "LTKX", 17043, 5, 3 },
    { "XKHH", 17060, 6, 0 },
    { "RJLG", 17070, 8, 0 },
    { "KGDD", 17091, 8, 1 }
};

static const char PROGRESS_MAGIC[4] = { 'H', 'P', 'R', 'G' };

const char *checkpoint_word(int i)
{
    if (i < 0 || i >= CHECKPOINT_COUNT) {
        return "";
    }
    return CHECKPOINTS[i].word;
}

int checkpoint_code(int i)
{
    if (i < 0 || i >= CHECKPOINT_COUNT) {
        return 0;
    }
    return CHECKPOINTS[i].code;
}

int checkpoint_room(int i)
{
    if (i < 0 || i >= CHECKPOINT_COUNT) {
        return 0;
    }
    return CHECKPOINTS[i].room;
}

int checkpoint_entry(int i)
{
    if (i < 0 || i >= CHECKPOINT_COUNT) {
        return 0;
    }
    return CHECKPOINTS[i].entry;
}

int checkpoint_find(int room, int entry)
{
    int i;

    for (i = 0; i < CHECKPOINT_COUNT; i++) {
        if (CHECKPOINTS[i].room == room && CHECKPOINTS[i].entry == entry) {
            return i;
        }
    }
    return -1;
}

const char *checkpoint_room_name(int room)
{
    static const char *NAMES[9] = {
        "ROOM 0", "ROOM 1", "ROOM 2", "ROOM 3", "ROOM 4",
        "ROOM 5", "ROOM 6", "ROOM 7", "ROOM 8"
    };

    if (room < 0 || room > 8) {
        return "ROOM ?";
    }
    return NAMES[room];
}

int checkpoint_visible(unsigned long mask, int all, unsigned char *out,
                       int cap)
{
    int n = 0;
    int i;

    for (i = 0; i < CHECKPOINT_COUNT && n < cap; i++) {
        if (i == 0 || all || (mask & (1UL << i)) != 0UL) {
            out[n] = (unsigned char)i;
            n++;
        }
    }
    return n;
}

void checkpoint_progress_serialise(unsigned long mask, unsigned char *out)
{
    int i;

    for (i = 0; i < 4; i++) {
        out[i] = (unsigned char)PROGRESS_MAGIC[i];
    }
    out[4] = (unsigned char)CHECKPOINT_PROGRESS_VERSION;
    out[5] = 0;
    out[6] = 0;
    out[7] = 0;
    out[8] = (unsigned char)((mask >> 24) & 0xFFUL);
    out[9] = (unsigned char)((mask >> 16) & 0xFFUL);
    out[10] = (unsigned char)((mask >> 8) & 0xFFUL);
    out[11] = (unsigned char)(mask & 0xFFUL);
    out[12] = 0;
    out[13] = 0;
    out[14] = 0;
    out[15] = 0;
}

int checkpoint_progress_parse(const unsigned char *buf, int len,
                              unsigned long *mask)
{
    unsigned long v;
    int i;

    *mask = 0UL;

    if (len < CHECKPOINT_PROGRESS_BYTES) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (buf[i] != (unsigned char)PROGRESS_MAGIC[i]) {
            return 0;
        }
    }
    if (buf[4] != (unsigned char)CHECKPOINT_PROGRESS_VERSION) {
        return 0;
    }

    v = ((unsigned long)buf[8] << 24) | ((unsigned long)buf[9] << 16) |
        ((unsigned long)buf[10] << 8) | (unsigned long)buf[11];
    *mask = v & ((1UL << CHECKPOINT_COUNT) - 1UL);
    return 1;
}

/*----------------------
 | discfmt.c
 | Description: Implementation of the pure Sega CD disc-format arithmetic
 |   declared in discfmt.h. Kept to <stdint.h>, <stddef.h> and <string.h> so it
 |   compiles unmodified into the engine, into tools/extract_disc, and into the
 |   host test runner in saturn/tests/run_tests.sh.
 | Author: suinevere
 | Dependencies: discfmt.h
 ----------------------*/
#include "discfmt.h"
#include <string.h>

#define DISCFMT_USER_SECTOR  2048
#define DISCFMT_SYNC_HEADER  16

#define DISCFMT_MUSIC_FIRST_TRACK 1   /* engine index already counts the data track */
#define DISCFMT_MUSIC_MAX_INDEX   41  /* cue 02..42 reached from engine 1..41 */

uint32_t discfmt_mode1_user_offset(uint32_t lba)
{
    return lba * (uint32_t)DISCFMT_RAW_SECTOR + DISCFMT_SYNC_HEADER;
}

uint32_t discfmt_sector_span(uint32_t size)
{
    return (size + (DISCFMT_USER_SECTOR - 1u)) / DISCFMT_USER_SECTOR;
}

/*----------------------
 | discfmt_ascii_upper
 | Description: Explicit A-Z fold instead of tolower() -- tolower is locale
 |   dependent and would drag in <ctype.h>, and this file must stay free of
 |   anything beyond stdint.h/stddef.h/string.h to remain host-testable.
 | Author: suinevere
 ----------------------*/
static char discfmt_ascii_upper(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        return (char)(c - 'a' + 'A');
    }

    return c;
}

int discfmt_iso_name_eq(const char *iso_name, uint8_t iso_len, const char *want)
{
    uint8_t i;
    size_t want_len = strlen(want);

    for (i = 0; i < iso_len; i++)
    {
        if (iso_name[i] == ';')
        {
            break;
        }

        if (i >= want_len)
        {
            /* iso_name is longer than want at the point want ran out --
               a shared prefix, not a match (e.g. ROOMS11.BIN vs ROOMS1.BIN). */
            return 0;
        }

        if (discfmt_ascii_upper(iso_name[i]) != discfmt_ascii_upper(want[i]))
        {
            return 0;
        }
    }

    /* Both sides must end together: if want has bytes left over, iso_name's
       name (up to ';' or iso_len) was a strict prefix of it and no match. */
    return i == want_len;
}

int discfmt_cue_track_for_music(int engine_index)
{
    if (engine_index < 1 || engine_index > DISCFMT_MUSIC_MAX_INDEX)
    {
        return 0;
    }

    return engine_index + DISCFMT_MUSIC_FIRST_TRACK;
}

/*----------------------
 | discfmt_read_le32
 | Description: Reads a little-endian uint32 out of a 4-byte field. Every
 |   LBA and size in an ISO9660 directory record is stored twice, LE then BE;
 |   this always reads the LE copy, per the header's contract.
 | Author: suinevere
 ----------------------*/
static uint32_t discfmt_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

int discfmt_iso_root(const uint8_t *pvd_user, uint32_t *lba, uint32_t *len)
{
    static const uint8_t DISCFMT_PVD_IDENTIFIER[5] = { 'C', 'D', '0', '0', '1' };
    const uint8_t *rec;
    uint8_t rec_len;

    /* Refuse to trust offset 156 of a buffer that is not actually a PVD --
       wrong type code, or missing the "CD001" identifier, means this is the
       wrong sector (or garbage), not a directory record. */
    if (pvd_user[0] != 1 || memcmp(pvd_user + 1, DISCFMT_PVD_IDENTIFIER, 5) != 0)
    {
        return 0;
    }

    rec = pvd_user + 156;
    rec_len = rec[0];

    if (rec_len < 34)
    {
        return 0;
    }

    if (lba != NULL)
    {
        *lba = discfmt_read_le32(rec + 2);
    }

    if (len != NULL)
    {
        *len = discfmt_read_le32(rec + 10);
    }

    return 1;
}

int discfmt_iso_find(const uint8_t *dir, uint32_t dir_len, const char *name, uint32_t *lba, uint32_t *size)
{
    uint32_t pos = 0;

    while (pos < dir_len)
    {
        uint32_t block_end = (pos / DISCFMT_USER_SECTOR + 1) * DISCFMT_USER_SECTOR;
        uint8_t rec_len;

        if (block_end > dir_len)
        {
            block_end = dir_len;
        }

        if (pos >= block_end)
        {
            break;
        }

        rec_len = dir[pos];

        if (rec_len == 0)
        {
            /* Rest of this block is padding -- advance to the next block
               boundary, not end of directory. */
            pos = block_end;
            continue;
        }

        if (rec_len < 33 || pos + rec_len > block_end)
        {
            /* Truncated or corrupt: fail closed rather than read past the
               record's own declared length or the block/dir_len bound. */
            return 0;
        }

        {
            uint8_t name_len = dir[pos + 32];

            if (33u + name_len > rec_len)
            {
                return 0;
            }

            if (discfmt_iso_name_eq((const char *)(dir + pos + 33), name_len, name))
            {
                if (lba != NULL)
                {
                    *lba = discfmt_read_le32(dir + pos + 2);
                }

                if (size != NULL)
                {
                    *size = discfmt_read_le32(dir + pos + 10);
                }

                return 1;
            }
        }

        pos += rec_len;
    }

    return 0;
}

/*----------------------
 | discfmt_cue_track_number
 | Description: Parses the decimal, zero-padded track number following
 |   "TRACK " on a cue TRACK line. Bounds the scan with avail because a
 |   cue sheet's final line may lack a trailing newline -- ordinary input
 |   when Task 4's disc_open reads with fread into a sized buffer and
 |   hands over that exact length. Every existing test and the real disc's
 |   own cue file end in a newline, which is why this was not caught until
 |   a guard-page regression test was written for it.
 | Author: suinevere
 ----------------------*/
static int discfmt_cue_track_number(const char *p, size_t avail)
{
    int n = 0;

    while (avail > 0 && *p >= '0' && *p <= '9')
    {
        n = n * 10 + (*p - '0');
        p++;
        avail--;
    }

    return n;
}

/*----------------------
 | discfmt_cue_msf_frames
 | Description: Parses a cue sheet's mm:ss:ff timestamp into a frame (sector)
 |   count. Every field is exactly two digits and the separator is exactly a
 |   colon, so this refuses anything looser rather than guessing: a
 |   mis-parsed timestamp becomes a byte offset that silently truncates a
 |   track, which is far harder to see than a rejected cue.
 |
 |   Frames are bounded at 74 because 75 of them are one second and should
 |   have been carried. Minutes are not bounded -- Red Book allows 99 and no
 |   overflow is reachable at two digits.
 | Author: suinevere
 | Params: p -- first byte of the timestamp; avail -- bytes left on the line;
 |   out_frames -- receives the frame count, untouched on failure
 | Returns: 1 on a well-formed timestamp, 0 otherwise
 ----------------------*/
static int discfmt_cue_msf_frames(const char *p, size_t avail, int *out_frames)
{
    int m;
    int s;
    int f;
    int k;

    if (avail < 8)
    {
        return 0;
    }

    for (k = 0; k < 8; k++)
    {
        if (k == 2 || k == 5)
        {
            if (p[k] != ':')
            {
                return 0;
            }
        }
        else if (p[k] < '0' || p[k] > '9')
        {
            return 0;
        }
    }

    m = (p[0] - '0') * 10 + (p[1] - '0');
    s = (p[3] - '0') * 10 + (p[4] - '0');
    f = (p[6] - '0') * 10 + (p[7] - '0');

    if (s > 59 || f > 74)
    {
        return 0;
    }

    *out_frames = (m * 60 + s) * 75 + f;
    return 1;
}

int discfmt_cue_parse(const char *text, size_t len, DiscCue *out, int *single_file)
{
    size_t i = 0;
    char cur_filename[256];
    int have_file = 0;
    int index0_frames = 0;
    int have_index0 = 0;
    int have_index1 = 0;

    if (single_file != NULL)
    {
        *single_file = 0;
    }

    out->count = 0;
    cur_filename[0] = '\0';

    while (i < len)
    {
        size_t line_start = i;
        size_t line_end;

        while (i < len && text[i] != '\n' && text[i] != '\r')
        {
            i++;
        }
        line_end = i;

        while (i < len && (text[i] == '\n' || text[i] == '\r'))
        {
            i++;
        }

        {
            const char *line = text + line_start;
            size_t line_len = line_end - line_start;
            size_t j = 0;

            /* Skip leading whitespace to find the first non-space token. */
            while (j < line_len && (line[j] == ' ' || line[j] == '\t'))
            {
                j++;
            }

            if (line_len - j >= 4 && memcmp(line + j, "FILE", 4) == 0)
            {
                /* Take everything between the first and last '"' on the
                   line -- filenames on this disc contain spaces and
                   parentheses that a whitespace tokeniser would mangle. */
                size_t first_quote = (size_t)-1;
                size_t last_quote = (size_t)-1;
                size_t k;

                for (k = j; k < line_len; k++)
                {
                    if (line[k] == '"')
                    {
                        if (first_quote == (size_t)-1)
                        {
                            first_quote = k;
                        }
                        last_quote = k;
                    }
                }

                if (first_quote == (size_t)-1 || last_quote <= first_quote)
                {
                    return 0;
                }

                {
                    size_t name_len = last_quote - first_quote - 1;

                    if (name_len >= sizeof(cur_filename))
                    {
                        return 0;
                    }

                    memcpy(cur_filename, line + first_quote + 1, name_len);
                    cur_filename[name_len] = '\0';
                    have_file = 1;
                }
            }
            else if (line_len - j >= 5 && memcmp(line + j, "TRACK", 5) == 0)
            {
                int number;
                int is_audio;
                size_t k = j + 5;

                if (!have_file)
                {
                    /* A second TRACK with no intervening FILE line means a
                       single-file image; a TRACK with no FILE line at all
                       (the very first line) is a different, plain malformed
                       cue -- distinguished by whether we have ever seen a
                       FILE line and already consumed it for a prior track. */
                    if (out->count > 0)
                    {
                        if (single_file != NULL)
                        {
                            *single_file = 1;
                        }
                    }

                    return 0;
                }

                while (k < line_len && line[k] == ' ')
                {
                    k++;
                }

                number = discfmt_cue_track_number(line + k, (size_t)(line_len - k));

                while (k < line_len && line[k] >= '0' && line[k] <= '9')
                {
                    k++;
                }

                while (k < line_len && line[k] == ' ')
                {
                    k++;
                }

                if (line_len - k >= 5 && memcmp(line + k, "AUDIO", 5) == 0)
                {
                    is_audio = 1;
                }
                else if (line_len - k >= 10 && memcmp(line + k, "MODE1/2352", 10) == 0)
                {
                    is_audio = 0;
                }
                else
                {
                    return 0;
                }

                if (out->count >= DISCFMT_MAX_TRACKS)
                {
                    return 0;
                }

                out->tracks[out->count].number = number;
                out->tracks[out->count].is_audio = is_audio;
                out->tracks[out->count].pregap_sectors = 0;
                memcpy(out->tracks[out->count].filename, cur_filename, sizeof(cur_filename));
                out->count++;

                /* This FILE line has now been consumed by a TRACK; a second
                   TRACK before another FILE line is the single-file case. */
                have_file = 0;

                /* Index state belongs to one TRACK. Not clearing it here would
                   let a track with no INDEX 00 inherit its predecessor's and
                   strip audio off its own front. */
                index0_frames = 0;
                have_index0 = 0;
                have_index1 = 0;
            }
            else if (line_len - j >= 5 && memcmp(line + j, "INDEX", 5) == 0)
            {
                /* Only INDEX 00 and INDEX 01 are read, and only their
                   difference is kept. Higher indexes subdivide a track and
                   are none of this parser's business.

                   These are offsets into the track's own file rather than
                   absolute disc addresses, which holds only because the
                   single-file layout is rejected above -- there, INDEX times
                   are absolute and their difference would still be right but
                   INDEX 00 at 00:00:00 would not mean "byte 0 of the file".
                   That distinction is why this reads the difference rather
                   than trusting INDEX 01 alone. */
                int number;
                int frames;
                size_t k = j + 5;

                if (out->count == 0)
                {
                    return 0;
                }

                while (k < line_len && line[k] == ' ')
                {
                    k++;
                }

                number = discfmt_cue_track_number(line + k, (size_t)(line_len - k));

                while (k < line_len && line[k] >= '0' && line[k] <= '9')
                {
                    k++;
                }

                while (k < line_len && line[k] == ' ')
                {
                    k++;
                }

                if (number > 1)
                {
                    continue;
                }

                if (!discfmt_cue_msf_frames(line + k, (size_t)(line_len - k), &frames))
                {
                    return 0;
                }

                if (number == 0)
                {
                    if (have_index0 || have_index1)
                    {
                        return 0;
                    }

                    index0_frames = frames;
                    have_index0 = 1;
                }
                else
                {
                    if (have_index1)
                    {
                        return 0;
                    }

                    if (have_index0)
                    {
                        if (frames < index0_frames)
                        {
                            return 0;
                        }

                        out->tracks[out->count - 1].pregap_sectors = frames - index0_frames;
                    }

                    have_index1 = 1;
                }
            }
        }
    }

    if (out->count == 0)
    {
        return 0;
    }

    return 1;
}

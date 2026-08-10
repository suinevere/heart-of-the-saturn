#!/usr/bin/env python3
"""----------------------
 | mkpcmsfx.py
 | Description: Converts the game's short CD-DA effect tracks into 8 kHz
 |   8-bit signed mono PCM files for the Saturn sound backend to play, so a
 |   death sting does not cost a seek a third of the way across the disc.
 |
 |   Cues 26-31 and 42 are effects, not music: one to six seconds of content
 |   each, every one of them preceded by about two seconds of digital silence
 |   because Red Book will not accept a track shorter than four seconds. The
 |   lead is measured per track rather than assumed, so a track whose padding
 |   differs is still trimmed correctly.
 |
 |   Output is the format sfxconv.c already produces and sound_srl.cxx
 |   already plays: raw signed 8-bit mono at 8 kHz, no header. That is why
 |   there is no decoder on the Saturn side.
 |
 |   Run from the repository root. Re-run only if the audio rip changes.
 | Author: suinevere
 ----------------------"""

import os
import struct
import sys

SRC_RATE = 44100
DST_RATE = 8000
CUES = [26, 27, 28, 29, 30, 31, 42]
SILENCE = 1000
MUSIC_DIR = os.path.join("saturn", "cd", "music")
DATA_DIR = os.path.join("saturn", "cd", "data")


def read_pcm(path):
    """Returns the 16-bit stereo sample payload of a RIFF/WAVE file."""
    raw = open(path, "rb").read()
    if raw[:4] != b"RIFF":
        return raw
    pos = 12
    while pos + 8 <= len(raw):
        cid = raw[pos:pos + 4]
        size = struct.unpack_from("<I", raw, pos + 4)[0]
        if cid == b"data":
            return raw[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    return b""


def content_bounds(data):
    """First and last frame index whose peak clears the silence floor."""
    frames = len(data) // 4
    first = None
    last = None
    for i in range(frames):
        left = struct.unpack_from("<h", data, i * 4)[0]
        if abs(left) > SILENCE:
            if first is None:
                first = i
            last = i
    if first is None:
        return 0, frames
    return first, last + 1


def convert(data, first, last):
    """Resamples a stereo 16-bit range to signed 8-bit mono at DST_RATE."""
    out = bytearray()
    span = last - first
    count = (span * DST_RATE) // SRC_RATE
    for i in range(count):
        src = first + (i * SRC_RATE) // DST_RATE
        left = struct.unpack_from("<h", data, src * 4)[0]
        right = struct.unpack_from("<h", data, src * 4 + 2)[0]
        mono = (left + right) // 2 // 256
        if mono > 127:
            mono = 127
        if mono < -127:
            mono = -127
        out.append(mono & 0xff)
    return bytes(out)


def main():
    if not os.path.isdir(MUSIC_DIR):
        sys.stderr.write("mkpcmsfx: %s not found; run from the repo root\n" % MUSIC_DIR)
        return 1

    total = 0
    for cue in CUES:
        src = os.path.join(MUSIC_DIR, "track%02d.wav" % cue)
        if not os.path.exists(src):
            sys.stderr.write("mkpcmsfx: %s missing, skipped\n" % src)
            continue

        data = read_pcm(src)
        first, last = content_bounds(data)
        pcm = convert(data, first, last)

        dst = os.path.join(DATA_DIR, "SFX%02d.PCM" % cue)
        open(dst, "wb").write(pcm)
        total += len(pcm)
        sys.stdout.write("SFX%02d.PCM  %6.2fs lead  %6.2fs content  %6d bytes\n"
                         % (cue, first / float(SRC_RATE),
                            (last - first) / float(SRC_RATE), len(pcm)))

    sys.stdout.write("total %d bytes\n" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())

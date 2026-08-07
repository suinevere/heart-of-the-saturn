/*----------------------
 | sfxconv.h
 | Description: Turns a sound-effect index into playable sample bytes: locates
 |   the sample inside the emulated 68000 map, decodes its 8-bit
 |   sign-magnitude data to the signed 8-bit the SCSP plays natively, and pads
 |   it to the minimum length slPCMOn will accept.
 |
 |   It is a separate file from sound_srl.cxx for two reasons. The first is
 |   testability, the same reason discsec.c and cdda_classify.c are separate:
 |   a wrong decode table or an off-by-eight in the header walk produces sound,
 |   just the wrong sound, and that is diagnosed by ear on an emulator at a
 |   round trip per attempt unless a host test pins it in milliseconds.
 |
 |   The second is linkage. Every map access in this port goes through vm.h's
 |   get_byte/get_long, which assemble their results byte by byte and are the
 |   reason the map has been endian-clean since the port began. vm.h carries no
 |   extern "C" guard, unlike disc.h, discsec.h, cdtoc.h and the rest of the
 |   seam headers, so including it from a .cxx would declare get_byte with C++
 |   linkage and fail the link. Keeping every map access on this side of the
 |   boundary means sound_srl.cxx never needs it.
 |
 |   Design: docs/superpowers/specs/2026-08-07-hota-saturn-sfx-design.md
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef SFXCONV_H
#define SFXCONV_H

/* The Saturn backend is C++; without this its callers would look for mangled
   names and fail to link, the way the six seam headers did before 7f66fe3. */
#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SFXCONV_MIN_PLAYABLE
 | Description: The shortest buffer slPCMOn will play, in bytes. SRL's own
 |   RawPcm constructor clamps to this with the comment "slPCMOn won't play
 |   samples shorter than 0x900" (srl_sound.hpp:522), and pads the tail with
 |   zeroes. 2,304 bytes is 0.288 seconds at 8 kHz, so a short sample holds its
 |   channel far longer than the host would -- silently, because the padding is
 |   silence, and harmlessly, because play_sample stops a channel before
 |   reusing it.
 | Author: suinevere
 ----------------------*/
#define SFXCONV_MIN_PLAYABLE 0x900

/*----------------------
 | sfxconv_decode_byte
 | Description: Converts one 8-bit sign-magnitude sample byte to signed 8-bit.
 |   Reproduces src/sound.c's two-branch form exactly, including its overflow
 |   at 0x80: that byte is sign-magnitude negative zero, whose correct value is
 |   0, and the host's `s = u` assigns 128 to a signed char and lands on -128.
 |   The host is the only reference implementation anyone here can run, so
 |   parity wins -- but the divergence is one table entry, pinned by name in
 |   tests/test_sfxconv.c, so reversing the decision later is one character.
 | Author: suinevere
 | Globals: N/A
 | Params: u -- raw byte from the emulated 68000 map
 | Returns: The decoded signed sample value
 ----------------------*/
signed char sfxconv_decode_byte(unsigned char u);

#ifdef __cplusplus
}
#endif

#endif /* SFXCONV_H */

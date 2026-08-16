/*----------------------
 | chainload.h
 | Description: Hands the console to Part I. Both programs link to 0x06004000
 |   (sgl.linker's PRELOADER section), so Part I is not loaded beside us but
 |   over us, and there is no way back short of a console reset. There is no
 |   BOOT ROM service for this: SYS_EXECDMP is the crash dumper and SYS_Exit
 |   returns to the shell, which re-boots our own first-read file.
 |
 |   Design: docs/superpowers/specs/2026-08-16-hota-saturn-part-one-chainload-design.md
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef CHAINLOAD_H
#define CHAINLOAD_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | CHAINLOAD_IMAGE
 | Description: Part I's program on the disc, written there by
 |   tools/another/fetch.sh from Another-Saturn's own cd/data/0.bin.
 | Author: suinevere
 ----------------------*/
#define CHAINLOAD_IMAGE "ANOTHER.BIN"

/*----------------------
 | chainload_available
 | Description: Reports whether Part I's program is on this disc. One name
 |   lookup, called once before the first still is drawn while the drive is
 |   otherwise idle.
 | Author: suinevere
 | Dependencies: srl.hpp
 | Globals: N/A
 | Params: N/A
 | Returns: non-zero when CHAINLOAD_IMAGE exists
 ----------------------*/
int chainload_available(void);

/*----------------------
 | chainload_run
 | Description: Fades out, loads Part I over this program and jumps to it.
 |   Never returns on success.
 |
 |   Returns only on a staging failure, which is the last point at which
 |   returning is possible: nothing has been overwritten and no hardware has
 |   been torn down yet. Past that point failure is neither recoverable nor
 |   detectable, which is why the quiesce steps are short, ordered, and
 |   independent of each other -- a failure is found by removing them one at a
 |   time rather than by rewriting this.
 |
 |   A return relights the boot art and VDP2, since the caller faded both out
 |   before calling and nothing else puts them back.
 | Author: suinevere
 | Dependencies: srl.hpp, disc.h, saturn_bootart.h, video.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A on success; returns to the caller only if staging failed
 ----------------------*/
void chainload_run(void);

#ifdef __cplusplus
}
#endif

#endif /* CHAINLOAD_H */

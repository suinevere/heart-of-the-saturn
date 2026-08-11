# Memory index — heart-of-the-saturn

- [Input and the endian sprite bug](2026-08-06-input-and-endian-sprite-bug.md) — session handoff at 94aee4b: CD-DA and input shipped and verified; a big-endian type-mismatch in the sprite free list fixed but not yet confirmed on hardware.
- [SFX silent in gameplay](2026-08-08-saturn-sfx-silent-in-gameplay.md) — RESOLVED at 21e24fb: the volume was mapped onto slPCMOn's dB attenuation as if linear. Kept for the five refuted hypotheses and the cycle-costing traps; the SCSP contention experiment is answered (it does not contend).
- [Death animation CD-DA sync](2026-08-11-death-animation-cdda-sync.md) — the delay is the fade, not disc_wait_for_music, which returns in 16 ms; four attempts to make it conditional each lost the splat. A two-segment death is two adjacent 0x21 opcodes, so its whole 960 ms is now spent in one fade up front. Read before touching play_death_animation.
- [Death fade shape and the history squash](2026-08-13-death-fade-shape-and-history-squash.md) — the 67-commit history exists only in the local branch pre-squash-845ffac; origin/main was force-pushed over it. Nothing in the squashed commit has run on hardware.

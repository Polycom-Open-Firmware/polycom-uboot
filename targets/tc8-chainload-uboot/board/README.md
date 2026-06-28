TC8 DDR config is byte-for-byte identical to C60's, minus the 5 trailing
ANATOP/CCM register writes the C60 fork appended (see
`re/c60_ddr_extraction.md`). `lpddr4_timing.c` is a forked copy of the C60
table; `lpddr4_timing.h` is a symlink to the C60 header (shared, verified).
If TC8 needs anything board-specific later, fork the header too as needed.

This is the TC8 **chainloaded stage-2** target (`tc8-chainload-uboot`):
unsigned U-Boot 2024.04 loaded from the eMMC `boot1` HW partition by the
stock signed stage-1, booting via the unlocked NXP `boota` path. The wider
spec lives in the repo-root `UNLOCK_SPEC.md`; the bulk of the board logic
(GT9271 gesture/bootsel, panel, `osprep`, boota fixes) is in
`../uboot-overlay/`, not here — this `board/` dir only carries the DDR table.

# C60 PMIC discrepancies — u-boot vs kernel/silicon (work list)

**For:** whoever brings u-boot's PMIC handling in line with the kernel.
**Date:** 2026-07-03. **Source of truth:** live register dumps of the SAME board
under u-boot (dark) vs mainline-Linux (display lit), plus the STOCK Polycom DTB
extracted from eMMC `dtbo_b` (decompiled at `aibox:/root/c60-flash/c60-stock.dts`).

## The headline bug

**The C60's PMIC is a ROHM BD71847. Everything we ship treats it as a BD71837.**

- Stock Polycom DTB: `pmic@4b { compatible = "rohm,bd71847"; }` (+ `rohm,reset-snvs-powered`).
- Our u-boot SPL runs the legacy `power_bd71837_init()` (37-only register map).
- Our u-boot DTS (`imx8mm-polycom-kepler-proto1.dts`) says `Rohm BD71837` in comments; no PMIC node is actually used by the DM driver in SPL.
- Our **mainline kernel DTS** (c60-kernel-patches 0001) inherits the EVK's `rohm,bd71837` node — also wrong; the kernel "works" by luck of SPL defaults but drives phantom registers (regulator_summary shows buck7/buck8/ldo7 that don't exist; its writes to them are dropped by silicon and faked by the regmap cache — verified live: writes to 0x07/0x08/0x1E bounce).
- Difference that matters: BD71837 = 8 bucks + 7 LDOs; **BD71847 = 6 bucks + 6 LDOs**, different voltage tables for BUCK3 (was "1st NODVS"), BUCK5, LDO5; different range bits. Stock C60 constrains: BUCK1 0.7–1.3 (boot/always-on), BUCK2 0.7–1.3 (dvs run=1.0 idle=0.9), BUCK3 0.7–1.35, BUCK4 3.0–3.3, BUCK5 1.605–1.995, BUCK6 →1.4, LDO1 1.6–1.9, LDO2 →0.9, LDO3 1.8–3.3, LDO4 0.9–1.8, LDO6 0.9–1.8 (all boot-on + always-on; **no LDO5, no LDO7 nodes**).

The TC8 is NOT affected: its stock DTB is a genuine `rohm,bd71837`, and its
chainloaded stage-2 never runs our SPL anyway (stock stage-1 owns PMIC init).
The shared `power_bd71837_init()` is correct for TC8. **Only the C60 target
needs the 47 treatment — keep the fix target-scoped.**

## Measured register deltas (u-boot-after-our-SPL vs Linux-lit, chip @ i2c1 0x4b)

True BD71847 map (kernel `rohm-bd718x7.h` common regs). "Fix" = what a
kernel-equivalent u-boot should do.

| Reg | Name (47) | u-boot | Linux | Fix |
|---|---|---|---|---|
| 0x05 | BUCK1_CTRL | 0x40 | 0xC1 | Set SEL+EN (SW-control) like the kernel driver `.init`; rail is on via HW state machine either way — cosmetic but do it for parity |
| 0x06 | BUCK2_CTRL | 0x40 | 0xC1 | same |
| 0x07/0x08 | *(37-only BUCK3/4_CTRL — PHANTOM on 47)* | 0x00, write-bounces | 0x01 *(regmap-cache fiction)* | **Do nothing; never write** |
| 0x09–0x0C | 1ST–4TH NODVS BUCK_CTRL (= 47's BUCK3–6) | 0x00 | 0x01 | Set EN like kernel |
| 0x11 | BUCK2_VOLT_IDLE | 0x0A (0.8 V) | 0x14 (0.9 V) | Write 0x14 — stock DTS says `dvs-idle = 0.9V` |
| 0x14 | 1ST_NODVS_VOLT (47 BUCK3 = VDD_DRAM) | 0x83 | 0x83 | equal — but **verify 0x83 decodes to the intended DRAM voltage with the 47's BUCK3 table** (37's BUCK5 and 47's BUCK3 encode differently; kernel driver `bd71847_buck3_volts` is the reference). It works (DDR trains), so likely fine — confirm and document |
| 0x18 | LDO1_VOLT | 0x22 (OFF) | 0x62 (EN, 1.8 V) | **Enable** (bit6) — stock: always-on 1.6–1.9 |
| 0x19 | LDO2_VOLT | 0x20 (OFF) | 0x60 (EN, 0.8/0.9 SNVS) | **Enable** |
| 0x1A | LDO3_VOLT | 0x00 (OFF) | 0x40 (EN, 1.8 V) | **Enable** — 1.8 V analog rail, stock always-on. Panel-relevant suspect |
| 0x1B | LDO4_VOLT | 0x00 (OFF) | 0x40 (EN, 0.9 V) | **Enable** — stock always-on. Panel/PHY-relevant suspect |
| 0x1C | LDO5_VOLT | 0x8F | 0x8F | equal; note 47's LDO5 uses a range bit (0x20) the 37 lacks — verify decode once, then leave |
| 0x1D | LDO6_VOLT | 0x43 | 0x43 | equal (EN + 1.2 V-ish) |
| 0x1E | *(37-only LDO7 — PHANTOM on 47)* | 0x00, write-bounces | 0x80 *(cache fiction)* | **Do nothing; never write** |
| 0x24/0x25/0x27 | MVRFLTMASK2 / RCVCFG / PWRONCONFIG0 | 0x16/0x00/0x00 | 0x30/0x4C/0x16 | Investigate: not written by our SPL (PWRONCONFIG1 is 0x28). Likely kernel-driver/stock-DT config (`rohm,reset-snvs-powered` sets PWRONCONFIG). Decide per datasheet whether SPL should match; at minimum document |

## Work items (ordered)

1. **u-boot SPL (`targets/c60-kepler_proto1/uboot-overlay/board/freescale/imx8mm_evk/spl.c`)**
   Replace the C60's `power_init_board()` BD71837 path with a BD71847-correct
   sequence. Options: (a) keep legacy pmic API but write registers per the table
   above (unlock REGLOCK 0x2F→0x00 for VREG writes, do the volt/EN writes,
   restore REGLOCK), or (b) switch SPL to the DM PMIC driver — u-boot's
   `drivers/power/pmic/bd71837.c` **already supports `rohm,bd71847`**
   (ROHM_CHIP_TYPE_BD71847) with a proper regulator driver alongside; add the
   pmic node (from stock DTS) to the u-boot DT with SPL bootph tags. (b) is the
   clean end-state; (a) is the quick parity fix.
   Keep existing correct writes: BUCK1_VOLT_RUN=0x0F (0.85 VDD_SOC),
   BUCK2_VOLT_RUN=0x1E (1.0 VDD_ARM), 0x14=0x83 (DRAM), PWRONCONFIG1=0x0,
   final REGLOCK=0x1 (voltage regs left unlocked — stock BSP kernel needs this
   for BUCK2 I2C-DVS; see comment in spl.c).

2. **Remove the board-code PMIC hacks in `imx8mm_evk.c`** — they were display-
   debug era and are now superseded/mislabeled:
   - the "enable BD71837 LDO6 + LDO7 (0x1d/0x1e)" block — 0x1E is phantom; 0x1D
     already correct;
   - the "LDO1-4 enable (0x18..0x1B)" block in `c60_display_test` — RIGHT IDEA,
     wrong place; move into the SPL sequence (item 1) and delete here;
   - any REGLOCK pokes at 0x2b/0x2a (bogus addresses; real REGLOCK = 0x2F).

3. **u-boot DTS**: fix the `Rohm BD71837` comment; if going route (b), add the
   full `rohm,bd71847` pmic node with the stock constraint set.

4. **Kernel DTS (c60-kernel-patches 0001)** — the "in line with the kernel"
   direction needs the kernel itself corrected: replace the inherited EVK
   `rohm,bd71837` node with `rohm,bd71847` + `rohm,reset-snvs-powered` + the
   stock constraints listed above (mainline `bd718x7` driver supports the 47).
   Today's kernel drives phantom regulators and mislabels rails; it must be the
   reference u-boot converges to. Sanity-check after: `regulator_summary` should
   show 6 bucks/6 LDOs (no buck7/8, no ldo7) and the same measured registers.

5. **Verify on bench** (tooling exists): boot u-boot, dump 0x00–0x30 via
   `i2c read 0x4b 0x00.1 0x30 <addr>` + fastboot getvar trick, diff against the
   Linux column above — target is byte-equality on all non-phantom registers.
   Then boot Linux and re-dump to confirm nothing regressed.

## Context / provenance

- Everything here was established during the display bring-up hunt (see
  session notes / `c60-uboot-backlight-hunt` memory). The LDO3/LDO4-off
  discovery is also the current prime suspect for the still-open
  "panel black, backlight lit" issue (enabling them post-hoc didn't light the
  panel, but the panel powers up BEFORE they're enabled in the current hack
  placement — the SPL-time fix in item 1 changes that ordering, so re-test the
  panel after item 1 lands).
- Phantom-register proof: writes to 0x07/0x08/0x1E bounce on silicon while
  Linux debugfs shows values — regmap cache, not hardware.
- TC8 stock DTB PMIC: `rohm,bd71837` (verified from stock TC8 kernel image DTB).

## BENCH RESULT (2026-07-03) — legacy pmic_reg_write does NOT stick

Enabling LDO1-4 from SPL via the legacy `pmic_reg_write()` in `power_init_board`
(BD71837 path) was tested: **the writes did not land** — readback of 0x18-0x1B
stayed `0x22/0x20/0x00/0x00` (unchanged OFF), and the panel stayed dark.
Confirms item 1 route (a) "keep legacy pmic API" is INSUFFICIENT on the 47 —
use the DM `rohm,bd71847` driver route (b), which manages REGLOCK and the
correct register set. So the panel-black / rail-ordering hypothesis is still
UNTESTED (the rails were never actually enabled). Retest after route (b) lands.

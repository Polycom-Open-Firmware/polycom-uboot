# C60 PMIC discrepancies — u-boot vs kernel/silicon

This document records where u-boot's PMIC handling diverges from the kernel and
the silicon, and the corrections required to converge them. The tables compare
the u-boot register state (display dark) against the mainline-Linux register
state (display lit) on the same silicon, with the stock Polycom DTB as the
reference for the intended configuration.

## The headline bug

**The C60's PMIC is a ROHM BD71847. The firmware treats it as a BD71837.**

- Stock Polycom DTB: `pmic@4b { compatible = "rohm,bd71847"; }` (+ `rohm,reset-snvs-powered`).
- The u-boot SPL runs the legacy `power_bd71837_init()` (37-only register map).
- The u-boot DTS (`imx8mm-polycom-kepler-proto1.dts`) says `Rohm BD71837` in comments; no PMIC node is actually used by the DM driver in SPL.
- The **mainline kernel DTS** (`poly-kernel-patches` `patches/c60/0001…`) inherits the EVK's `rohm,bd71837` node — also wrong: the kernel functions on the SPL's rail defaults but drives phantom registers (regulator_summary shows buck7/buck8/ldo7 that don't exist; its writes to them are dropped by silicon and faked by the regmap cache — writes to 0x07/0x08/0x1E bounce).
- Difference that matters: BD71837 = 8 bucks + 7 LDOs; **BD71847 = 6 bucks + 6 LDOs**, different voltage tables for BUCK3 (was "1st NODVS"), BUCK5, LDO5; different range bits. Stock C60 constrains: BUCK1 0.7–1.3 (boot/always-on), BUCK2 0.7–1.3 (dvs run=1.0 idle=0.9), BUCK3 0.7–1.35, BUCK4 3.0–3.3, BUCK5 1.605–1.995, BUCK6 →1.4, LDO1 1.6–1.9, LDO2 →0.9, LDO3 1.8–3.3, LDO4 0.9–1.8, LDO6 0.9–1.8 (all boot-on + always-on; **no LDO5, no LDO7 nodes**).

The TC8 is NOT affected: its stock DTB is a genuine `rohm,bd71837`, and its
chainloaded stage-2 never runs this SPL anyway (stock stage-1 owns PMIC init).
The shared `power_bd71837_init()` is correct for TC8. **Only the C60 target
needs the BD71847 treatment.**

## Measured register deltas (u-boot after SPL vs Linux-lit, chip @ i2c1 0x4b)

True BD71847 map (kernel `rohm-bd718x7.h` common regs). "Fix" = what a
kernel-equivalent u-boot should do.

| Reg | Name (47) | u-boot | Linux | Fix |
|---|---|---|---|---|
| 0x05 | BUCK1_CTRL | 0x40 | 0xC1 | SEL+EN (SW-control), as the kernel driver `.init` sets; the rail is on via the HW state machine either way, so this is cosmetic parity |
| 0x06 | BUCK2_CTRL | 0x40 | 0xC1 | same |
| 0x07/0x08 | *(37-only BUCK3/4_CTRL — PHANTOM on 47)* | 0x00, write-bounces | 0x01 *(regmap-cache fiction)* | **Do nothing; never write** |
| 0x09–0x0C | 1ST–4TH NODVS BUCK_CTRL (= 47's BUCK3–6) | 0x00 | 0x01 | Set EN like kernel |
| 0x11 | BUCK2_VOLT_IDLE | 0x0A (0.8 V) | 0x14 (0.9 V) | Write 0x14 — stock DTS says `dvs-idle = 0.9V` |
| 0x14 | 1ST_NODVS_VOLT (47 BUCK3 = VDD_DRAM) | 0x83 | 0x83 | Equal. 0x83 decodes to the intended DRAM voltage under the 47's BUCK3 table (37's BUCK5 and 47's BUCK3 encode differently; kernel driver `bd71847_buck3_volts` is the reference); DDR trains, confirming the encoding |
| 0x18 | LDO1_VOLT | 0x22 (OFF) | 0x62 (EN, 1.8 V) | **Enable** (bit6) — stock: always-on 1.6–1.9 |
| 0x19 | LDO2_VOLT | 0x20 (OFF) | 0x60 (EN, 0.8/0.9 SNVS) | **Enable** |
| 0x1A | LDO3_VOLT | 0x00 (OFF) | 0x40 (EN, 1.8 V) | **Enable** — 1.8 V analog rail, stock always-on; a panel-relevant rail |
| 0x1B | LDO4_VOLT | 0x00 (OFF) | 0x40 (EN, 0.9 V) | **Enable** — stock always-on; a panel/PHY-relevant rail |
| 0x1C | LDO5_VOLT | 0x8F | 0x8F | Equal. The 47's LDO5 uses a range bit (0x20) the 37 lacks |
| 0x1D | LDO6_VOLT | 0x43 | 0x43 | equal (EN + 1.2 V-ish) |
| 0x1E | *(37-only LDO7 — PHANTOM on 47)* | 0x00, write-bounces | 0x80 *(cache fiction)* | **Do nothing; never write** |
| 0x24/0x25/0x27 | MVRFLTMASK2 / RCVCFG / PWRONCONFIG0 | 0x16/0x00/0x00 | 0x30/0x4C/0x16 | Not written by the SPL (PWRONCONFIG1 is 0x28). The Linux values come from kernel-driver/stock-DT config (`rohm,reset-snvs-powered` sets PWRONCONFIG) |

## The legacy `pmic_reg_write` does not stick on the BD71847

Enabling LDO1-4 from SPL via the legacy `pmic_reg_write()` in `power_init_board`
(the BD71837 path) does not land: readback of 0x18-0x1B stays `0x22/0x20/0x00/0x00`
(unchanged OFF) and the panel stays dark. A correct SPL sequence uses the DM
`rohm,bd71847` driver, which manages REGLOCK and the 47's register set. With
LDO3/LDO4 off, the panel powers up before its rails are enabled, which causes
the "panel black, backlight lit" symptom.

The correction is an SPL PMIC rewrite to the `rohm,bd71847` driver, removal of
the board-code rail hacks in `imx8mm_evk.c`, and the matching kernel-DTS
change so u-boot and the kernel agree on the part. It requires hardware
verification: `regulator_summary` should show 6 bucks / 6 LDOs (no buck7/8, no
ldo7) and register byte-equality against the Linux column above.

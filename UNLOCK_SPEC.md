# polycom-uboot — Fully-Unlocked Firmware Spec

This spec defines the unlocked stage-2 U-Boot for the HAB-closed TC8. HAB gates
stage-1 only, so the signed stock bootloader (U-Boot 2018.03) is left in place
and chainloads an unsigned U-Boot 2024.04 from the eMMC `boot1` HW partition;
the chain persists through the stock bootloader's rewritable, CRC-only env. The
stage-2 boots unsigned Android-format slot images via the NXP FSL `boota` path
forced unlocked. Covered here: the chainload mechanism and anti-brick
guarantees, the GT9271 touch-gesture boot selector (F1), the `boota`/legacy-v0
image fixes, in-u-boot DHCP-66 networking (F3), the display splash (F2), and env
persistence. The C60 (HAB-open) is the working reference the design tracks but
is not lockstep.

## 0. Goal & philosophy

Owner has total control of an e-waste-liberated Polycom i.MX 8M Mini
device. Nothing the vendor did to lock the box survives; bricking is
always recoverable via BootROM SDP; per-device factory identity
(`ethaddr`, `cert`, `presistdata`) is never trampled. Base tree is the
**NXP `nxp-imx/uboot-imx`** fork (pinned tag, vendored), driven through
the `targets/` + `uboot-overlay/` pattern established on C60.

## 1. Locked decisions

| # | Decision | Choice |
|---|----------|--------|
| D1 | AVB / boot-image | Boot via NXP FSL `boota` run **UNLOCKED** (AVB forced `FASTBOOT_UNLOCK`) so one path boots **unsigned** Android-format slot images (stock Android + the Linux slot). Keep stock GPT; flash into existing slots. |
| D2 | Persistence | **Flash to eMMC (persistent)** is the goal; SDP-RAM-load validates first. The SDP-recovery copy stays untouchable. |
| D3 | Console | **Always-open, no password.** Non-zero `bootdelay`, any-key interrupt, unconditional shell. |
| D4 | Target focus | **TC8-first** (`tc8-chainload-uboot`). C60 stays the working reference, not lockstep. TC8 delivery is the stage-2 chainload, not UUU. |

New feature requirements:

| # | Feature | Summary |
|---|---------|---------|
| F1 | **Touch boot-selector** | Read Goodix **GT9xx** finger-count at power-on: **5 → SDP/UUU**, **4 → fastboot** (web provisioner), none → normal `bootcmd`. |
| F2 | **Display splash/icon** | Bring panel up in u-boot, show a boot icon. **Staged after F1** (see §5). |
| F3 | **DHCP-66 netboot** | `bootcmd` does DHCP; if opt-66 (TFTP server) + opt-67 (bootfile) present, TFTP the image and boot it; else fall through to local boot. |

## 2. Boot decision flow (target `bootcmd`)

```
stock signed stage-1 (boot0)  ── persisted env bootcmd: mmc read boot1 → go
  └─ chainloaded stage-2 (U-Boot 2024.04, in eMMC boot1):
       bootcmd = run gesture_sel; osprep; run dhcp66_boot; run mmcboot
            1. GESTURE SAMPLE (F1)  ── GT9271 I2C touch-count, debounced
                 5 fingers → enter BootROM SDP (USB-download)  [hard escape]
                 4 fingers → fastboot gadget (web provisioner entry)
                 else      → continue (skip_net unset)
            2. osprep  ── clear panel + stop eLCDIF + re-latch GT9271 → 0x5d
            3. DHCP-66 (F3, unless gesture skip)
                 dhcp; if ${serverip} && ${bootfile} → tftp → boot   (wins)
            4. LOCAL BOOT
                 mmcboot = `boota` (unlocked) — active GPT slot via the misc
                 bootctrl: boot_<slot> + dtbo_<slot> + vbmeta_<slot> → booti
            5. FALLBACK
                 console; last resort = SDP re-entry
```

Precedence: **gesture > DHCP-66 > local slot (boota) > fallback/SDP.**
Gesture is sampled before the network so a stuck/rogue DHCP can always be
overridden by hand on a button-less chassis.

## 3. Feature matrix (baseline vs. work)

Legend: ⚙ = defconfig/env only · ✎ = new code/DTS · ✓ = already in base
overlay (C60-proven)

### Lock / verification removal — D1 (unlocked `boota`)
The FSL Android stack is kept and **forced UNLOCKED** rather than removed — one
path boots unsigned images.
| Item | How | St |
|------|-----|----|
| AVB present but forced UNLOCKED | `fastboot_get_lock_stat()→FASTBOOT_UNLOCK` stub; `vbmeta --algorithm NONE`; the rootfs has no dm-verity | ✎ |
| Keep `boota` (unlocked) | defconfig keeps `ANDROID_SUPPORT,ANDROID_AB_SUPPORT,CMD_BOOTA,SHA256`; `mmcboot=boota` | ⚙✎ |
| BCB / misc A-B slot select | KEPT — `boota` reads the active slot from `misc` bootctrl; `fastboot set_active` flips it | ⚙ |
| Fastboot lock-state gate | the unlock stub IS the gate, pinned to UNLOCK — `fb_fsl/fastboot_lock_unlock.c` | ✎ |
| No anti-rollback | unlocked AVB tolerates the signature/rollback mismatch and boots | ✎ |
| No Polycom `is_usb_boot`→fastboot trap | clean port lacks it; assert in bootcmd | ✓ |
| Env RW, `saveenv` works | `ENV_OVERWRITE=y`, `ENV_IS_IN_MMC` @0x700000 | ✓ |
| Keep stock GPT (no repartition) | flash Linux into an existing slot (A or B) + `userdata`; never `gpt write` | ✎ |
| HABv4 (C60) | left **open** (unsigned SPL loads via SDP). On TC8 HAB is **closed** → stage-1 untouched; an unsigned stage-2 chainloads from boot1 instead | ✓ |

### Owner access — D3
| Item | How | St |
|------|-----|----|
| Always-on serial console | `DM_SERIAL`+`MXC_UART`, stdout=console UART | ✓ |
| Interruptible autoboot, no pw | `BOOTDELAY` ≥ 3, no `AUTOBOOT_KEYED`/password | ⚙ |
| `fastboot`+`ums`+`dfu` no unlock | `CI_UDC`,`CMD_FASTBOOT`,`CMD_USB_MASS_STORAGE`,`CMD_DFU` | ✓ |
| SDP/uuu RAM-load first-class | `SPL_USB_SDP_SUPPORT`,`CMD_USB_SDP` + BMOD=00 | ✓ |
| `usbg1` DM gadget node | TC8 u-boot DTS: `fsl,imx27-usb-gadget`+`chipidea,usb`→`&usbotg1`, `dr_mode=peripheral` | ✎ |
| SDP escape from u-boot & Linux | bootcmd cmd + Linux `hab_failsafe` SIP (documented) | ✓ |

### Boot flexibility
| Item | How | St |
|------|-----|----|
| Stage-2 owns `bootcmd` | env: `gesture_sel; dhcp66_boot; distro_bootcmd; bsp_bootcmd` | ✎ |
| eMMC/USB/SD/TFTP/NFS/FIT/booti | `DISTRO_DEFAULTS`,`FIT`,`CMD_DHCP`,`CMD_FS_GENERIC` | ✓ |
| A/B kept, owner-picked, no forced rollback | env `target_slot`, no BCB | ✎ |

### F1 GT9xx gesture selector

**Stock GT9xx wiring (from the stock DT):** `compatible="goodix,gt9xx"`,
**`reg=<0x14>`** (I2C address 0x14 — Goodix latches 0x14 when **INT is HIGH** at
reset-release, 0x5d when low), **`reset-gpios=<&gpio2 3 GPIO_ACTIVE_LOW>`**
(GPIO2_IO3), **`irq-gpios=<&gpio1 9 ...>`** (GPIO1_IO9), `interrupts=<9 2>`,
**`goodix,driver-send-cfg=<1>`** + **`goodix,cfg-group2=[50 20 03 … 09 01]`**
(186-byte config the host must push), `touchscreen-size-x=0x320` (800),
`-y=0x500` (1280). The controller ACKs I2C but its firmware does not scan/report
(status `0x814E` stays 0) until it is brought up at **0x14** (INT high at reset)
and the **cfg-group2** config is written (Goodix cfg reg `0x8047`). Reads at
0x5d without config return zero touches.

**Gesture bring-up recipe** (in `imx8mm_evk.c`:
`gt9271_reset`/`gt9271_send_cfg`/`gt9271_cfg[]`/`do_bootsel`):
1. Canonical Goodix GTP `gtp_reset_guitar` timing: RST low, hold **20 ms**, INT
   **HIGH** (→ addr **0x14**; low = 0x5d), **udelay 2 ms**, RST high, **6 ms**,
   RST→input.
2. `gtp_int_sync`: INT low → **50 ms** → INT→input, then 50 ms settle. Stock has
   `goodix,int-sync=<1>`; without it the GT9xx never produces coordinate frames
   (0x814E stays 0) even with correct address and config.
3. Push the stock 186-byte `cfg-group2` to reg **0x8047** at 0x14 (checksum /
   `config_fresh` already valid).
4. Poll `0x814E` (bit7 ready, low nibble = count); settle ~1 s on the MAX count
   before acting (fingers land staggered).

**Boot-UX:** the chainloaded stage-2 shows a rotated submarine BMP on black
(no VIDEO_LOGO corner), opens a selection window (20 s bench, ~4 s production),
samples the GT9271 gesture, settles ~2 s on the max count (catches staggered
landing), swaps to the selected mode's rotated icon, and runs the action.
Mappings: **5 fingers → SDP/UUU** (`sdp 0`), **4 → fastboot** (`fastboot usb 0`,
the web provisioner entry), none → normal `bootcmd`. The mode icons are 256²
24bpp BMPs compiled into the stage-2 binary (`.rodata`, `tc8_logos.h`) and
displayed straight from their address — nothing is read from flash.

**On-eMMC placement:** stage-2 lives in the eMMC **`boot1` hardware
partition**, outside the user area; the stock A/B GPT is untouched and
nothing of the project's sits in it. Layout authority:
`poly-firmware-build` `FLASHING.md`. The gesture/mode icons are compiled
into the stage-2 binary (`.rodata`) — nothing else is stored on flash.
Field updates rewrite `boot1` through the config-blob mechanism
(`poly-firmware-build` `CONFIG-PARTITION.md`), sha256-gated with read-back
verify.

**Auto-persist:** the stock env (byte 0x400000 / LBA 0x2000, CRC32-only, not
HAB-signed) `bootcmd` is rewritten to chainload stage-2 from `boot1`:

```
setenv bootcmd 'mmc dev 1 2\; mmc read 0x40200000 0 0x1400\; mmc dev 1 0\;
  dcache flush\; icache off\; dcache off\; go 0x40200000'
saveenv
```

Stock `saveenv`s the live env every boot, so the rewritten `bootcmd`
self-perpetuates. Stage-2's own forced bootcmd is
`run gesture_sel; osprep; run dhcp66_boot; run mmcboot` — `gesture_sel`
picks the mode (a gesture, else the last sticky selection: default eMMC
sets `skip_net=1` so dhcp66 is skipped; a sticky netboot selection clears
it). `mmcboot` is force-set to `boota` — the NXP Android slot boot of
`boot_a`/`boot_b` — because the stock env defines a conflicting `mmcboot`
(see §6). Recovery: the previous `bootcmd` is captured
before the rewrite; `bootdelay` stays 3 so stock is re-catchable
(`setenv bootcmd …; saveenv`); or restore via the env image at byte 0x400000;
or BootROM SDP (BMOD strap, HAB-independent).

| Item | How | St |
|------|-----|----|
| Minimal GT9xx I2C reader | new board hook: I2C@0x14 bus-1, read status reg → buffer-status + touch-count nibble; rst/irq GPIO bring-up; ~150 ms debounce window | ✎ |
| Map count→action | **5→`sdp 0` (SDP/UUU); 4→`fastboot usb 0` (web provisioner); else normal** | ✎ |
| Diagnostic print | prints `gesture: N finger(s) → <action>` | ✎ |

### F3 DHCP-66 netboot
| Item | How | St |
|------|-----|----|
| DHCP + opt66/67 | `CMD_DHCP` populates `serverip`/`bootfile` from opt 66/67 | ✓ |
| `dhcp66_boot` env | `dhcp; test -n ${serverip} -a -n ${bootfile} && tftp ${loadaddr} ${bootfile} && booti/bootm` | ✎ |
| Skipped on gesture | gesture sets a flag the stanza checks | ✎ |

**Switch inheritance:** TC8 wires the i.MX8MM FEC (ENET1, RGMII) to an
**RTL8363NB-VB** L2 switch (chip-id `0x1cc943`, MDIO `0x1d`, reset GPIO5_IO02
active-low). The signed stock stage-1 fully initialises the switch (jam-table +
8051 firmware + transparent forwarding — `Switch: RTL8363NB-VB(0x1cc943)`) and
chainloads via `go`, which does not reset the chip, so stage-2 inherits a
configured transparent switch. Stage-2 networking therefore needs only
`CONFIG_PHY_FIXED=y`; `&fec1 status="okay"` + `pinctrl_fec1` (standard i.MX8MM
RGMII pad group) + `phy-mode="rgmii-id"` + a `fixed-link` 1G/full subnode; and
it **must never drive the eth-reset GPIO (GPIO5_IO02)** — pulsing it wipes the
stock init and stage-2 carries no replacement. No MDIO/switch node is needed
(the FEC sees a fixed link; the chip forwards CPU↔wire in hardware). Files:
`imx8mm-polycom-proline-exec.dts` (`&fec1` + `pinctrl_fec1`), defconfig
`CONFIG_PHY_FIXED=y`. At the stage-2 prompt this yields `Net: Switch:
RTL8363NB-VB(0x1cc943)` / `eth0 active`, a DHCP lease obtained, and the gateway
reachable. `CONFIG_NET_RANDOM_ETHADDR=y` yields a different MAC each boot; set a
stable `ethaddr` for DHCP reservations. This is the u-boot path; the kernel-side
RTL8363NB-VB support (DSA/realtek-mdio, FEC-MDIO re-init) is separate.

### F2 display splash (staged — §5)
| Item | How | St |
|------|-----|----|
| LCDIF + Samsung DSIM + Raydium-class panel | `VIDEO_MXS`,`VIDEO_IMX_SEC_DSI`,`VIDEO_LCD_RAYDIUM_RM67191` (NXP downstream drivers, in base) | ✓ (driver) / ✎ (TC8 DTS+timing) |
| Splash bitmap | `SPLASH_SCREEN`,`VIDEO_LOGO`,`CMD_BMP`,`BMP_*BPP` | ✓ |
| TC8 panel node + PWM4 backlight + reset GPIO2_IO05 | TC8 u-boot DTS | ✎ |

## 4. Anti-brick guarantees (non-negotiable)

1. **BMOD=00 USB-download always wins** — bypasses u-boot+AVB+BCB+touch+UART (BootROM-level). Documented escape on a button-less chassis.
2. **Never overwrite the SDP-recovery SPL/u-boot copy.** On TC8 the active u-boot is in `mmcblk2boot0` HW partition; the user-area SDP copy at eMMC `0x8400` is the rescue and is off-limits to the reflasher.
3. **Per-device identity preserved** — `ethaddr` (env, not fuses), stock `cert` (#9), `presistdata` (#12). MAC restore logic retained.
4. **A bad env/bootcmd can't lock out recovery** — gesture sample + console interrupt run before any env-scripted boot; SDP is reachable regardless of env state.

## 5. Display staging & de-risk

Display-in-u-boot inherits the **same hardware constraints** as the kernel
bring-up (Samsung-DSIM PMS, LCDIF↔DSIM clock coherence). Therefore:

- **F1 ships first, display-independent.** Gesture selection is
  serial-reported; a button-less device is fully controllable with zero
  pixels.
- **F2 layers on after** the panel pipeline is verified on hardware.

The NXP `uboot-imx` base **already ships** `VIDEO_IMX_SEC_DSI` (Samsung DSIM) +
`VIDEO_LCD_RAYDIUM_RM67191` + `SPLASH_SCREEN`, and the C60 unlock defconfig
enables them — NXP's own untouched downstream sec-dsi/raydium driver, the same
code being ported into the mainline kernel. In u-boot it may come up with only
the correct DTS + panel timing, **decoupled from the mainline-kernel
DRM/runtime-PM deadlock**. Treat u-boot display as a separate, possibly-easier
bring-up.

## 6. Implementation map

- **Defconfig** `targets/tc8-chainload-uboot/uboot-overlay/configs/polycom_proline_exec_defconfig`
  — forks C60's. For the unlocked `boota` path, **keep/enable** the FSL Android
  stack: `ANDROID_SUPPORT, ANDROID_AB_SUPPORT, CMD_BOOTA, AVB_SUPPORT, LIBAVB,
  SHA256` — AVB is left in but forced UNLOCKED by the
  `fastboot_get_lock_stat()→FASTBOOT_UNLOCK` overlay stub (so unsigned slot
  images boot); `USB_GADGET_OS_DESCRIPTORS=y` (shared `f_fastboot` WinUSB
  gadget); **add**: `BOOTDELAY=3`; **also keep**:
  FIT/DISTRO/VIDEO/SEC_DSI/RAYDIUM/SPLASH/CI_UDC/CMD_DHCP/CMD_TFTPBOOT/I2C/
  USDHC/FEC.
- **DTS** `targets/tc8-chainload-uboot/uboot-overlay/arch/arm/dts/imx8mm-polycom-proline-exec.dts`
  — mirror C60 minimal DTS + add: GT9xx `i2c@0x14` (bus-1) w/ rst+irq
  GPIOs; panel/DSIM/PWM4-backlight/reset-GPIO2_IO05; correct console
  UART (from stock TC8 DTB); PMIC; `usbg1` gadget; eMMC USDHC.
  `lpddr4_timing.c` is a forked copy of the C60 LPDDR4 table; `lpddr4_timing.h`
  is a symlink to the C60 header.
- **Gesture hook** — board late-init (or pre-`bootcmd` env via a small
  C cmd `do_gesture`): GT9xx reset/irq, I2C status-reg read, count map.
- **Env** — `uboot-overlay` env additions: `gesture_sel`, `sdp_enter`,
  `emmc_only`, `dhcp66_boot`, `target_slot`; rewire `bootcmd`.

Stage-2 build notes (no-SPL u-boot proper, chainloaded):

- **No-SPL DRAM detect** — with no SPL, `save_boot_params()` captures garbage
  boot-arg registers so `rom_pointer[1]` is junk-nonzero; NXP imx8m `soc.c` then
  computes `gd->ram_size = PHYS_SDRAM_SIZE - rom_pointer[1]`, underflowing to
  ~16 EiB and wedging relocation. Board `board_phys_sdram_size()` zeroes
  `rom_pointer[0..1]` and sets `*size = PHYS_SDRAM_SIZE`; it runs at the top of
  both `dram_init()` and `dram_init_banksize()`, before their `rom_pointer[1]`
  checks.
- **Stage-2 artifact = `vendored/uboot-imx/u-boot.bin`** (no-SPL u-boot proper,
  entry@0, self-relocating), not `out/.../flash.bin` — flash.bin's byte 0 is the
  imx boot-container header, so `go` faults (`esr 0x02000000`). It is staged to
  the eMMC `boot1` hardware partition (LBA 0).
- **Chainload MMU teardown** — stock `go` does not tear down its MMU/caches, so
  stage-2 would run under stock page tables (no display map) and fault
  (`esr 0x96000006`); the chainload sequence issues `dcache flush; icache off;
  dcache off` before `go`.
- **Bootsel WDT service** — the gesture poll-loop and settle-loop must service
  the i.MX WDOG (armed by stock/ATF): `#include <cyclic.h>` + `schedule();` as
  the first statement of both loops in `imx8mm_evk.c`, else the SoC resets
  mid-window and re-chainloads.
- **`booti` display teardown** — `announce_and_cleanup()` calls
  `video_link_shut_down()` before the kernel jump, cascading
  `imx_sec_dsim_remove()` (panel remove + `dsi_host_disable()`) + `lcdifv3`
  disable on the live DSI/LCDIF pipeline, which deadlocks the sec-dsim
  host-disable. The overlay `arch/arm/lib/bootm.c` comments out the
  `video_link_shut_down()` call; Linux re-inits the display IP, so leaving the
  pipeline up across the jump is benign.
- **`mmcboot` env override** — stock imx8mm_evk's built-in env already defines a
  distro `mmcboot` targeting the EVK's removable `mmc 2` (absent on TC8, whose
  eMMC is `mmc dev 0` in stage-2), so `board_late_init` force-sets `mmcboot`
  unconditionally (no `!env_get` guard). `gesture_sel`/`dhcp66_boot`/`tc8_bootargs`
  keep their guards (stock does not define them).

## 7. Per-target status

| Target | DDR | defconfig | u-boot DTS | gesture | dhcp66 | splash | on-device |
|--------|-----|-----------|-----------|---------|--------|--------|-----------|
| tc8-chainload-uboot | ✓ (forked .c, symlink .h) | ✓ (unlocked boota) | ✓ | ✓ | ✓ | ✓ (panel up) | chainload + gesture + boota in place |

## 7a. TC8 hardware map — from stock DT [deterministic]

From the stock TC8 DT (`model = "Polycom TC8 (LCC PROTO)"`,
`compatible = "poly,tc8"`). TC8 mirrors C60 closely:

| Block | TC8 stock | vs C60 |
|-------|-----------|--------|
| Console | `serial@30890000` = **UART2** (`ttymxc1`), bootargs empty in stock DT | same (uart2) |
| eMMC | `mmc@30b60000` = **usdhc3**, `non-removable` | same (usdhc3) |
| PMIC | **`rohm,bd71837` @ I2C1 (0x30a20000) 0x4b** | same |
| Touch | **`goodix,gt9271` @ I2C2 (0x30a30000) reg `0x5d`**, `irq-gpios=<ph0x28 9 0>`, size-x=0x320 (800), size-y=0x500 (1280) | C60 differs |
| Panel | `panel@0` under **`dsi@32e10000`** (Samsung sec-dsi), `reset-gpios=<ph0x2f 5 1>` (GPIO IO5 active-low), `backlight`=pwm-backlight | same class |
| DSI | `dsi@32e10000` `samsung,pll-clock-frequency=24MHz` ref, `samsung,esc-clock-frequency=10MHz`; `lcdif@32e00000` | same class |

**Touch address quirk (F1-critical):** the GT9-series I2C address is **0x5d by
default**, **0x14 if INT is held during reset**. The gesture reader brings the
controller up at 0x14 (INT high at reset) and pushes the stock cfg-group2; the
status reg (0x814E) low nibble then gives the touch-point count (see F1). The
0x5d↔0x14 dual-address behaviour requires on-device confirmation.

## 7b. F2 panel bringup — resolved in SW (deterministic)

Panel path implemented and compiling:

- **Driver**: LCC variant added to NXP `raydium-rm67191.c`
  (overlay copy) — new compatible `poly,lcc-proto`, the LCC MCS init tables +
  sequence (Enter Page3/GIP_1-3/Page4/Page1/Page0, TE-on, sleep-out 120 ms,
  display-on 150 ms). `rad_platform_data` gained a per-variant `.timing`.
- **Timing**: **800×1280, pixelclock 76 556 400 Hz**, hfp/hbp=81, hsync=12,
  vfp=8, vbp=18, vsync=4, HSYNC/VSYNC/DE-LOW + PIXDATA-negedge.
- **DTS**: base `imx8mm.dtsi` already carries the lcdif↔dsi graph;
  TC8 DTS enables `&lcdif`, retargets `&mipi_dsi` to the u-boot
  sec-dsi host (`compatible = "samsung,sec-mipi-dsi"`), adds
  `panel@0 "poly,lcc-proto"` (4-lane, burst, `reset-gpio`=&gpio2 5
  ACTIVE_LOW), `&pwm4` + `pwm-backlight` (period 25 000 ns, dflt
  250/255). The rm67191 driver drives the external PWM4 backlight
  (`backlight` phandle resolve + `backlight_enable()` in `enable_backlight`),
  not only the internal DCS brightness.
- **Backlight pad**: DETERMINISTIC — decoded stock `pwm4grp`
  (`<0x64 0x2cc..0x05..0x10>`) = `GPIO1_IO15` ALT5 `PWM4_OUT`,
  pad-conf `0x10`, pwm@30690000.

Still SW-pending for F2: the splash icon (icon → BMP → splash env).
Requires hardware verification: DSIM PMS lock at 76.5564 MHz, the `disp_reset`
pad drive, and actual illumination.

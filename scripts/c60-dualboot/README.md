# C60 dual-boot via uuu (our u-boot, stock-style A/B)

One command, from a C60 in SDP mode (BOOT_MODE both switches off), on the
host that has the C60's USB + UART (aibox):

```
c60-boot          # slot A: our mainline kernel (DSI-disabled) + Debian 12
c60-boot b        # slot B: unmodified stock images (reference)
```

## What it does

1. Waits for the C60 BootROM SDP enum (`1fc9:0134`).
2. `uuu -b spl flash.bin` loads our u-boot into RAM (SPL → ATF → u-boot).
   Our u-boot enumerates as a fastboot gadget (`1fc9:0152`) — USB-boot
   detected, exactly stock NXP behaviour.
3. Interrupts the u-boot autoboot over UART and runs the slot's boot
   sequence: `mmc read` the slot's `boot_<x>` (Android boot.img v0:
   kernel + DTB in `second`), `cp.b` kernel→0x40080000 and DTB→0x46000000,
   `setenv bootargs`, `booti`.

Slot A `system_a` holds our 1.8 GiB Debian 12 ext4 rootfs
(`root=/dev/mmcblk2p5`, `init=/sbin/init`). Slot B is the untouched stock
Android (kernel + system_b); its kernel currently deadlocks in
`imx8_register_cpufreq` (NXP-BSP ATF-SIP mismatch — separate deferred
work), but the A/B *flow* is identical to stock.

## Why not native `boota`?

NXP's `boota` command does all of this (BCB slot select + AVB + dm
cmdline + booti) and our build enables it (`CONFIG_CMD_BOOTA`,
`ANDROID_AB_SUPPORT`, `BCB_SUPPORT`, `AVB_SUPPORT`). It correctly reads
the BCB and writes A/B metadata, but its AVB rejects our test-key
`vbmeta` (`fsl_validate_vbmeta_public_key_rpmb` — no RPMB key
provisioned) even when unlocked, ending in "bad boot image magic". The
`mmc read`+`booti` path skips AVB and is proven. Re-signing vbmeta to
the device-expected key (true `boota`) is a future goodie.

## The u-boot that makes this work

Built from `scripts/build.sh c60-kepler_proto1`. The reproducibility-
critical patches live in `targets/c60-kepler_proto1/uboot-overlay/`
(applied over the gitignored vendored tree at build step [3.0/5]):

- **`usbg1` DM gadget node** in the C60 DTS — `ci-udc-otg` only binds
  `compatible="fsl,imx27-usb-gadget"` + `chipidea,usb` phandle. NXP
  supply this via `imx8mm-evk-u-boot.dtsi`; our standalone DTS had no
  matching `-u-boot.dtsi`, so fastboot died `-19`. **This single node
  is what makes the gadget enumerate.**
- `dr_mode = "peripheral"` on `&usbotg1` (no USB-C TCPC on C60).
- TCPC bypass in `imx8mm_evk.c` `board_usb_init` (C60 has no PTN5110).
- `CONFIG_SYS_MMC_ENV_DEV=0` + `CONFIG_FASTBOOT_FLASH_MMC_DEV=0`
  (C60 eMMC = mmc dev 0; EVK default 1/2 → "device 255 / partition
  size 0").
- `fastboot_dev=mmc` + `target_ubootdev=0` + `emmc_dev=0` in the
  Android env header (else `_fastboot_setup_dev` leaves devinfo
  {0xff,0xff}).
- A53 clock root → arm_pll_out (kills stock-kernel "unlisted 500 MHz").
- BUCK2 1.0 V + REGLOCK left unlocked (stock-kernel DVS).
- Android-AB/AVB/BCB/`CMD_BOOTA` config (native stock-style boota
  available even though we currently use the mmc-read path).

## Slot A image set (flashed once via our u-boot fastboot)

`fastboot flash boot_a/dtbo_a/vbmeta_a/system_a` with the
`c60-firmware-build` `out/emmc/` artifacts, DTB rebuilt with
`&mipi_dsi { status="disabled"; }` (mainline samsung-dsim runtime-PM
deadlocks DRM bringup; headless is fine for HW-RE). `system_a` =
`v010-system.img` (Debian 12). Slot B never touched.

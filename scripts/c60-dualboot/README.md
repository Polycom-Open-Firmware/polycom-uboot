# C60 dual-boot via uuu (RAM-loaded u-boot, stock-style A/B)

From a C60 in SDP mode (both BOOT_MODE switches off), on a host with the
C60's USB and UART attached:

```
c60-boot          # slot A: mainline kernel (DSI-disabled) + Debian 12
c60-boot b        # slot B: unmodified stock images (reference)
```

## What it does

1. Waits for the C60 BootROM SDP enumeration (`1fc9:0134`).
2. `uuu -b spl flash.bin` loads this u-boot into RAM (SPL -> ATF ->
   u-boot). It enumerates as a fastboot gadget (`1fc9:0152`) — the
   standard NXP USB-boot behaviour.
3. Interrupts the u-boot autoboot over UART and runs the slot's boot
   sequence: `mmc read` the slot's `boot_<x>` (Android boot.img v0:
   kernel + DTB in `second`), `cp.b` kernel -> `0x40080000` and
   DTB -> `0x46000000`, `setenv bootargs`, `booti`.

Slot A `system_a` holds a 1.8 GiB Debian 12 ext4 rootfs
(`root=/dev/mmcblk2p5`, `init=/sbin/init`). Slot B is the untouched stock
Android (kernel + `system_b`); its kernel currently stops in
`imx8_register_cpufreq` (NXP-BSP ATF-SIP mismatch, tracked separately),
but the A/B flow is identical for both slots.

## Native boota

NXP's `boota` command performs the same steps natively (BCB slot select
+ AVB + dm cmdline + `booti`), and this build enables it
(`CONFIG_CMD_BOOTA`, `ANDROID_AB_SUPPORT`, `BCB_SUPPORT`, `AVB_SUPPORT`).
Its AVB, however, requires an RPMB-provisioned key
(`fsl_validate_vbmeta_public_key_rpmb`) that is not present, so it
rejects the test-key `vbmeta` even when unlocked. The `mmc read` +
`booti` path skips AVB and is used instead. Re-signing `vbmeta` to the
device-expected key to switch to native `boota` is a possible follow-up.

## u-boot build

Built from `scripts/build.sh c60-kepler_proto1`. The
reproducibility-critical changes live in
`targets/c60-kepler_proto1/uboot-overlay/` (applied over the vendored
tree at build step [3.0/5]):

- **`usbg1` DM gadget node** in the C60 DTS — `ci-udc-otg` binds only on
  `compatible="fsl,imx27-usb-gadget"` plus a `chipidea,usb` phandle. NXP
  supply this via `imx8mm-evk-u-boot.dtsi`; the standalone C60 DTS has no
  matching `-u-boot.dtsi`, so without this node fastboot fails `-19`. The
  node is required for the USB gadget to enumerate.
- `dr_mode = "peripheral"` on `&usbotg1` (no USB-C TCPC on C60).
- TCPC bypass in `imx8mm_evk.c` `board_usb_init` (C60 has no PTN5110).
- `CONFIG_SYS_MMC_ENV_DEV=0` + `CONFIG_FASTBOOT_FLASH_MMC_DEV=0`
  (C60 eMMC = mmc dev 0; the EVK default of 1/2 yields
  "device 255 / partition size 0").
- `fastboot_dev=mmc` + `target_ubootdev=0` + `emmc_dev=0` in the Android
  env header (otherwise `_fastboot_setup_dev` leaves devinfo
  {0xff,0xff}).
- A53 clock root -> arm_pll_out (avoids the stock-kernel "unlisted
  500 MHz").
- BUCK2 1.0 V with REGLOCK left unlocked (stock-kernel DVS).
- Android-AB/AVB/BCB/`CMD_BOOTA` config (native boota available even
  though the mmc-read path is used by default).

## Slot A image set (flashed once via the u-boot fastboot gadget)

`fastboot flash boot_a/dtbo_a/vbmeta_a/system_a` with the
`c60-firmware-build` `out/emmc/` artifacts, DTB rebuilt with
`&mipi_dsi { status="disabled"; }` (mainline samsung-dsim runtime-PM
deadlocks DRM bring-up; a headless DTB is fine for hardware bring-up).
`system_a` is the Debian 12 system image. Slot B is never touched.

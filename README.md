# polycom-uboot

Mainline u-boot for Polycom i.MX 8M Mini devices — currently:

| Build target              | Device                   | Codename          | DRAM         |
|---------------------------|--------------------------|-------------------|--------------|
| `c60-kepler_proto1`       | Trio C60                 | kepler_proto1     | 2 GiB LPDDR4 |
| `tc8-chainload-uboot`     | TC8 conference tablet    | proline_exec      | 2 GiB LPDDR4 |

Both use the same i.MX 8M Mini Quad SoC and
share DDR training configs — but each has its own board file, DT, and any
board-specific quirks. **The two targets are delivered by different
mechanisms** because their BootROM lock state differs (see Goal).

## Goal

Replace the stock Polycom u-boot so the owner has total control of the box.
How we get there depends on the device's HAB (secure-boot) fuse state:

### C60 — HAB open: replace stage-1 directly

1. **SDP rescue path**: with chassis BOOT_MODE switches set to USB-download
   (BMOD=00), `uuu` on the host loads this u-boot into RAM. It controls
   `bootcmd` so the chain becomes hands-off: power-on → SDP enum →
   uuu loads → u-boot autoboots Linux from slot A or B.

2. **Removable Polycom override**: stock u-boot enters fastboot mode whenever
   BootROM detects USB-boot (`is_usb_boot` check at VA 0x40204268).
   This build skips that override, so the loop SDP→u-boot→fastboot
   never traps.

### TC8 — HAB closed: chainload a stage-2 from eMMC boot1

The bench TC8 ships **HAB-closed** (`hab_status` → "Secure boot enabled",
SRK fuses burned), so the BootROM rejects any unsigned stage-1 — we have no
Polycom key and cannot replace it. HAB gates **stage-1 only**, so we instead
run our unsigned **U-Boot 2024.04 as a chainloaded stage-2**:

- **Stage-1** = stock signed bootloader in the eMMC **`boot0`** HW partition.
  Its (CRC-only, unsigned) env `bootcmd` is rewritten to chainload us, so the
  chain auto-persists with no human in the loop.
- **Stage-2** = our U-Boot 2024.04 living in the eMMC **`boot1`** HW
  partition (outside the GPT, so the stock GPT and factory partitions are
  left intact). Stage-1 `mmc read`s it into RAM and `go`es to it.
- **Boot method = unlocked NXP FSL Android `boota`.** AVB is forced
  *unlocked* (a `fastboot_get_lock_stat()→FASTBOOT_UNLOCK` overlay stub), so
  one path boots **unsigned** Android-format images: stock Android in one GPT
  slot, our Linux in the other. Our Linux ships as an Android slot image —
  `boot.img` + `dtbo` (dtb in a DTBO container) + `vbmeta` (`--algorithm
  NONE`); rootfs goes to `userdata` (Android sparse). Switch slots with
  `fastboot set_active`. (We do **not** repartition: the stock GPT stays.)
- **Boot UX**: a bootsel logo + a GT9271 touch-gesture window. A **4-finger
  gesture drops to fastboot** (the web provisioner's entry point); 5 fingers
  → SDP/UUU; none → normal boot. At OS handoff, `osprep` clears the panel and
  stops the eLCDIF (kills u-boot→Linux transition garbage) and re-latches the
  GT9271 to 0x5d so the booted OS's touch driver finds it.
- **Provisioning** is a browser **WebUSB/WebSerial** tool: *enroll* installs
  stage-2 into boot1, *flashos* flashes the slot image. The `f_fastboot` USB
  gadget (WinUSB, `1fc9:0152`) is shared with the FSL fastboot command layer.

See `UNLOCK_SPEC.md` for the full TC8 spec and bench history,
and `Polycom-Open-Firmware/c60-firmware-build` for the kernel/rootfs side.

## Layout

```
targets/
  c60-kepler_proto1/        Polycom Trio C60
    board/                  board.c, spl.c, lpddr4_timing.c
    dts/                    u-boot-side DTS
    ddr/                    extracted DDR config (or copy from upstream)
    target.env              build vars
  tc8-chainload-uboot/      Polycom TC8 (codename proline_exec) — chainloaded stage-2
    board/                  lpddr4_timing.c (DDR shared with C60)
    uboot-overlay/          files layered over vendored uboot-imx
                            (defconfig, DTS, board hooks, fb_fsl boota fixes)
patches/                    patches applied over upstream nxp-imx/uboot-imx
scripts/
  build.sh                  ./scripts/build.sh <target>
  flash-via-uuu.sh          load to a board in SDP mode
vendored/                   nxp-imx/uboot-imx, arm-trusted-firmware,
                            imx-mkimage, firmware-imx blobs (gitignored)
out/                        per-target build artifacts (gitignored)
```

## Build

```sh
./scripts/build.sh c60-kepler_proto1
./scripts/build.sh tc8-chainload-uboot

./scripts/flash-via-uuu.sh out/c60-kepler_proto1/imxboot-c60.bin
```

The TC8 stage-2 artifact is `vendored/uboot-imx/u-boot.bin` (no-SPL u-boot
proper); it is installed into eMMC boot1 by the WebUSB provisioner, not flashed
via SDP.

## Status

- **C60**: clean board port — eMMC works, kernel handoff reaches Linux;
  `uuu` dual-boot (custom kernel+Debian on slot A / stock on slot B) works.
- **TC8**: chainloaded stage-2 **delivered and proven on hardware** —
  auto-persisting chain (stock → stage-2), bootsel logo + GT9271 gesture,
  F2 panel + F3 networking up, and the unlocked-`boota` path boots
  Android-format slot images. Provisioning is a WebUSB/WebSerial tool.

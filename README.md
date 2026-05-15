# polycom-uboot

Mainline u-boot for Polycom i.MX 8M Mini devices — currently:

| Build target              | Device                   | Codename          | DRAM         |
|---------------------------|--------------------------|-------------------|--------------|
| `c60-kepler_proto1`       | Trio C60                 | kepler_proto1     | 2 GiB LPDDR4 |
| `tc8-proline_exec`        | TC8 conference tablet    | proline_exec      | 2 GiB LPDDR4 |

Both use the same i.MX 8M Mini Quad SoC and (per 2026-05-14 live testing)
share DDR training configs — but each has its own board file, DT, and any
board-specific quirks.

## Goal

Replace the stock Polycom u-boot for two complementary purposes:

1. **SDP rescue path**: with chassis BOOT_MODE switches set to USB-download
   (BMOD=00), `uuu` on the host loads this u-boot into RAM. We control
   `bootcmd` so the chain becomes hands-off: power-on → SDP enum →
   uuu loads → u-boot autoboots Linux from slot A or B.

2. **Removable Polycom override**: stock u-boot enters fastboot mode whenever
   BootROM detects USB-boot (`is_usb_boot` check at VA 0x40204268 per the
   stock RE). Our build skips that override, so the loop SDP→u-boot→fastboot
   never traps.

See `Polycom-Open-Firmware/c60-firmware-build` for the kernel/rootfs side
and the user's local `c60_uuu_sdp_workflow` / `c60_bootmode_switches` memory
notes.

## Layout

```
targets/
  c60-kepler_proto1/        Polycom Trio C60
    board/                  board.c, spl.c, lpddr4_timing.c
    dts/                    u-boot-side DTS
    ddr/                    extracted DDR config (or copy from upstream)
    target.env              build vars
  tc8-proline_exec/         Polycom TC8
    (same layout)
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
./scripts/build.sh tc8-proline_exec

./scripts/flash-via-uuu.sh out/c60-kepler_proto1/imxboot-c60.bin
```

## Status

Bootstrap scaffold. DDR extraction in progress; build infra and per-target
board files TBD.

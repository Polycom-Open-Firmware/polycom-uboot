# polycom-uboot

Mainline u-boot for the Polycom Trio C60 (i.MX 8M Mini Quad, codename
`kepler_proto1`).

Goal: build a u-boot that's loaded via NXP SDP (`uuu`) when the chassis
BOOT_MODE switches are set to USB-download, then auto-boots the eMMC slot A
stock-or-mainline Linux. The Polycom-built stock u-boot enters fastboot
mode whenever BootROM=USB; this fork removes that quirk so the SDP path is
hands-off.

See `Polycom-Open-Firmware/c60-firmware-build` for the kernel+rootfs side and
`c60_uuu_sdp_workflow` memory file for the runtime details.

## Status

Bootstrap — DDR config not yet extracted. Using TC8/EVK config as starting
point. Builds but untested on hardware.

## Layout

| dir | content |
|-----|---------|
| `board/kepler_proto1/` | Polycom board file (will hold DDR tables) |
| `dts/` | u-boot-side device tree |
| `patches/` | patches over upstream `nxp-imx/uboot-imx` |
| `scripts/` | build + pack scripts |
| `ddr/` | DDR config — extracted from stock binary or copied from EVK |
| `vendored/` | NXP firmware blobs (`firmware-imx-*.bin`), ATF, mkimage |
| `out/` | build artifacts (gitignored) |

## Build

```sh
./scripts/build.sh                    # configures + compiles + packs imx-boot
./scripts/flash-via-uuu.sh            # loads via SDP to a C60 in BOOT_MODE=00
```

## Why a fresh repo

`c60-firmware-build` is the kernel/rootfs/boot.img pipeline.
`polycom-uboot` is the bootloader pipeline. Different toolchains, different
upstream, different scaling — kept separate.

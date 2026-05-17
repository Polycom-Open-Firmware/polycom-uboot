#!/usr/bin/env bash
# scripts/build.sh — v0.2: drop in C60 DDR config + C60 DTB into EVK skeleton
#
# What works after this build (verified 2026-05-14 on C60 hw):
#   ✓ DDR training (LPDDR4, our extracted table)
#   ✓ BL31 ATF
#   ✓ U-boot CLI on UART2
#
# What this build adds (target — DT swap should fix):
#   - eMMC (USDHC3) — stock-DTB pinmux
#   - fastboot gadget — needs C60 DT
#
# Output: out/<target>/flash.bin

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-c60-kepler_proto1}"
TARGET_DIR="$REPO/targets/$TARGET"
[ -d "$TARGET_DIR" ] || { echo "unknown target: $TARGET"; exit 1; }
source "$TARGET_DIR/target.env"
OUT="$REPO/out/$TARGET"; mkdir -p "$OUT"

UB="$REPO/vendored/uboot-imx"
ATF="$REPO/vendored/arm-trusted-firmware"
MK="$REPO/vendored/imx-mkimage"
FW="$REPO/vendored/firmware-imx"
FW_DIR=$(ls -d $FW/firmware-imx-*/firmware 2>/dev/null | head -1)
[ -d "$FW_DIR" ] || { echo "missing firmware-imx blobs — run scripts/setup.sh"; exit 1; }

export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

echo "[1/5] inject C60 DDR table"
EVK="$UB/board/freescale/imx8mm_evk"
[ -f "$EVK/lpddr4_timing.c.orig" ] || cp "$EVK/lpddr4_timing.c" "$EVK/lpddr4_timing.c.orig"
cp "$TARGET_DIR/board/lpddr4_timing.c" "$EVK/lpddr4_timing.c"

echo "[2/5] locate optional stock-reference DTB (md5 sanity only)"
DTB_SRC="$(ls "$TARGET_DIR"/dts/*.dtb 2>/dev/null | head -1 || true)"
[ -n "$DTB_SRC" ] && echo "      ref: $DTB_SRC" || echo "      none — dtb md5 sanity will be skipped"

echo "[3.0/5] apply C60 u-boot overlay (tracked patches; vendored/ is gitignored)"
OVERLAY="$TARGET_DIR/uboot-overlay"
if [ -d "$OVERLAY" ]; then
	# Copy tracked source patches over the freshly-cloned vendored tree so
	# the build is reproducible from a clean scripts/setup.sh. Covers:
	# defconfig (Android/AVB/BCB/fastboot/MMC_DEV), C60 DTS (usbg1 DM gadget
	# node + dr_mode=peripheral), A53 clock fix, BUCK2/REGLOCK, TCPC bypass,
	# fastboot_dev/target_ubootdev env.
	( cd "$OVERLAY" && find . -type f -print0 | \
	  while IFS= read -r -d '' f; do
	    mkdir -p "$UB/$(dirname "$f")"
	    cp "$f" "$UB/$f"
	  done )
	echo "      overlay applied ($(find "$OVERLAY" -type f | wc -l) files)"
fi

echo "[3/5] build u-boot ($DEFCONFIG)"
make -C "$UB" -s mrproper
make -C "$UB" -s "$DEFCONFIG"
make -C "$UB" -s -j$(nproc) >/dev/null
if [ -n "$DTB_SRC" ] && [ -f "$DTB_SRC" ]; then
	echo "[3.5/5] dtb md5 sanity ($(basename "$DTB_SRC") vs built imx8mm-evk.dtb)"
	md5sum "$DTB_SRC" "$UB/arch/arm/dts/imx8mm-evk.dtb" || true
else
	echo "[3.5/5] no stock-ref dtb in target — md5 sanity skipped"
fi

if [ -n "${BL31_OVERRIDE:-}" ] && [ -f "$TARGET_DIR/$BL31_OVERRIDE" ]; then
	echo "[4/5] using stock BL31 override: $BL31_OVERRIDE"
	cp "$TARGET_DIR/$BL31_OVERRIDE" "$OUT/bl31.bin"
else
	echo "[4/5] build ATF bl31.bin for imx8mm"
	make -C "$ATF" -s PLAT=imx8mm bl31 -j$(nproc) >/dev/null
	cp "$ATF/build/imx8mm/release/bl31.bin" "$OUT/bl31.bin"
fi

echo "[5/5] pack via imx-mkimage → flash.bin"
MKD="$MK/iMX8M"
cp "$UB/spl/u-boot-spl.bin" "$MKD/"
cp "$UB/u-boot-nodtb.bin"    "$MKD/"
cp "$UB/arch/arm/dts/${DTS}.dtb" "$MKD/imx8mm-evk.dtb"
cp "$UB/u-boot.bin"          "$MKD/"
cp "$UB/tools/mkimage"       "$MKD/mkimage_uboot"
cp "$OUT/bl31.bin" "$MKD/bl31.bin"
cp "$FW_DIR/ddr/synopsys/lpddr4_pmu_train_1d_imem.bin" "$MKD/"
cp "$FW_DIR/ddr/synopsys/lpddr4_pmu_train_1d_dmem.bin" "$MKD/"
cp "$FW_DIR/ddr/synopsys/lpddr4_pmu_train_2d_imem.bin" "$MKD/"
cp "$FW_DIR/ddr/synopsys/lpddr4_pmu_train_2d_dmem.bin" "$MKD/"

make -C "$MK" -s SOC=iMX8MM flash_evk >/dev/null
cp "$MKD/flash.bin" "$OUT/flash.bin"
ls -la "$OUT/flash.bin"
echo "[OK] flash.bin built — load via: uuu -b spl $OUT/flash.bin"

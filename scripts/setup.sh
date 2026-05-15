#!/usr/bin/env bash
# scripts/setup.sh — clone upstream u-boot + ATF + mkimage + fetch NXP DDR firmware.
#
# Run once per fresh checkout. Idempotent.

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENDORED="$REPO_ROOT/vendored"
mkdir -p "$VENDORED"

# Pinned NXP tags (kept in sync via this script; bump together when refreshing).
NXP_UBOOT_TAG="${NXP_UBOOT_TAG:-lf_v2024.04}"
NXP_MKIMAGE_TAG="${NXP_MKIMAGE_TAG:-lf-6.6.36-2.1.0}"
NXP_ATF_TAG="${NXP_ATF_TAG:-lf_v2.10}"
NXP_FW_VER="${NXP_FW_VER:-8.23}"
NXP_FW_BIN="firmware-imx-${NXP_FW_VER}.bin"
NXP_FW_URL="https://www.nxp.com/lgfiles/NMG/MAD/YOCTO/${NXP_FW_BIN}"

clone_or_fetch() {
  local dir="$1" url="$2" tag="$3"
  if [[ -d "$VENDORED/$dir/.git" ]]; then
    echo "[=] $dir present"
  else
    echo "[+] cloning $dir @ $tag"
    git clone --depth 1 -b "$tag" "$url" "$VENDORED/$dir"
  fi
}

clone_or_fetch uboot-imx https://github.com/nxp-imx/uboot-imx.git "$NXP_UBOOT_TAG"
clone_or_fetch imx-mkimage https://github.com/nxp-imx/imx-mkimage.git "$NXP_MKIMAGE_TAG"
clone_or_fetch arm-trusted-firmware https://github.com/nxp-imx/imx-atf.git "$NXP_ATF_TAG"

# NXP firmware-imx (DDR PMU + HDMI fw blobs). Self-extracting EULA-tagged .bin.
FW_DIR="$VENDORED/firmware-imx"
if [[ ! -f "$FW_DIR/firmware/ddr/synopsys/lpddr4_pmu_train_1d_imem.bin" ]]; then
  echo "[+] fetching $NXP_FW_BIN"
  mkdir -p "$FW_DIR"
  cd "$FW_DIR"
  if [[ ! -f "$NXP_FW_BIN" ]]; then
    curl -fLO "$NXP_FW_URL"
  fi
  chmod +x "$NXP_FW_BIN"
  echo "[+] extracting (auto-accepting NXP license)"
  ./"$NXP_FW_BIN" --auto-accept --force >/dev/null
  cd "$REPO_ROOT"
fi

# Sanity-check the bits the SPL build will look for.
need=(
  "$FW_DIR"/firmware-imx-*/firmware/ddr/synopsys/lpddr4_pmu_train_1d_imem.bin
  "$FW_DIR"/firmware-imx-*/firmware/ddr/synopsys/lpddr4_pmu_train_1d_dmem.bin
  "$FW_DIR"/firmware-imx-*/firmware/ddr/synopsys/lpddr4_pmu_train_2d_imem.bin
  "$FW_DIR"/firmware-imx-*/firmware/ddr/synopsys/lpddr4_pmu_train_2d_dmem.bin
)
for f in "${need[@]}"; do
  ls $f 2>/dev/null || { echo "[!] missing: $f"; exit 1; }
done

echo
echo "[OK] vendored sources ready:"
ls -d "$VENDORED"/*/ | sed 's|^| - |'

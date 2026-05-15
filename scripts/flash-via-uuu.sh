#!/usr/bin/env bash
# Run a built flash.bin on a C60 in BMOD=00 (SDP) mode. Watches for 1fc9:0134.
set -euo pipefail
BIN="${1:-out/c60-kepler_proto1/flash.bin}"
[ -f "$BIN" ] || { echo "no such file: $BIN"; exit 1; }
echo "[+] waiting for SDP device (1fc9:0134)..."
until lsusb 2>/dev/null | grep -q "1fc9:0134"; do sleep 1; done
echo "[+] loading via uuu"
uuu -b spl "$BIN"
echo "[OK] u-boot loaded — check UART"

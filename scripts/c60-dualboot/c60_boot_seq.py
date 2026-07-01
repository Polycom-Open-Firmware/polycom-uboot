#!/usr/bin/env python3
# Interrupt the u-boot autoboot over UART and run the slot boot sequence.
# The UART device is configurable via the C60_TTY environment variable.
import os, sys, time
slot = sys.argv[1] if len(sys.argv) > 1 else "a"
dev = os.environ.get("C60_TTY", "/dev/ttyUSB0")
if slot == "b":
    cmds = [
      "setenv bootargs console=ttymxc1,115200 earlycon=ec_imx6q,0x30890000,115200 init=/init androidboot.console=ttymxc1 androidboot.hardware=Poly cma=320M@0x400M-0xb80M androidboot.selinux=permissive androidboot.slot_suffix=_b androidboot.veritymode=disabled root=/dev/mmcblk2p6 skip_initramfs rootwait",
      "mmc dev 0",
      "mmc read 0x40400000 0x20000 0x18000",
      "mmc read 0x43400000 0x6000 0x2000",
      "cp.b 0x40400800 0x40480000 0x233b200",
      "cp.b 0x4273c000 0x50000000 0x622cc1",
      "booti 0x40480000 0x50000000:0x622cc1 0x43400040",
    ]
else:
    cmds = [
      "setenv bootargs console=ttymxc1,115200 earlycon=ec_imx6q,0x30890000,115200 clk_ignore_unused root=/dev/mmcblk2p5 rootwait rw init=/sbin/init",
      "mmc dev 0",
      "part start mmc 0 boot_a ba",
      "part size mmc 0 boot_a bn",
      "mmc read 0x50000000 ${ba} ${bn}",
      # Android boot.img v0 header (LE u32 in RAM at 0x50000000):
      #   kernel_size @+8, ramdisk_size @+16, second_size @+24, page_size @+36
      # dtb_off = page + roundup(kernel_size,page) + roundup(ramdisk_size,page);
      # so the DTB source tracks the kernel size instead of a fixed offset.
      "setexpr.l hks *0x50000008",
      "setexpr.l hrs *0x50000010",
      "setexpr.l hss *0x50000018",
      "setexpr.l hps *0x50000024",
      "setexpr pm1 ${hps} - 1",
      "setexpr kpd ${hks} + ${pm1}", "setexpr kpd ${kpd} / ${hps}", "setexpr kpd ${kpd} * ${hps}",
      "setexpr rpd ${hrs} + ${pm1}", "setexpr rpd ${rpd} / ${hps}", "setexpr rpd ${rpd} * ${hps}",
      "setexpr dof ${hps} + ${kpd}", "setexpr dof ${dof} + ${rpd}",
      "setexpr ksr 0x50000000 + ${hps}",
      "setexpr dsr 0x50000000 + ${dof}",
      "cp.b ${ksr} 0x40080000 ${hks}",
      "cp.b ${dsr} 0x46000000 ${hss}",
      "booti 0x40080000 - 0x46000000",
    ]
fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    # hammer CR for ~14s to catch the autoboot interrupt window
    t = time.time()
    while time.time() - t < 14:
        os.write(fd, b"\r"); time.sleep(0.1)
    time.sleep(0.5)
    for c in cmds:
        os.write(fd, c.encode() + b"\r")
        time.sleep(2.0 if c.startswith("mmc read") else 0.4)
finally:
    os.close(fd)
print("seq sent (slot %s)" % slot)

#!/usr/bin/env python3
# Interrupt our u-boot autoboot over UART and run the slot boot sequence.
import os, sys, time
slot = sys.argv[1] if len(sys.argv) > 1 else "a"
dev = "/dev/ttyUSB0"
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
      "mmc read 0x44000000 0x8000 0x18000",
      "cp.b 0x44000800 0x40080000 0x019f4a00",
      "cp.b 0x459f5800 0x46000000 0xb7a1",
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

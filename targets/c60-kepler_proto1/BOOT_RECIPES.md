# C60 (kepler_proto1) — boot recipes for polycom-uboot

These u-boot CLI sequences boot the C60 once `flash.bin` (this repo's mainline u-boot)
is live in DRAM via uuu, with the board in SDP mode.

## GPT layout

```
  1  0x00004000  0x00005fff  dtbo_a       (4 MiB)
  2  0x00006000  0x00007fff  dtbo_b       (4 MiB)
  3  0x00008000  0x0001ffff  boot_a       (48 MiB)
  4  0x00020000  0x00037fff  boot_b       (48 MiB)
  5  0x00038000  0x003b7fff  system_a     (1.75 GiB)
  6  0x003b8000  0x00737fff  system_b     (1.75 GiB)
  7  0x00738000  0x00739fff  misc         (1 MiB)
 16  0x00dfd000  0x00dfd7ff  vbmeta_a
 17  0x00dfe000  0x00dfe7ff  vbmeta_b
```

## Recipe 1 — boot stock Polycom Android (slot B)

Stock boot.img is Android v1: kernel + ramdisk only, no embedded DTB. The DTB
lives in `dtbo_b`, wrapped in an Android DTBO container; the actual FDT starts
at offset 0x40 inside the dtbo file.

```sh
# Read boot_b into low RAM (boot.img: hdr/kernel/ramdisk)
mmc dev 0
mmc read 0x40400000 0x20000 0x18000

# Read dtbo_b at high enough addr that the DTB extraction won't collide
mmc read 0x43400000 0x6000 0x2000

# Move kernel and ramdisk to their final addresses
# kernel:   boot.img page-aligned offset 0x800, size 0x0233b200
# ramdisk:  boot.img offset (0x800 + page_round(kernel_size)) = 0x233c000
cp.b 0x40400800 0x40480000 0x233b200
cp.b 0x4273c000 0x50000000 0x622cc1

# Bootargs — stock kernel needs explicit console=/earlycon=, AOSP needs
# verifiedbootstate/veritymode (orange + disabled = unlocked-AVB pass-through).
# androidboot.slot_suffix=_b selects stock slot.
setenv bootargs 'console=ttymxc1,115200 earlycon=ec_imx6q,0x30890000,115200 init=/init androidboot.console=ttymxc1 androidboot.hardware=Poly cma=320M@0x400M-0xb80M androidboot.selinux=permissive androidboot.slot_suffix=_b androidboot.verifiedbootstate=orange androidboot.veritymode=disabled androidboot.vbmeta.device_state=unlocked'

# DTB inside dtbo container starts at +0x40
booti 0x40480000 0x50000000:0x622cc1 0x43400040
```

Expected behaviour: kernel printk reaches mmc2 enumeration, GPT visible
(all 17 partitions), LP5569 LEDs, TAS5751M codec, kepler_cap touch, then
goes quiet. AOSP's `service console` does not start when
`ro.debuggable=0`, so the silence after bring-up is the stock
service-console gate, not a boot failure.

## Recipe 2 — boot mainline kernel (slot A)

Slot A holds an Android boot.img v0 produced by `c60-firmware-build`
(`pack_boota_set.sh`): kernel in primary slot, **DTB in `second`**
(ramdisk_size=0). Layout offsets listed are for the current build;
recompute if kernel_size changes.

```sh
# Read boot_a high — don't clobber where booti will relocate the kernel
mmc dev 0
mmc read 0x44000000 0x8000 0x18000

# Header at 0x44000000:
#   kernel_size  = 0x019f4a00  @ +0x800
#   second_size  = 0x0000b79d  @ +0x019f5800 (page_round(kernel_size)+0x800)
#   kernel_addr  = 0x40080000  (DRAM_base + text_offset)

# Move kernel directly to text_offset position (booti would relocate
# from 0x44000800 → 0x40080000 anyway; doing the move ourselves avoids
# the relocation clobbering the DTB).
cp.b 0x44000800 0x40080000 0x019f4a00

# DTB to high RAM (keep clear of kernel image_size from 0x40080000).
cp.b 0x459f5800 0x46000000 0xb79d

# Mainline kernel + Debian rootfs cmdline (root on system_a):
setenv bootargs 'console=tty0 console=ttymxc1,115200 earlycon=ec_imx6q,0x30890000,115200 keep_bootcon panic=10 rw rootwait fw_devlink=permissive root=/dev/disk/by-partlabel/system_a'

booti 0x40080000 - 0x46000000
```

The kernel parses the DTB ("Hardware name: Polycom Trio C60 (Kepler
proto1) (DT)" appears in early printk); etnaviv/mxsfb/samsung-dsim probe
and PCIe init reaches iATU unroll. Bring-up beyond that point (audio
card, WiFi/PCIe) is tracked separately.

## Memory map for both recipes

```
 0x40000000  DRAM base (i.MX 8M Mini)
 0x40080000  arm64 Linux text_offset position (kernel runs here)
 0x40200000  u-boot itself (loaded by uuu SDPV)
 0x40400000  staging for stock boot.img (Recipe 1 only)
 0x43400000  FDT/DTB landing zone (both recipes)
 0x44000000  staging for mainline boot.img (Recipe 2)
 0x46000000  DTB landing for Recipe 2 (avoid kernel relocation range)
 0x50000000  ramdisk landing for Recipe 1
 0xbfe00000  u-boot's relocated self (top of DRAM)
```

## Why `bootargs` matters

Booting with an empty `bootargs` gives the kernel no
`console=`/`earlycon=`, so the early boot chain faults before any output
is printed. An explicit `console=ttymxc1,115200 earlycon=...` is required
for either kernel to reach its banner. Stock Polycom u-boot prepends
these via `CONFIG_CMDLINE`; mainline u-boot does not, so they are set
explicitly in the recipes above.

## TODO

- Encode the slot sequences as `bootcmd` env macros with a `boot_slot`
  selector so the board autoboots without UART intervention.
- Fold live flashing (fastboot over the u-boot USB gadget) into an
  env-driven path.

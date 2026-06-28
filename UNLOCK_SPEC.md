# polycom-uboot — Fully-Unlocked Firmware Spec

Status: **DRAFT / living doc** · Owner: alex · Started 2026-05-16
System of record for the "fully unlocked" u-boot. Decisions here are
binding until changed *here*. Linked from repo `CLAUDE.md` doc tree.

> ## ⛔ REALITY CHECK 2026-05-16 — TC8 premise INVALIDATED
> The bench TC8's BootROM **HAB secure boot is ENABLED (closed/fused)** —
> proven on the TC8's own u-boot console: `hab_status` → *"Secure boot
> enabled"*, HAB cfg `0xcc` / state `0x99`, SRK_HASH fuses burned.
> Therefore a **custom/unsigned SPL+u-boot cannot be flashed-and-booted**
> on this TC8 (BootROM rejects non-Polycom-signed stage-1; we have no
> key). The §1 "strip-AVB, flash our own u-boot via SDP/eMMC" premise is
> **not achievable on a HAB-closed TC8.** Today's SDP "downloads-Okay-
> but-never-executes" = HAB rejecting unsigned (real, not the dead-UART
> artifact it was first mis-read as). **Viable unlock = ENV-VECTOR only**
> (keep the Polycom-signed stock `U-Boot 2018.03-g01365dcbff`, own its
> rewritable env: `bootcmd=run slotbboot` → raw `mmc read`+`booti` of an
> unsigned kernel; optionally chainload a 2nd-stage custom u-boot from
> eMMC — HAB gates stage-1 only). This is exactly `tc8-firmware-build/
> FLASHING.md` and what the bench TC8 runs now. Everything below (F1/F2/
> F3, the custom-u-boot build) only applies where HAB is **open** (e.g.
> the C60) — keep for that target; for TC8, pivot to the env-vector.
> Stock TC8 u-boot is **interruptible** (`bootdelay=3`, "Hit any key…")
> — the old zero-delay assumption was C60-scoped.
>
> ### ✅ PATH FORWARD (TC8) — CHAINLOAD STAGE-2 — **PROVEN END-TO-END 2026-05-16**
> **DONE/validated on hardware:** stock signed `U-Boot 2018.03` →
> `mmc dev 1; mmc read 0x40200000 0x20000 0x800; go 0x40200000` →
> our **unsigned `U-Boot 2024.04` + TC8 DTS booted to its own
> `u-boot=>` prompt** on the HAB-closed TC8. Stage-2 = `u-boot.bin`
> (no-SPL, 994 KB) on eMMC `kernel_bak`/p2 (LBA `0x20000`), dd'd from
> TC8 Linux. First iter sync-aborted on F2 display init (DISPMIX
> `0x32e28004`); **F2 stripped → clean boot to prompt**. Zero brick
> risk confirmed (failed iters auto-recover stock→Linux). Remaining:
> hardcode `PHYS_SDRAM_SIZE` (no-SPL DDR-detect cosmetic "16 EiB"),
> ethernet for F3, test F1 gesture, F2 deferred, persist via env
> (`bootcmd`→auto `mmc read`+`go`). The build/F1/F3 (§1–§7) stand;
> delivery = stage-2 chainload.
>
> ### Original analysis (now proven above)
> HAB gates **stage-1 only**. The signed stock `U-Boot 2018.03` does
> *not* re-verify what it loads (proven: `slotbboot` raw `mmc read` +
> `booti` of an **unsigned** kernel). On-device `help` confirmed the
> stock u-boot exposes **`go`, `bootm`, `mmc read`, `tftpboot`,
> `ext4load`, `fatload`, `booti`** — every chainload primitive. So the
> **entire F1/F2/F3 unlocked-u-boot feature set is achievable on the
> HAB-closed TC8 as a chainloaded STAGE-2 custom u-boot**, loaded by the
> env-vector — no Polycom key, no HAB exploit, no fuse change, **zero
> brick risk** (signed stage-1 untouched; worst case = fix env).
> Build model: u-boot-proper **without SPL/DDR** (stock SPL already did
> DRAM/clocks/PMIC); load to a non-colliding DRAM addr (low, ~`0x40000000`
> region; stock u-boot self-relocates high); env: `mmc read`/`tftp` →
> `go`/`bootm`. Bench-iterate over the now-working slug-B serial console
> (`tx16/rx17`). **Net: UNLOCK_SPEC is REVIVED for TC8 via chainload,
> not by replacing stage-1.** §1–§7 features stand; only the *delivery*
> changes (stage-2 chainload instead of SDP/eMMC stage-1 replace).
>
> ### 🔬 2026-05-16 (later) — chainload hardened; DRAM solved; F2 isolated
> Re-validated end-to-end after compaction. Confirmed facts + fixes:
> - **[P1] Custom unsigned `U-Boot 2024.04` chainloads to its own
>   interactive `u-boot=>` prompt** on the HAB-closed TC8 with
>   `DRAM: 2 GiB`, `Core: 177 devices`, **zero Synchronous Abort**, and
>   our `vtest` command registered. The whole "can we run custom u-boot"
>   question = **YES**.
> - **[P1] Chainload prerequisite — the dcache-off MMU fix:** stock `go`
>   does NOT tear down its MMU/caches, so stage-2 runs under stock page
>   tables (no display map) → `esr 0x96000006` translation fault. Issue
>   **`dcache flush; icache off; dcache off` before `go`**. With it: no
>   abort. (Kept in the chainload sequence.)
> - **[P1] Stage-2 artifact = `vendored/uboot-imx/u-boot.bin`** (no-SPL
>   u-boot proper, entry@0, self-relocating). **NOT `out/.../flash.bin`**
>   (SPL/imx container; byte0 is the boot header → `go` faults
>   `esr 0x02000000`). dd `u-boot.bin` → mmcblk2 LBA `0x20000`.
> - **[P1] The "16 EiB" DRAM was NOT cosmetic — it was THE post-DRAM
>   hang.** Root cause: no-SPL chainload → `save_boot_params()` captures
>   garbage boot-arg regs → `rom_pointer[1]` junk-nonzero → NXP imx8m
>   `soc.c` `dram_init()`/`dram_init_banksize()` do
>   `gd->ram_size = PHYS_SDRAM_SIZE - rom_pointer[1]` → underflow to
>   16 EiB → relocation/bank wedge → silent hang right after `DRAM:`.
>   **Fix (KEPT):** board `board_phys_sdram_size()` zeros
>   `rom_pointer[0..1]` and sets `*size = PHYS_SDRAM_SIZE` — it is called
>   at the TOP of both functions, before their `rom_pointer[1]` checks,
>   so it fixes both. (Supersedes the "hardcode PHYS_SDRAM_SIZE cosmetic"
>   note above.)
> - **[P1] ✅ F2 PANEL RESOLVED END-TO-END (user eyes-on, 2026-05-16):**
>   the chainloaded custom U-Boot 2024.04 drives the lcc-proto panel —
>   `vtest` RGB/colorbars/checkerboard **visible & looping on glass**,
>   backlight on, 800×1280, zero abort. The full KEPT fix chain (each a
>   prior dead-end if missing), all deterministic / NXP-ref-mirrored:
>   1. **`&lcdif` `display=<&display0>`** + `bits-per-pixel`/`bus-width`
>      + `display-timings` (RE'd lcc 800×1280/76.5564 MHz) — else
>      `mxsfb` "required display property isn't provided".
>   2. **Panel = TOP-LEVEL node** (not a `&mipi_dsi` child w/ `reg`) so
>      u-boot DM binds it and `video_link` (`find_device_by_ofnode`)
>      resolves it — else "failed to get any video link display timings".
>   3. **dsi↔panel of-graph** endpoint pair (`&mipi_dsi port@1` ↔ panel
>      `port`), base wires `port@0`←lcdif.
>   4. **`&mipi_dsi` keeps base `fsl,imx8mm-mipi-dsim`** (binds the
>      UCLASS_VIDEO_BRIDGE `imx_sec_dsim` that `mxsfb` drives) + a
>      **separate top-level `dsi-host { compatible="samsung,sec-mipi-dsi" }`**
>      DSI_HOST back-end. Overriding `&mipi_dsi` to the DSI_HOST
>      compatible = dark (mxsfb doesn't handle UCLASS_DSI_HOST).
>   5. **rm67191 driver: enable the EXTERNAL PWM4 backlight.** It only
>      wrote internal DCS brightness; added `backlight` phandle resolve
>      (`UCLASS_PANEL_BACKLIGHT`) + `backlight_enable()` in
>      `enable_backlight` (mirrors u-boot `simple_panel.c`). THIS was
>      the "clean path but totally dark" final piece — matches the
>      documented C60 rm67191/PWM4 root cause.
>   All in `targets/tc8-chainload-uboot/uboot-overlay/` (DTS +
>   raydium-rm67191.c). Net: **F1/F2/F3-capable fully-custom u-boot is
>   delivered on the HAB-closed TC8 as a chainloaded stage-2.**
> - **[P1] ⚠ SAFETY — TC8/C60 share a cloned identity.** The C60
>   (`Polycom Trio C60 (Kepler proto1)`, IP often `.47`) carries the
>   SAME MAC `d6:8e:5d:39:e4:5e` AND hostname `tc8-kiosk` as the TC8
>   (cloned rootfs). **Neither MAC nor hostname distinguishes them.**
>   The ONLY authoritative discriminator is the **DT model string**
>   (`/sys/firmware/devicetree/base/model`: TC8 = `… LCC PROTO`, C60 =
>   `… Kepler …`). Any reflash MUST hard-gate on the DT model before dd.
>   slug-1 serial (`ws://192.168.10.123`) is physically the TC8 (C60 =
>   slug-2 `.95`) → use it to learn/confirm the TC8's IP.
> - **Proven reflash+test recipe (C60-safe):** PoE cold-boot port 1 →
>   poll SSH `.42`, **hard DT-model guard** → `dd u-boot.bin
>   of=/dev/mmcblk2 bs=512 seek=131072 conv=fsync` (kernel_bak/p2
>   scratch, non-load-bearing) → catch stock over slug-1 →
>   `mmc dev 1; mmc read 0x40200000 0x20000 0x830; dcache flush;
>   icache off; dcache off; go 0x40200000`. Helpers:
>   `/tmp/tc8_full.sh`, `/tmp/wsce2.py` (drafts).
>
> ### ✅ 2026-05-17 — SHIPPABLE: autonomous chain → Debian PROVEN end-to-end
> The auto-persisting chainloaded stage-2 now boots **Debian 12 with
> ZERO interaction**, deterministically proven (drop-immune steady-state
> `tc8-kiosk login:` getty on ttymxc1 after an unattended cold boot).
> Proven chain: **stock 2018.03 → chainload → stage-2 2024.04 → bootsel
> logo (20 s window, no gesture) → eMMC default → dhcp66 skipped → our
> `mmcboot` (raw-read eMMC) → `booti` → Linux 6.6 → samsung-dsim OK →
> Debian 12 bookworm → login getty**. Three independent post-bring-up
> bugs were root-caused (deterministic, diag1–7 single-WS captures) and
> fixed — all KEPT in `targets/tc8-chainload-uboot/uboot-overlay/`:
> - **[P1] WDT starvation in bootsel.** The F1 gesture poll-loop +
>   2 s settle-loop never serviced the i.MX WDOG (stock/ATF armed it)
>   → SoC reset mid-window → infinite re-chainload (looked like the
>   logo "hanging"). Fix: `#include <cyclic.h>` + `schedule();` as the
>   first statement of BOTH the `BOOTSEL_WIN_MS` poll loop and the 2 s
>   max-count settle loop in `imx8mm_evk.c`. (Earlier manual-gesture
>   tests "passed" only because a touch broke the loop before WDT.)
> - **[P1] `booti` silent hang = `video_link_shut_down()` DSI
>   teardown deadlock.** `announce_and_cleanup()` (arch/arm/lib/
>   bootm.c) calls `video_link_shut_down()` *before* the "Starting
>   kernel" print → `device_remove()` cascades `imx_sec_dsim_remove()`
>   (panel remove + `dsi_host_disable()`) + `lcdifv3` disable on the
>   **live F2 DSI/LCDIF/lcc-panel** pipeline → sec-dsim host-disable
>   deadlocks (the C60 `samsung_dsim` teardown precedent). Symptom:
>   stops byte-identically one line before `Starting kernel`. Fix:
>   overlay `arch/arm/lib/bootm.c` — comment out the
>   `video_link_shut_down();` call (keep `CONFIG_VIDEO_LINK` for the
>   F2 splash + bootsel BMP; Linux re-inits the display IP itself, so
>   leaving the pipeline up across the jump is benign).
> - **[P1] Autonomous gap = `mmcboot` env collision with stock.**
>   `board_late_init` installed our custom `mmcboot` under
>   `if (!env_get("mmcboot"))`, but stock imx8mm_evk's built-in
>   default env **already defines** a distro `mmcboot` (`run mmcargs;
>   run loadfdt; booti … ${fdt_addr_r}`) that targets the EVK's
>   removable **`mmc 2`** — absent on TC8 (eMMC = `mmc dev 0` in
>   stage-2). So the guard skipped our override and autonomous
>   `run bootcmd` ran *stock* mmcboot → `** Bad device specification
>   mmc 2 **` → `WARN: Cannot load the DT` → drop to `u-boot=>`
>   (never booted). Root-caused via `diag7` `printenv`. Fix: in
>   `imx8mm_evk.c`, force-set `mmcboot` **unconditionally** (drop the
>   `!env_get` guard). `gesture_sel`/`dhcp66_boot`/`tc8_bootargs` do
>   NOT collide (stock doesn't define them) → they keep their guards.
> - **Falsified & reverted:** `CONFIG_OF_SYSTEM_SETUP` (suspected
>   `ft_system_setup` fixup hang) — disabling it produced a
>   byte-identical hang (diag4), so it was NOT the cause; reverted to
>   baseline `=y` to keep the experiment single-variable.
>
> Final shippable stage-2 = `vendored/uboot-imx/u-boot.bin` from
> `./scripts/build.sh tc8-chainload-uboot` (md5 `aa4645a3` at proof
> time), re-staged to eMMC LBA `0x4000`. Net: **TC8 #14 (auto-persist
> chainload + end-to-end OS boot) is DELIVERED and PROVEN.** Open
> follow-ups (separate, pre-existing): kernel-side ethernet
> (RTL8363NB-VB FEC-MDIO blocker — why net-probe can't see the unit),
> prod gesture window 20 s→~4 s, F3 netboot, user eyes-on F2-splash-
> survives-jump check.
>
> ### ✅ 2026-05-17 (later) — F3 networking SOLVED in stage-2 u-boot
> **u-boot networking works** (real DHCP lease + ICMP round-trip) with
> a SINGLE DTS+config change — no RTL8363 driver, no jam-table port.
> Key architectural insight (durable): **stock signed u-boot 2018.03
> is our de-facto switch BSP.** TC8 wires the i.MX8MM FEC (ENET1,
> RGMII) to an **RTL8363NB-VB** L2 switch (chip-id `0x1cc943`, MDIO
> `0x1d`, reset GPIO5_IO02 AL). The switch is *stateful external
> silicon*: stock u-boot fully initialises it (jam-table + 8051 fw +
> transparent-forwarding — `Switch: RTL8363NB-VB(0x1cc943)`) and then
> chainloads us via `go`, which does **not** reset that chip. So our
> chainloaded stage-2 **inherits a fully-configured transparent
> switch** and needs only: (1) `CONFIG_PHY_FIXED=y`; (2) `&fec1`
> `status="okay"` + `pinctrl_fec1` (standard i.MX8MM RGMII pad group,
> EVK-template) + `phy-mode="rgmii-id"` + a `fixed-link` 1G/full
> subnode; **(3) CRUCIALLY — never drive the eth-reset GPIO
> (GPIO5_IO02)**: pulsing it wipes stock's init and stage-2 carries no
> replacement (the heavy ~150-reg + 8051-fw + OCP-table port in
> `re/rtl83_switch_clean/` is thereby *avoided*). No MDIO/switch node
> needed (FEC sees a fixed link; chip forwards CPU↔wire in HW).
> [P1] verified at the stage-2 prompt: `Net: Switch:
> RTL8363NB-VB(0x1cc943)` / `eth0 active` / `DHCP client bound to
> 192.168.10.56` / `ping 192.168.10.123 → is alive`. Files:
> `imx8mm-polycom-proline-exec.dts` (`&fec1` + `pinctrl_fec1`),
> defconfig `+CONFIG_PHY_FIXED=y`. Net: **F3 (DHCP-66 netboot) is now
> unblocked.** Caveats (polish, non-blocking): `CONFIG_NET_RANDOM_
> ETHADDR=y` → a different MAC each boot (set a stable `ethaddr`/fuse
> for DHCP reservations); the test DHCP scope didn't hand option-3
> (gatewayip empty — fine for same-subnet TFTP, F3 uses opt-66
> serverip). This is the **u-boot** path; the *kernel*-side
> RTL8363NB-VB (DSA/realtek-mdio, FEC-MDIO re-init) remains a separate
> pre-existing open blocker.
>
> ### ✅ 2026-06-28 — PIVOT: boot method = UNLOCKED `boota` (D1 SUPERSEDED)
> The original D1 ("strip AVB+Android entirely → plain `booti`/FIT; no
> boota, no vbmeta") is **REVERSED.** Stage-2 now boots via the **NXP FSL
> Android `boota` path run UNLOCKED**, so a *single* established method
> boots **unsigned** Android-format images — both stock Android and our
> Linux. See commit `529ffc9` and `targets/tc8-chainload-uboot/
> uboot-overlay/drivers/fastboot/fb_fsl/`. Why the change: `boota` already
> does BCB slot-select + image unpack + DTB/DTBO + cmdline, the C60 dual-boot
> already produces Android slot images, and forcing AVB *unlocked* sidesteps
> signing entirely — simpler than maintaining a parallel `booti`/FIT path.
> - **Unlock stub:** `fb_fsl/fastboot_lock_unlock.c`
>   `fastboot_get_lock_stat()` → `FASTBOOT_UNLOCK` (always unlocked → AVB
>   tolerates the signature mismatch and boots; also dodges the
>   fbmisc-read-error → LOCK trap). This is what makes unsigned boot legal
>   to the FSL path.
> - **5 genuine NXP boota fixes** for legacy v0 images (modern v3/v4 never
>   exercise them), all in the vendored overlays:
>   1. `fb_fsl_boot.c`: **inverted v0 magic check** —
>      `is_android_boot_image_header()` returns TRUE for a *valid* header,
>      but `do_boota` treated TRUE as "bad magic" and failed every v0 image.
>   2. `fb_fsl_boot.c`: **always copy the boot.img ramdisk** —
>      `SYSTEM_RAMDISK_SUPPORT` skipped the copy yet still set
>      `ramdisk_size`, handing `booti` a stale ramdisk (→ init -13).
>   3. `fb_fsl_boot.c`: place `androidboot.force_normal_boot=1` **ahead of**
>      the long `dm=`/avb cmdline so it stays within the cmdline length cap.
>   4. `mach-imx/Kconfig`: **drop `ANDROID_AB_SUPPORT`'s force-select of
>      `SYSTEM_RAMDISK_SUPPORT`** (our images are not system-as-root).
>   5. (the unlock stub above — the 5th overlay file.)
> - **Image model:** our Linux ships as an Android **slot image** —
>   `boot.img` + `dtbo` (dtb inside a DTBO container) + `vbmeta`
>   (AVB `--algorithm NONE`), flashed to a GPT slot (A = replace stock, or
>   B). rootfs → `userdata` (Android sparse). **Stock GPT is kept** — we do
>   NOT repartition / `gpt write`; we flash into existing slots.
> - **defconfig** (`polycom_proline_exec_defconfig`): now KEEPS
>   `ANDROID_SUPPORT`, `CMD_BOOTA`, `ANDROID_AB_SUPPORT`, `SHA256`
>   (re-enabled — see §3/§6). `mmcboot` env = **`boota`** (active slot via
>   the `misc` bootctrl); `board_late_init` re-asserts our gesture/logo
>   `bootcmd` because `imx8mm_evk_android.h` `#undef`s `CONFIG_BOOTCOMMAND`.
> - **Gesture mapping UPDATED:** `do_gesture`/`do_bootsel` now map
>   **4 fingers → fastboot** (`fastboot usb 0`, the web provisioner's entry),
>   **5 → SDP/UUU**, none → normal `bootcmd`. (The old **4 → eMMC UMS** mode
>   is GONE.)
> - **`osprep` at OS handoff** (run from `bootcmd` before the OS boots):
>   clears the panel to black + clears `HW_LCDIF_CTRL.RUN` to **stop the
>   eLCDIF scan-out** (kills u-boot→Linux transition garbage), and
>   **re-latches the GT9271 to 0x5d** (bootsel re-latched it to 0x14 for
>   gesture polling; the Linux DT is `touchscreen@5d` with no `reset-gpios`,
>   so the OS can't re-address it → osprep restores 0x5d so booted-OS touch
>   works). Commits `bca0cee`, `1b27955`, `cdaab96`.
> - **⚠ Stock-via-boota was ABANDONED.** Unlocked `boota` reaches stock
>   Android's init and runs first+second stage, but stock is **Android-9
>   system-as-root + dm-verity** and loops in recovery — a separate effort.
>   Our Linux slot has **none** of that (plain rootfs on `userdata`, no
>   verity), so it boots cleanly. The unified path is real; only *stock's*
>   completion is out of scope.

## 0. Goal & philosophy

Owner has total control of an e-waste-liberated Polycom i.MX 8M Mini
device. Nothing the vendor did to lock the box survives; bricking is
always recoverable via BootROM SDP; per-device factory identity
(`ethaddr`, `cert`, `presistdata`) is never trampled. Base tree is the
**NXP `nxp-imx/uboot-imx`** fork (pinned tag, vendored), driven through
the `targets/` + `uboot-overlay/` pattern already proven on C60.

## 1. Locked decisions (2026-05-16)

| # | Decision | Choice |
|---|----------|--------|
| D1 | AVB / boot-image | **~~Strip AVB+Android entirely → plain `booti`/FIT~~ → SUPERSEDED 2026-06-28: boot via NXP FSL `boota` run UNLOCKED** (AVB forced `FASTBOOT_UNLOCK`) so one path boots **unsigned** Android-format slot images (stock Android + our Linux). Keep stock GPT; flash into existing slots. See the 2026-06-28 bench entry above and `529ffc9`. |
| D2 | Persistence | **Flash to eMMC (persistent)** is the goal. Validate via SDP-RAM-load first; eMMC-flash is a later milestone. SDP-recovery copy stays untouchable. |
| D3 | Console | **Always-open, no password.** Non-zero `bootdelay`, any-key interrupt, unconditional shell. |
| D4 | Target focus | **TC8-first** (`tc8-chainload-uboot`). C60 stays the working reference; not lockstep. TC8 not yet in UUU — source work proceeds; on-device gated on user. |

New feature requirements (same session):

| # | Feature | Summary |
|---|---------|---------|
| F1 | **Touch boot-selector** | Read Goodix **GT9xx** finger-count at power-on: **5 fingers → SDP/UUU**, **4 fingers → eMMC boot**, none → normal `bootcmd`. |
| F2 | **Display splash/icon** | Bring panel up in u-boot, show a boot icon. **Staged after F1** (see §5). |
| F3 | **DHCP-66 netboot** | `bootcmd` does DHCP; if opt-66 (TFTP server) + opt-67 (bootfile) present, TFTP the image and boot it; else fall through to local boot. |

## 2. Boot decision flow (target `bootcmd`)

```
stock signed stage-1 (boot0)  ── persisted env bootcmd: mmc read boot1 → go
  └─ chainloaded stage-2 (our U-Boot 2024.04, in eMMC boot1):
       bootcmd = run gesture_sel; osprep; run dhcp66_boot; run mmcboot
            1. GESTURE SAMPLE (F1)  ── GT9271 I2C touch-count, debounced
                 5 fingers → enter BootROM SDP (USB-download)  [hard escape]
                 4 fingers → fastboot gadget (web provisioner entry)
                 else      → continue (skip_net unset)
            2. osprep  ── clear panel + stop eLCDIF + re-latch GT9271 → 0x5d
            3. DHCP-66 (F3, unless gesture skip)
                 dhcp; if ${serverip} && ${bootfile} → tftp → boot   ✦ wins
            4. LOCAL BOOT
                 mmcboot = `boota` (unlocked) — active GPT slot via the misc
                 bootctrl: boot_<slot> + dtbo_<slot> + vbmeta_<slot> → booti
            5. FALLBACK
                 console; last resort = SDP re-entry
```

Precedence: **gesture > DHCP-66 > local slot (boota) > fallback/SDP.**
Gesture is sampled before the network so a stuck/rogue DHCP can always be
overridden by hand on a button-less chassis.

## 3. Feature matrix (baseline vs. work)

Legend: ⚙ = defconfig/env only · ✎ = new code/DTS · ✓ = already in base
overlay (C60-proven)

### Lock / verification removal — D1 (REVISED 2026-06-28: unlocked `boota`)
The original "strip AVB+Android" rows are superseded. We now KEEP the FSL
Android stack and **force it UNLOCKED** instead of removing it — one path
boots unsigned images. (Crossed-out rows = the old plan, for history.)
| Item | How | St |
|------|-----|----|
| ~~No AVB/vbmeta/dm-verity gate~~ → **AVB present but forced UNLOCKED** | `fastboot_get_lock_stat()→FASTBOOT_UNLOCK` stub; `vbmeta --algorithm NONE`; our rootfs has no dm-verity | ✎ |
| ~~No Android image / boota~~ → **KEEP `boota`** (unlocked) | defconfig KEEPS `ANDROID_SUPPORT,ANDROID_AB_SUPPORT,CMD_BOOTA,SHA256`; `mmcboot=boota` | ⚙✎ |
| BCB / misc A-B slot select | KEPT — `boota` reads the active slot from `misc` bootctrl; `fastboot set_active` flips it | ⚙ |
| ~~No fastboot lock-state gate~~ → unlock stub IS the gate, pinned to UNLOCK | `fb_fsl/fastboot_lock_unlock.c` | ✎ |
| No anti-rollback | unlocked AVB tolerates the signature/rollback mismatch and boots | ✎ |
| No Polycom `is_usb_boot`→fastboot trap | clean port doesn't have it; assert in bootcmd | ✓ |
| Env RW, `saveenv` works | `ENV_OVERWRITE=y`, `ENV_IS_IN_MMC` @0x700000 | ✓ |
| Keep stock GPT (no repartition) | flash our Linux into an existing slot (A or B) + `userdata`; never `gpt write` | ✎ |
| HABv4 (C60) | left **open** (unsigned SPL loads via SDP). On TC8 HAB is **closed** → we don't touch stage-1; we chainload an unsigned stage-2 from boot1 instead | ✓ |

### Owner access — D3
| Item | How | St |
|------|-----|----|
| Always-on serial console | `DM_SERIAL`+`MXC_UART`, stdout=console UART | ✓ |
| Interruptible autoboot, no pw | `BOOTDELAY` ≥ 3, no `AUTOBOOT_KEYED`/password | ⚙ |
| `fastboot`+`ums`+`dfu` no unlock | `CI_UDC`,`CMD_FASTBOOT`,`CMD_USB_MASS_STORAGE`,`CMD_DFU` | ✓ |
| SDP/uuu RAM-load first-class | `SPL_USB_SDP_SUPPORT`,`CMD_USB_SDP` + BMOD=00 | ✓ |
| `usbg1` DM gadget node | TC8 u-boot DTS: `fsl,imx27-usb-gadget`+`chipidea,usb`→`&usbotg1`, `dr_mode=peripheral` | ✎ |
| SDP escape from u-boot & Linux | bootcmd cmd + Linux `hab_failsafe` SIP (documented) | ✓ |

### Boot flexibility
| Item | How | St |
|------|-----|----|
| We own `bootcmd` | env: `gesture_sel; dhcp66_boot; distro_bootcmd; bsp_bootcmd` | ✎ |
| eMMC/USB/SD/TFTP/NFS/FIT/booti | `DISTRO_DEFAULTS`,`FIT`,`CMD_DHCP`,`CMD_FS_GENERIC` | ✓ |
| A/B kept, owner-picked, no forced rollback | env `target_slot`, no BCB | ✎ |

### F1 GT9xx gesture selector

**[P2] Stock GT9xx wiring — from `captures/firmware/lcc.dtb` `gt9xx@14`
(deterministic, decompiled):** `compatible="goodix,gt9xx"`, **`reg=<0x14>`**
(I2C addr 0x14 — Goodix latches 0x14 when **INT is HIGH** at reset-release,
0x5d when low), **`reset-gpios=<&gpio2 3 GPIO_ACTIVE_LOW>`** (GPIO2_IO3),
**`irq-gpios=<&gpio1 9 ...>`** (GPIO1_IO9), `interrupts=<9 2>`,
**`goodix,driver-send-cfg=<1>`** + **`goodix,cfg-group2=[50 20 03 … 09 01]`**
(186-byte config the host MUST push), `touchscreen-size-x=0x320`(800),
`-y=0x500`(1280). Implication: the controller ACKs I2C but its firmware
does not scan/report (status `0x814E` stays 0) until (a) it's brought up
at **0x14** (INT high at reset) and likely (b) the **cfg-group2** config
is written (Goodix cfg reg `0x8047`). Our earlier reads at 0x5d w/o config
= always 0 touches → "no gesture".

**[P1] ✅ F1 GESTURE RESOLVED (user eyes-on + console, 2026-05-16):**
3-finger gesture registers → submarine logo swaps to the RJ45/network
icon → `bootsel: 3 fingers -> DHCP-66 netboot`. The complete KEPT
recipe (took 3 iters; the int-sync was the final piece):
1. **Canonical Goodix GTP `gtp_reset_guitar` timing:** RST low, hold
   **20 ms**, INT **HIGH** (→ addr **0x14**; low = 0x5d),
   **udelay 2 ms**, RST high, **6 ms**, RST→input.
2. **`gtp_int_sync` (THE missing piece, iter-3):** INT low → **50 ms**
   → INT→input, then 50 ms settle. Stock has `goodix,int-sync=<1>`;
   without it the GT9xx never produces coordinate frames (0x814E
   stays 0) even with correct addr+config.
3. **Push the stock 186-byte `cfg-group2`** to reg **0x8047** at 0x14
   (verbatim from lcc.dtb; checksum/`config_fresh` already valid).
4. Poll `0x814E` (bit7 ready, low nibble = count); **~1 s settle on
   the MAX count** before acting (fingers land staggered; 5 passes
   through 3,4).
All in `imx8mm_evk.c` (`gt9271_reset`/`gt9271_send_cfg`/`gt9271_cfg[]`
/`do_bootsel`). Residual: the 3→netboot *action* needs ethernet up in
stage-2 (`No ethernet found` — separate F3 item); 4→eMMC / 5→UUU don't.

**[P1] ✅ T8 BOOT-UX DELIVERED END-TO-END (user eyes + host, 2026-05-16):**
chainloaded stage-2 → rotated submarine BMP on black (no VIDEO_LOGO
corner) → 20 s window (bench; dial → ~4 s for prod) → GT9271 gesture →
**~2 s max-count settle** (catches staggered landing: log shows
`detected(3) → settled on 4`) → swap to that mode's rotated icon →
action. Mode actions, all wired in `do_bootsel` (mapping as of 2026-05-16;
**SUPERSEDED 2026-06-28 → 5=SDP, 4=fastboot, none=normal**; the 4→UMS mode
below is GONE — fastboot now drives provisioning over the shared gadget):
- **3 → DHCP-66 netboot** (`skip_net` clear → `dhcp66_boot`); needs F3.
- **4 → eMMC as UMS** — `run_command("ums 0 mmc 0")`. **CONFIRMED:**
  host enumerates our gadget `1fc9:0152` on the **TC8 path 3-2.1**
  (C60 = `0134/0151` "SE Blank" 3-2.2 — distinct, no cross-stream) →
  `sd … [sda] 30777344 512-byte blocks (15.8 GB)` = whole TC8 eMMC
  mounted on the host. Icon stays up while mounted.
- **5 → SDP/UUU** (`sdp 0`; uuu reflash; anti-brick escape).
Assets: 4× 256² 24bpp BMP rotated 90° CCW, eMMC kernel_bak scratch
LBA 0x21000 (256 KB slots), `bmp_blob.bin` md5 bdf37371; board reads
`mmc dev 0` (stage-2 alias mmc0=usdhc3). Open follow-ups: F3 ethernet
(so 3-finger fully netboots), prod window value, auto-persist chainload.

**[P2] ✅ ON-eMMC NESTLE / NO-GO MAP (2026-05-16, user-approved):**
The stage-2 + blob were in `kernel_bak` (LBA 0x20000) — **WRONG**: in
the flat T8 FW (`tc8-firmware-build/FLASHING.md`; A/B ditched, single
large rootfs) `kernel_bak` is the *load-bearing rollback-kernel
partition* — `slotbboot` does `mmc read 0x40000000 0x20000 0x18000;
booti` on `boot_slot=bak`. Relocated into the **reserved unallocated
pre-GPT gap**:
- `0x0–0x2000` HAB-signed SPL/u-boot (⛔ UUU only) · `0x2000` env (⛔)
  · `0x2008–0x4000` free (opt. redundant env)
- **`0x4000` (0x800000) = stage-2 `u-boot.bin`** → chainload
  `mmc dev 1; mmc read 0x40200000 0x4000 0x830; dcache off; go`
- **`0x5000` (0xA00000) = BMP blob** (256 KB slots
  logo/uuu/emmc/net = 0x5000/0x5200/0x5400/0x5600; board `mmc dev 0`)
- `0x5800–0x8000` free · `0x8000+` flat GPT (`kernel` first)
**Contract:** flat-GPT first partition MUST stay ≥ `0x8000`; installer
must never allocate/reclaim `0x4000–0x8000`. Documented in
FLASHING.md. Changed: board MMC_* defines, wsce2/wsbootsel chainload,
tc8_full.sh dd `seek=16384`/`20480`.

**[P1] ✅ AUTO-PERSIST CONFIRMED (console, 2026-05-16):** stage-2 is
now the *default* boot with **zero serial/human**. Mechanism: the stock
env (byte 0x400000/LBA 0x2000, CRC32-only, NOT HAB-signed) `bootcmd` is
rewritten to **chainload-only** (user choice: no stock tail):
```
setenv bootcmd 'mmc dev 1\; mmc read 0x40200000 0x4000 0x830\;
  dcache flush\; icache off\; dcache off\; go 0x40200000'
saveenv
```
Self-perpetuating: stock `saveenv`s the live env **every boot**, so our
`bootcmd` survives — verified by **2 consecutive bare `reset`s each
autonomously producing the `U-Boot 2024.04` stage-2 banner**
(STAGE2_BANNERS=2; STAT "AUTO-PERSIST CONFIRMED"). Stage-2 then runs
`bootcmd = run gesture_sel; run dhcp66_boot; run mmcboot`
(gesture_sel=bootsel; no-gesture sets skip_net=1 → dhcp66 skipped →
**`mmcboot`** = `mmc dev 0; raw-read flat-GPT kernel@0x8000 +
dtb@0x38000; booti` = the deterministic local OS boot). Recovery: old
`bootcmd` captured (this bench: `run slotbboot`); `bootdelay` still 3 so
stock is re-catchable → `setenv bootcmd …; saveenv`; or emmc.img
env@0x400000; or BootROM SDP (BMOD strap, HAB-independent). Helpers:
`/tmp/wspersist.py`, `/tmp/tc8_persist.sh`.

**[P2] SHIPPABLE INSTALLER CODE-COMPLETE (2026-05-16):**
`tc8-firmware-build/smoke/onboard.sh` extended — if `stage2-uboot.bin`
+ `bmp_blob.bin` are in the artifacts dir → `STAGE2_ENABLED`:
`install_env` sets `bootcmd` = the wspersist-proven chainload (else
`run slotbboot`); new `write_stage2_ums()` dd's stage-2 @LBA 0x4000 +
blob @0x5000 (readback md5-verified) into the gap the flat GPT already
reserves (first partition @16 MiB). One-command unlock-FW installer,
`bash -n` clean. **GATE — real end-to-end run needs the OS artifacts**
(`Image`/`imx8mm-tc8.dtb`/`rootfs.img[.zst]`): NOT built anywhere
(`/tmp/tc8-v0.3.0` empty; no kernel/rootfs on host or aibox). Building
them (`tc8-firmware-build` build.sh + rootfs.sh) is the remaining
blocker for the `mmcboot`→Linux end-to-end validation.

| Item | How | St |
|------|-----|----|
| Minimal GT9xx I2C reader | new board hook: I2C@0x14 bus-1, read status reg → buffer-status + touch-count nibble; rst/irq GPIO bring-up; ~150 ms debounce window | ✎ |
| Map count→action | **5→`sdp 0` (SDP/UUU); 4→`fastboot usb 0` (web provisioner); else normal** (2026-06-28; was 4→UMS) | ✎ |
| Serial-confirmed, no UI | prints `gesture: N finger(s) → <action>` | ✎ |

### F3 DHCP-66 netboot
| Item | How | St |
|------|-----|----|
| DHCP + opt66/67 | `CMD_DHCP` populates `serverip`/`bootfile` from opt 66/67 | ✓ |
| `dhcp66_boot` env | `dhcp; test -n ${serverip} -a -n ${bootfile} && tftp ${loadaddr} ${bootfile} && booti/bootm` | ✎ |
| Skipped on 4-finger | gesture sets a flag the stanza checks | ✎ |

### F2 display splash (staged — §5)
| Item | How | St |
|------|-----|----|
| LCDIF + Samsung DSIM + Raydium-class panel | `VIDEO_MXS`,`VIDEO_IMX_SEC_DSI`,`VIDEO_LCD_RAYDIUM_RM67191` (NXP downstream drivers, in base) | ✓ (driver) / ✎ (TC8 DTS+timing) |
| Splash bitmap | `SPLASH_SCREEN`,`VIDEO_LOGO`,`CMD_BMP`,`BMP_*BPP` | ✓ |
| TC8 panel node + PWM4 backlight + reset GPIO2_IO05 | TC8 u-boot DTS | ✎ |

## 4. Anti-brick guarantees (non-negotiable)

1. **BMOD=00 USB-download always wins** — bypasses u-boot+AVB+BCB+touch+UART (BootROM-level). Documented escape on a button-less chassis.
2. **Never overwrite the SDP-recovery SPL/u-boot copy.** On TC8 the active u-boot is in `mmcblk2boot0` HW partition; the user-area SDP copy at eMMC `0x8400` is the rescue and is off-limits to our reflasher.
3. **Per-device identity preserved** — `ethaddr` (env, not fuses), stock `cert` (#9), `presistdata` (#12). MAC restore logic retained.
4. **A bad env/bootcmd can't lock out recovery** — gesture sample + console interrupt run before any env-scripted boot; SDP is reachable regardless of env state.

## 5. Display staging & de-risk

Display-in-u-boot inherits the **same hardware blockers** as the kernel
fight (Samsung-DSIM PMS, LCDIF↔DSIM clock coherence — see repo
`re/C60_DISPLAY_SPEC.md`, `re/C60_LCDIF_DATAPATH.md`). Therefore:

- **F1 ships first, display-independent.** Gesture selection is
  serial-confirmed; a button-less device is fully controllable with zero
  pixels.
- **F2 layers on after** the panel pipeline is proven on the bench.

De-risk finding (2026-05-16): the NXP `uboot-imx` base **already ships**
`VIDEO_IMX_SEC_DSI` (Samsung DSIM) + `VIDEO_LCD_RAYDIUM_RM67191` +
`SPLASH_SCREEN`, and the C60 unlock defconfig already enables them. This
is NXP's *own untouched downstream* sec-dsi/raydium driver — the very
code we have been porting into the mainline kernel by hand. In u-boot it
may come up with only the correct DTS + panel timing, **decoupled from
the mainline-kernel DRM/runtime-PM deadlock**. Treat u-boot display as a
*separate, possibly-easier* bring-up; capture any working DSIM
PMS/LCDIF clock values back into `re/C60_DISPLAY_SPEC.md`.

## 6. Implementation map

- **Defconfig** `targets/tc8-chainload-uboot/uboot-overlay/configs/polycom_proline_exec_defconfig`
  — fork C60's; **(REVISED 2026-06-28 for unlocked `boota` — D1 reversal)**
  **keep/enable** the FSL Android stack: `ANDROID_SUPPORT,
  ANDROID_AB_SUPPORT, CMD_BOOTA, AVB_SUPPORT, LIBAVB, SHA256` — AVB is left
  in but forced UNLOCKED by the `fastboot_get_lock_stat()→FASTBOOT_UNLOCK`
  overlay stub (so unsigned slot images boot); `USB_GADGET_OS_DESCRIPTORS=y`
  (shared `f_fastboot` WinUSB gadget); **add**: `BOOTDELAY=3`; **also keep**:
  FIT/DISTRO/VIDEO/SEC_DSI/RAYDIUM/SPLASH/CI_UDC/CMD_DHCP/CMD_TFTPBOOT/I2C/
  USDHC/FEC. (Original plan was to *strip* ANDROID/AVB/BCB/BOOTA and run
  plain `booti`/FIT — superseded; see the 2026-06-28 bench entry.)
- **DTS** `targets/tc8-chainload-uboot/uboot-overlay/arch/arm/dts/imx8mm-polycom-proline-exec.dts`
  — mirror C60 minimal DTS + add: GT9xx `i2c@0x14` (bus-1) w/ rst+irq
  GPIOs; panel/DSIM/PWM4-backlight/reset-GPIO2_IO05; correct console
  UART (confirm from stock TC8 DTB); PMIC; `usbg1` gadget; eMMC USDHC.
  DDR table is a symlink to the C60 LPDDR4 table (shared, verified).
- **Gesture hook** — board late-init (or pre-`bootcmd` env via a small
  C cmd `do_gesture`): GT9xx reset/irq, I2C status-reg read, count map.
- **Env** — `uboot-overlay` env additions: `gesture_sel`, `sdp_enter`,
  `emmc_only`, `dhcp66_boot`, `target_slot`; rewire `bootcmd`.
- **Build** — `scripts/build.sh` currently hardcodes kepler defconfig/DTB
  names; parameterize from `target.env` (`DEFCONFIG`, `DTS`).

## 7. Per-target status

| Target | DDR | defconfig | u-boot DTS | gesture | dhcp66 | splash | on-device |
|--------|-----|-----------|-----------|---------|--------|--------|-----------|
| c60-kepler_proto1 | ✓ | boota+AVB (pre-unlock) | ✓ | — | — | drivers on | uuu dual-boot works |
| tc8-chainload-uboot | ✓ (symlink) | ✓ (unlocked boota) | ✓ | ✓ | ✓ | ✓ (panel up) | **DELIVERED + proven on HW** |

C60 will be migrated to the unlocked (D1) defconfig *after* TC8 proves it,
to avoid regressing the working reference.

## 7a. TC8 hardware map — from stock `lcc.dtb` [P2, deterministic]

Decompiled `re/firmware/_archive_pre_release/lcc.dtb`
(`model = "Polycom TC8 (LCC PROTO)"`, `compatible = "poly,tc8"`). TC8
mirrors C60 closely:

| Block | TC8 stock | vs C60 |
|-------|-----------|--------|
| Console | `serial@30890000` = **UART2** (`ttymxc1`), bootargs empty in stock DT | same (uart2) |
| eMMC | `mmc@30b60000` = **usdhc3**, `non-removable` | same (usdhc3) |
| PMIC | **`rohm,bd71837` @ I2C1 (0x30a20000) 0x4b** | same |
| Touch | **`goodix,gt9271` @ I2C2 (0x30a30000) reg `0x5d`**, `irq-gpios=<ph0x28 9 0>`, size-x=0x320 (800), size-y=0x500 (1280) | C60 differs |
| Panel | `panel@0` under **`dsi@32e10000`** (Samsung sec-dsi), `reset-gpios=<ph0x2f 5 1>` (GPIO IO5 active-low), `backlight`=pwm-backlight | same class |
| DSI | `dsi@32e10000` `samsung,pll-clock-frequency=24MHz` ref, `samsung,esc-clock-frequency=10MHz`; `lcdif@32e00000` | feeds C60_DISPLAY_SPEC PMS work |

**Touch address quirk (F1-critical):** GT9-series I²C address is **0x5d by
default**, **0x14 if INT is held during reset** (stock Linux log shows
`1-0014`). The gesture reader must handle both: try `0x5d`, fall back to
`0x14`, or replicate the stock INT/reset timing. GT9271 status reg
(0x814E) low nibble = touch-point count — that single byte is all F1
needs (no driver config-set push required: hypothesis, validate on device).

## 7b. F2 panel bringup — RESOLVED in SW (deterministic) [P2]

Panel path implemented & compiling (flash.bin green 2026-05-16):

- **Driver**: LCC variant added to NXP `raydium-rm67191.c`
  (overlay copy) — new compatible `poly,lcc-proto`, MCS init tables +
  sequence transcribed verbatim from `re/c60_lcc_mcs_tables.md`
  (Enter Page3/GIP_1-3/Page4/Page1/Page0, TE-on, sleep-out 120 ms,
  display-on 150 ms). `rad_platform_data` gained a per-variant
  `.timing`.
- **Timing**: from `re/c60_stock_panel_full_decomp.md` —
  **800×1280, pixelclock 76 556 400 Hz**, hfp/hbp=81, hsync=12,
  vfp=8, vbp=18, vsync=4, HSYNC/VSYNC/DE-LOW + PIXDATA-negedge.
- **DTS**: base `imx8mm.dtsi` already carries the lcdif↔dsi graph;
  TC8 DTS enables `&lcdif`, retargets `&mipi_dsi` to the u-boot
  sec-dsi host (`compatible = "samsung,sec-mipi-dsi"`), adds
  `panel@0 "poly,lcc-proto"` (4-lane, burst, `reset-gpio`=&gpio2 5
  ACTIVE_LOW), `&pwm4` + `pwm-backlight` (period 25 000 ns, dflt
  250/255).
- **Backlight pad**: DETERMINISTIC — decoded stock `pwm4grp`
  (`<0x64 0x2cc..0x05..0x10>`) = `GPIO1_IO15` ALT5 `PWM4_OUT`,
  pad-conf `0x10`, pwm@30690000. (Earlier `backlightgrp`/PWM1
  hand-trace was wrong; corrected via phandle-0x23 resolution.)

Still SW-pending for F2: splash icon (Colloid-icon-theme → BMP →
splash env). On-device-only (#8): DSIM PMS lock at 76.5564 MHz, the
PROVISIONAL `disp_reset` pad drive, actual illumination.

## 8. Open questions / risks

- ~~TC8 console UART~~ → **UART2** (resolved, §7a).
- ~~TC8 PMIC~~ → **BD71837 @ I2C1 0x4b** (resolved, §7a).
- ~~Touch controller~~ → **GT9271 @ I2C2 0x5d/0x14** (resolved, §7a);
  the 0x5d↔0x14 dual-address dance still needs on-device confirmation.
- GT9271 INT/reset micro-sequence to get a stable status-reg read in
  early u-boot — phandle 0x28 (irq) / 0x2f (panel-rst) gpio banks need
  resolving from stock pinctrl when writing the DTS.
- eMMC-persistent flash layout vs. `mmcblk2boot0` + SDP-recovery copy +
  factory partitions — detailed layout spec deferred to D2 milestone.
- Gesture timing window vs. SPL/u-boot boot speed — needs on-device tune.
</invoke>

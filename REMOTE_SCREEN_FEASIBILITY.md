# Remote Screen — Feasibility Study

**Question:** Can we ship a "remote screen" for the Polycom i.MX8MM devices —
mirror the device's panel to a host, over **network or USB**, delivered as an
**app target** in this repo?

**Short answer:** Yes, and cheaply, for the *bootloader* use case. The
framebuffer is a plain linear buffer in DRAM that we already read and write
directly (see `c60_display_test()` in the C60 board file), the USB gadget that
would carry the pixels is already built, debugged, and browser-reachable
(WebUSB, `1fc9:0152`), and both targets already bring up FEC ethernet. The
realistic v1 is a **one-way framebuffer mirror served over the existing
fastboot USB gadget**, viewed in a browser `<canvas>` — no new gadget, no TCP,
no native host tool. Network transport and a standards VNC server are sensible
phase-2/3 add-ons. True remote *control* (input injection) is a separate, harder
problem.

This is scoped to **U-Boot only** — it mirrors what the bootloader paints
(splash, boot-selector menu, provisioning/diagnostic screens), up to the OS
handoff where `osprep` tears the panel down. It is not a mirror of booted
Android/Linux.

---

## 1. Why this is worth building

The repo's own history is the business case. Bench bring-up here is done
*blind*: there is "no UART on this bench," so diagnostics are published as
`fastboot.disp_*` env vars and read back with `fastboot getvar`, and the panel
is confirmed lit by **pointing a bench camera at the glass** and shining a
torch at it (see the flashlight test-pattern comment in
`targets/c60-kepler_proto1/uboot-overlay/board/freescale/imx8mm_evk/imx8mm_evk.c`).

A remote screen replaces the bench camera with an exact, pixel-for-pixel copy
of what the panel is showing, delivered to the host that is already driving the
board over USB. For panel bring-up, boot-menu work, and provisioning support
it is a direct, large workflow win — not a speculative feature.

---

## 2. What the hardware and current build already give us

| Capability | Status in-tree | Source |
|---|---|---|
| **Linear framebuffer in DRAM** | Live. `struct video_priv` exposes `fb`, `xsize`, `ysize`, `line_length`, `bpix`. We already scan it pixel-by-pixel. | `imx8mm_evk.c` `c60_display_test()` |
| **Panel geometry** | C60: **720×1280**; TC8: RM67191 default **1080×1920** (portrait). | `raydium-rm67191.c` timings |
| **Pixel format** | 16bpp (RGB565) or 32bpp (XRGB). Trivial host-side conversion. | `pr->bpix == VIDEO_BPP16/32` |
| **USB device gadget** | ChipIdea UDC, USB2 High-Speed, download framework, **f_fastboot with WinUSB/WebUSB OS descriptors**, VID/PID `1fc9:0152`. | defconfigs `CONFIG_CI_UDC`, `CONFIG_USB_GADGET_OS_DESCRIPTORS`, `CONFIG_USB_FUNCTION_FASTBOOT` |
| **Browser reachability** | Existing WebUSB/WebSerial provisioner already talks to that gadget. | README "Provisioning" |
| **Ethernet / IP** | FEC up, `ETHPRIME="FEC"`, DHCP/ping/TFTP/SNTP; TC8 already netboots via `dhcp66_boot`. | defconfigs `CONFIG_FEC_MXC`, `CONFIG_CMD_NET/DHCP/PING` |
| **Fastboot-over-UDP** | Compiled in. A ready UDP framing precedent. | `CONFIG_UDP_FUNCTION_FASTBOOT` |

**Framebuffer sizes** (one frame, 32bpp): C60 720×1280×4 ≈ **3.5 MiB**;
TC8 1080×1920×4 ≈ **7.9 MiB**. At 16bpp, half that.

---

## 3. The three constraints that shape the design

These are U-Boot realities, not blockers, but they decide what "remote screen"
can mean.

1. **No preemptive multitasking.** U-Boot is a single-threaded cooperative
   command loop; nothing services USB/net while it blocks in another command.
   ⇒ The screen is served by a **dedicated command that owns the CPU** (e.g.
   `screen serve`), looping until a key/host-disconnect. You mirror *the screen
   the bootloader is currently on* (menu, diagnostic, splash). A background
   "mirror while U-Boot does unrelated work" is not on the table without
   invasive hooks into every poll loop. For the bring-up/provisioning use case
   this is exactly what you want anyway.

2. **No hardware video encode; one A53 core.** The VPU/GPU are not brought up
   in U-Boot and SMP is not started — you have **a single Cortex-A53** and
   CPU-only compression. That rules out H.264 but is fine: raw or a light
   run-length / LZ pass is plenty at bootloader frame sizes and the modest
   frame rates below. Bootloader screens are near-static (menus, text), so RLE
   wins big and per-frame *dirty-rectangle* diffing makes the steady state
   almost free.

3. **Single-buffered, DMA-shared framebuffer + cache.** The LCDIF DMA scans the
   same buffer the CPU writes; `video_sync()` already flushes dcache after we
   draw. A scraper must **invalidate/clean the FB range before reading** to
   avoid stale cache lines, and tolerate tearing (no vsync/page-flip in this
   path). Both are minor and local.

Two more to note, not on the critical path:

- **U-Boot has no TCP *server*.** 2024.04 has a minimal client-side TCP
  (`wget`), but no listen/accept. A UDP push protocol needs no new stack; a
  real **RFB/VNC server needs a small TCP-accept path added** (phase 3).
- **Security / attack surface.** A framebuffer-export (and especially any
  input-inject) service running in the bootloader is sensitive — more so on
  **TC8, which ships HAB-closed and forces AVB *unlocked***. Keep it behind a
  build flag and a gesture/opt-in, never default-on in a shipping image. On C60
  (HAB open) the exposure bar is lower.

---

## 4. Design options, cheapest first

### Option A — Framebuffer mirror over the existing fastboot USB gadget  ✅ recommended v1
Reuse the download gadget in the **upload** direction. Add one vendor path — a
`fastboot oem screencap` (or an `screen`-command upload) that points the
transfer at the live FB after a cache-invalidate. Host polls frames in a loop.

- **New firmware code:** small. One command + a cache-invalidate + optional RLE.
  No new USB descriptors, no new stack — the hardest part (WinUSB/WebUSB
  enumeration) is already solved and shipping.
- **Host app:** a **web page** — the existing WebUSB channel grabs frames and
  paints a `<canvas>`. Zero install; the same tool family as the provisioner.
  A native `fastboot`-loop script is a fallback.
- **Throughput:** USB2 HS realistically ~30–40 MB/s. Raw C60 frame 3.5 MiB ⇒
  ~8–10 fps; with RLE/dirty-rects on typical menu screens, effectively
  interactive. TC8 at 7.9 MiB/frame is ~half that raw, similar after diffing.
- **Why first:** maximum reuse of already-debugged infrastructure, serves the
  documented pain point directly, no new transport risk.

### Option B — Framebuffer mirror over UDP (network)
A trivial "framebuffer-over-UDP" push (tile/scanline packets + seq numbers) to a
host listener, modeled on the compiled-in fastboot-over-UDP. Good where the host
drives the board over the wire (C60 is PoE-powered — ethernet is always
present; TC8 netboots already).

- **New firmware code:** moderate — packetize/checksum/retat over U-Boot's UDP
  API; handle loss (idempotent full-frame refresh).
- **Host app:** small native listener rendering to a window, or a tiny local
  relay to the same browser canvas.
- **Caveat:** confirm the **TC8 chassis actually exposes a usable ethernet
  port** in its deployment (C60 clearly does). Otherwise B is C60-first.

### Option C — Real RFB/VNC server (network, standards client)
Serve RFB so any stock VNC viewer connects, no custom host tool at all.

- **New firmware code:** largest — add a minimal **TCP accept** path (U-Boot
  only has client TCP today), plus RFB handshake and Raw/Hextile encoding.
  Framebuffer-only; RFB input events can be wired later for Option D.
- **Payoff:** "point any VNC client at the phone." Worth it only if a standards
  client is a hard requirement; otherwise A+B cover the workflow for less.

### Option D — Bidirectional remote *control* (screen + input)  — separate track
Add input injection so the host can drive the boot menu / touch gestures
remotely. U-Boot already ships **netconsole** (text console over UDP) as the
console analog; a graphical control would synthesize events into the GT9271
gesture logic / menu input. This is a distinct, larger effort — recommend
descoping from v1 and revisiting after A proves out.

---

## 5. "App target" — how to package it

Two readings, and the recommendation differs:

- **As a compile-time feature of the existing targets (recommended).** Remote
  screen needs the *full* board bring-up (PMIC rails, DISPMIX, DSIM, panel) that
  the c60/tc8 targets already do — it is not a standalone U-Boot "app" that
  runs without them. Ship it as a Kconfig option (e.g. `POLY_REMOTE_SCREEN`)
  plus a `screen` command, off by default, enabled per build. The **deliverable
  "app" is the host side** — ideally the browser/WebUSB viewer page, matching
  the existing provisioner.

- **As a distinct build target under `targets/` (if a separate artifact is
  wanted).** A thin variant of an existing target — same board files/DTS, a
  defconfig with `POLY_REMOTE_SCREEN=y` and a `bootcmd` that drops straight into
  `screen serve`. Mechanically easy given `scripts/build.sh` is already
  per-target and overlay-driven; it just duplicates a target dir for a
  "kiosk/mirror" personality.

Either way it slots into the current build system with no structural change.

---

## 6. Recommendation & rough effort

1. **Phase 1 (days, not weeks): Option A.** `screen`/`oem screencap` upload over
   the existing fastboot USB gadget + a WebUSB `<canvas>` viewer. RLE +
   dirty-rects. This alone retires the bench-camera workflow.
2. **Phase 2: Option B** (UDP mirror) for C60's always-on PoE ethernet; verify
   TC8 ethernet exposure before committing it there.
3. **Phase 3 (optional): Option C** RFB server, only if "any VNC client" is a
   hard requirement.
4. **Separate track: Option D** input injection for true remote control.

**Open items to confirm before Phase 1 code:** exact `bpix` per target at
runtime (16 vs 32bpp changes host decode); whether the FB range is mapped
cached (sets the invalidate step); and the fastboot upload/`get_staged` path
availability in the vendored FSL fastboot (vs. adding a small `oem` handler).

**Bottom line:** low-risk and high-leverage. The pixels are already in a buffer
we own, the wire to the host is already up and browser-addressable, and the
only genuinely new work in v1 is "read the buffer, RLE it, push it out a channel
that already exists." Recommend proceeding with Phase 1.

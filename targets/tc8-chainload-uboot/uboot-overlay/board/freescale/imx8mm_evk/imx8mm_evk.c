// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 */
#include <common.h>
#include <efi_loader.h>
#include <env.h>
#include <init.h>
#include <memalign.h>
#include <miiphy.h>
#include <mmc.h>
#include <net.h>
#include <netdev.h>
#include <asm/global_data.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm-generic/gpio.h>
#include <asm/arch/imx8mm_pins.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/gpio.h>
#include <asm/mach-imx/mxc_i2c.h>
#include <i2c.h>
#include <asm/io.h>
#include "../common/tcpc.h"
#include <usb.h>
#include <dm.h>
#include <command.h>
#include <console.h>
#include <cyclic.h>		/* schedule() — services the i.MX WDOG */
#include <linux/delay.h>
#include <video.h>
#include <fdtdec.h>

DECLARE_GLOBAL_DATA_PTR;

#define UART_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_FSEL1)
#define WDOG_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_ODE | PAD_CTL_PUE | PAD_CTL_PE)

static iomux_v3_cfg_t const uart_pads[] = {
	IMX8MM_PAD_UART2_RXD_UART2_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
	IMX8MM_PAD_UART2_TXD_UART2_TX | MUX_PAD_CTRL(UART_PAD_CTRL),
};

static iomux_v3_cfg_t const wdog_pads[] = {
	IMX8MM_PAD_GPIO1_IO02_WDOG1_WDOG_B  | MUX_PAD_CTRL(WDOG_PAD_CTRL),
};

#ifdef CONFIG_NAND_MXS
#ifdef CONFIG_SPL_BUILD
#define NAND_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_FSEL2 | PAD_CTL_HYS)
#define NAND_PAD_READY0_CTRL (PAD_CTL_DSE6 | PAD_CTL_FSEL2 | PAD_CTL_PUE)
static iomux_v3_cfg_t const gpmi_pads[] = {
	IMX8MM_PAD_NAND_ALE_RAWNAND_ALE | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_CE0_B_RAWNAND_CE0_B | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_CE1_B_RAWNAND_CE1_B | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_CLE_RAWNAND_CLE | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_DATA00_RAWNAND_DATA00 | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_DATA01_RAWNAND_DATA01 | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_DATA02_RAWNAND_DATA02 | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_DATA03_RAWNAND_DATA03 | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_DATA04_RAWNAND_DATA04 | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_DATA05_RAWNAND_DATA05	| MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_DATA06_RAWNAND_DATA06	| MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_DATA07_RAWNAND_DATA07	| MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_RE_B_RAWNAND_RE_B | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_READY_B_RAWNAND_READY_B | MUX_PAD_CTRL(NAND_PAD_READY0_CTRL),
	IMX8MM_PAD_NAND_WE_B_RAWNAND_WE_B | MUX_PAD_CTRL(NAND_PAD_CTRL),
	IMX8MM_PAD_NAND_WP_B_RAWNAND_WP_B | MUX_PAD_CTRL(NAND_PAD_CTRL),
};
#endif

static void setup_gpmi_nand(void)
{
#ifdef CONFIG_SPL_BUILD
	imx_iomux_v3_setup_multiple_pads(gpmi_pads, ARRAY_SIZE(gpmi_pads));
#endif

	init_nand_clk();
}
#endif

#if CONFIG_IS_ENABLED(EFI_HAVE_CAPSULE_SUPPORT)
struct efi_fw_image fw_images[] = {
	{
		.image_type_id = IMX_BOOT_IMAGE_GUID,
		.fw_name = u"IMX8MM-EVK-RAW",
		.image_index = 1,
	},
};

struct efi_capsule_update_info update_info = {
	.dfu_string = "mmc 2=flash-bin raw 0x42 0x2000 mmcpart 1",
	.num_images = ARRAY_SIZE(fw_images),
	.images = fw_images,
};

#endif /* EFI_HAVE_CAPSULE_SUPPORT */

int board_early_init_f(void)
{
	struct wdog_regs *wdog = (struct wdog_regs *)WDOG1_BASE_ADDR;

	imx_iomux_v3_setup_multiple_pads(wdog_pads, ARRAY_SIZE(wdog_pads));

	set_wdog_reset(wdog);

	imx_iomux_v3_setup_multiple_pads(uart_pads, ARRAY_SIZE(uart_pads));

	init_uart_clk(1);

#ifdef CONFIG_NAND_MXS
	setup_gpmi_nand(); /* SPL will call the board_early_init_f */
#endif

	return 0;
}

#if IS_ENABLED(CONFIG_FEC_MXC)
static int setup_fec(void)
{
	struct iomuxc_gpr_base_regs *gpr =
		(struct iomuxc_gpr_base_regs *)IOMUXC_GPR_BASE_ADDR;

	/* Use 125M anatop REF_CLK1 for ENET1, not from external */
	clrsetbits_le32(&gpr->gpr[1], 0x2000, 0);

	return 0;
}

int board_phy_config(struct phy_device *phydev)
{
	if (phydev->drv->config)
		phydev->drv->config(phydev);

#ifndef CONFIG_DM_ETH
	/* enable rgmii rxc skew and phy mode select to RGMII copper */
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x1f);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, 0x8);

	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x00);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, 0x82ee);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1d, 0x05);
	phy_write(phydev, MDIO_DEVAD_NONE, 0x1e, 0x100);
#endif

	return 0;
}
#endif

#ifdef CONFIG_USB_TCPC
struct tcpc_port port1;
struct tcpc_port port2;

static int setup_pd_switch(uint8_t i2c_bus, uint8_t addr)
{
	struct udevice *bus;
	struct udevice *i2c_dev = NULL;
	int ret;
	uint8_t valb;

	ret = uclass_get_device_by_seq(UCLASS_I2C, i2c_bus, &bus);
	if (ret) {
		printf("%s: Can't find bus\n", __func__);
		return -EINVAL;
	}

	ret = dm_i2c_probe(bus, addr, 0, &i2c_dev);
	if (ret) {
		printf("%s: Can't find device id=0x%x\n",
			__func__, addr);
		return -ENODEV;
	}

	ret = dm_i2c_read(i2c_dev, 0xB, &valb, 1);
	if (ret) {
		printf("%s dm_i2c_read failed, err %d\n", __func__, ret);
		return -EIO;
	}
	valb |= 0x4; /* Set DB_EXIT to exit dead battery mode */
	ret = dm_i2c_write(i2c_dev, 0xB, (const uint8_t *)&valb, 1);
	if (ret) {
		printf("%s dm_i2c_write failed, err %d\n", __func__, ret);
		return -EIO;
	}

	/* Set OVP threshold to 23V */
	valb = 0x6;
	ret = dm_i2c_write(i2c_dev, 0x8, (const uint8_t *)&valb, 1);
	if (ret) {
		printf("%s dm_i2c_write failed, err %d\n", __func__, ret);
		return -EIO;
	}

	return 0;
}

int pd_switch_snk_enable(struct tcpc_port *port)
{
	if (port == &port1) {
		debug("Setup pd switch on port 1\n");
		return setup_pd_switch(1, 0x72);
	} else if (port == &port2) {
		debug("Setup pd switch on port 2\n");
		return setup_pd_switch(1, 0x73);
	} else
		return -EINVAL;
}

struct tcpc_port_config port1_config = {
	.i2c_bus = 1, /*i2c2*/
	.addr = 0x50,
	.port_type = TYPEC_PORT_UFP,
	.max_snk_mv = 5000,
	.max_snk_ma = 3000,
	.max_snk_mw = 40000,
	.op_snk_mv = 9000,
	.switch_setup_func = &pd_switch_snk_enable,
};

struct tcpc_port_config port2_config = {
	.i2c_bus = 1, /*i2c2*/
	.addr = 0x52,
	.port_type = TYPEC_PORT_UFP,
	.max_snk_mv = 9000,
	.max_snk_ma = 3000,
	.max_snk_mw = 40000,
	.op_snk_mv = 9000,
	.switch_setup_func = &pd_switch_snk_enable,
};

static int setup_typec(void)
{
	int ret;

	debug("tcpc_init port 2\n");
	ret = tcpc_init(&port2, port2_config, NULL);
	if (ret) {
		printf("%s: tcpc port2 init failed, err=%d\n",
		       __func__, ret);
	} else if (tcpc_pd_sink_check_charging(&port2)) {
		/* Disable PD for USB1, since USB2 has priority */
		port1_config.disable_pd = true;
		printf("Power supply on USB2\n");
	}

	debug("tcpc_init port 1\n");
	ret = tcpc_init(&port1, port1_config, NULL);
	if (ret) {
		printf("%s: tcpc port1 init failed, err=%d\n",
		       __func__, ret);
	} else {
		if (!port1_config.disable_pd)
			printf("Power supply on USB1\n");
		return ret;
	}

	return ret;
}

int board_usb_init(int index, enum usb_init_type init)
{
	int ret = 0;
	struct tcpc_port *port_ptr;

	debug("board_usb_init %d, type %d\n", index, init);

	if (index == 0)
		port_ptr = &port1;
	else
		port_ptr = &port2;

	imx8m_usb_power(index, true);

	/*
	 * Polycom C60 has no USB-C TCPC (the EVK's PTN5110). tcpc_init()
	 * fails at boot ("tcpc_init: Can't find bus", err=-22) so the port
	 * structs are invalid. Calling tcpc_setup_ufp_mode() on them leaves
	 * the OTG role unset and the CI_UDC gadget never enumerates
	 * (fastboot/ums -> "USB init failed: -19"). The imx8mm OTG defaults
	 * to peripheral mode, which is exactly what we want for fastboot /
	 * ums, so just skip the Type-C role dance when the port has no
	 * valid tcpc bus.
	 */
	if (port_ptr->i2c_dev) {
		if (init == USB_INIT_HOST)
			tcpc_setup_dfp_mode(port_ptr);
		else
			tcpc_setup_ufp_mode(port_ptr);
	}

	return ret;
}

int board_usb_cleanup(int index, enum usb_init_type init)
{
	int ret = 0;

	debug("board_usb_cleanup %d, type %d\n", index, init);

	if (init == USB_INIT_HOST) {
		if (index == 0)
			ret = tcpc_disable_src_vbus(&port1);
		else
			ret = tcpc_disable_src_vbus(&port2);
	}

	imx8m_usb_power(index, false);
	return ret;
}

int board_ehci_usb_phy_mode(struct udevice *dev)
{
	int ret = 0;
	enum typec_cc_polarity pol;
	enum typec_cc_state state;
	struct tcpc_port *port_ptr;

	if (dev_seq(dev) == 0)
		port_ptr = &port1;
	else
		port_ptr = &port2;

	tcpc_setup_ufp_mode(port_ptr);

	ret = tcpc_get_cc_status(port_ptr, &pol, &state);
	if (!ret) {
		if (state == TYPEC_STATE_SRC_RD_RA || state == TYPEC_STATE_SRC_RD)
			return USB_INIT_HOST;
	}

	return USB_INIT_DEVICE;
}

#endif

/*
 * --- Polycom board flag --------------------------------------------------
 * We build per-target (tc8-chainload-uboot / c60-kepler_proto1) but share this
 * board file; gate board-specific bring-up on the DT compatible so the same
 * code generalizes. TC8 now; C60 later (add poly_is_c60()).
 */
static inline bool poly_is_tc8(void)
{
	return of_machine_is_compatible("poly,tc8");
}

/*
 * Power up the i.MX8MM DISPMIX GPC power domain EARLY (before the video
 * uclass / disp_blk_ctrl probe). In the chainload context stage-1 leaves it
 * off; touching disp_blk_ctrl@0x32e28000 before it is powered = synchronous
 * abort (far 0x32e28004, esr 0x96000006). Sequence per u-boot
 * drivers/power/domain/imx8m-power-domain.c (DISPMIX = PGC 26).
 */
#define GPC_BASE_ADDR        0x303a0000
#define GPC_PGC_CPU_MAPPING  0x0ec
#define GPC_PU_PGC_SW_PUP    0x0f8
#define GPC_PGC_CTRL_DISPMIX (0x800 + 26 * 0x40)	/* PGC 26 */
#define DISPMIX_A53_DOMAIN   (1u << 12)
#define DISPMIX_SW_PXX_REQ   (1u << 10)
#define GPC_PGC_CTRL_PCR     (1u << 0)
/* NXP imx8mm clock_imx8mm.c — sets DISPLAY_AXI/APB + MIPI_DSI clock roots
 * and clock_enable(CCGR_DISPMIX,true). Normally called during clock init;
 * the chainload path skips it, so disp_blk_ctrl@0x32e28000 has no bus
 * clock and faults. Call it explicitly before powering the GPC domain. */
extern void enable_display_clk(unsigned char enable);

static void poly_power_up_dispmix(void)
{
	void __iomem *g = (void __iomem *)GPC_BASE_ADDR;
	int t = 10000;

	/* THE missing piece: disp-mix bus/pixel/DSI clocks + CCGR_DISPMIX */
	enable_display_clk(1);

	setbits_le32(g + GPC_PGC_CPU_MAPPING, DISPMIX_A53_DOMAIN);
	setbits_le32(g + GPC_PU_PGC_SW_PUP, DISPMIX_SW_PXX_REQ);
	while ((readl(g + GPC_PU_PGC_SW_PUP) & DISPMIX_SW_PXX_REQ) && --t)
		udelay(1);
	clrbits_le32(g + GPC_PGC_CTRL_DISPMIX, GPC_PGC_CTRL_PCR);
	udelay(10);
	printf("dispmix: GPC power-up %s\n", t ? "ok" : "TIMEOUT");
}

/* rom_pointer[] is filled by save_boot_params() from the boot-arg regs.
 * In a NO-SPL stage-2 chainloaded via stock u-boot `go`, those regs hold
 * garbage -> rom_pointer[1] (the "OPTEE size") is junk-nonzero. NXP
 * imx8m soc.c then does, in BOTH dram_init() and dram_init_banksize():
 *     gd->ram_size = PHYS_SDRAM_SIZE - rom_pointer[1]
 * which underflows to "16 EiB" -> relocation/bank sizing wedges -> stage-2
 * hangs silently right after the "DRAM:" line. There is no real OPTEE
 * carveout in the chainload path. board_phys_sdram_size() is invoked at
 * the TOP of both functions, before their rom_pointer[1] checks, so
 * neutralising rom_pointer[] here fixes both. TC8 = fixed 2 GiB LPDDR4. */
extern unsigned long rom_pointer[];
int board_phys_sdram_size(phys_size_t *size)
{
	rom_pointer[0] = 0;
	rom_pointer[1] = 0;
	*size = PHYS_SDRAM_SIZE;
	return 0;
}

int board_init(void)
{
#ifdef CONFIG_USB_TCPC
	setup_typec();
#endif

	if (IS_ENABLED(CONFIG_FEC_MXC))
		setup_fec();

	if (poly_is_tc8())
		poly_power_up_dispmix();

	return 0;
}

/*
 * TC8 stage-2 chainload: env-load device remap.
 *
 * NXP's mach-imx/mmc_env.c::mmc_get_env_dev() reads boot_dev_instance
 * from the BootROM SW info block — but on TC8 we are NOT booting from
 * BootROM, we are chainloaded by stock u-boot 2018.03 via `go`. Stock
 * SPL filled boot_dev_instance with its OWN MMC numbering, in which
 * the eMMC user area is mmc 2. Stage-2 (NXP 2024.04) enumerates only
 * a single FSL_SDHC controller as mmc 0 — so find_mmc_device(2)
 * returns NULL and env load prints "MMC Device 2 not found", falling
 * back to compiled defaults (which lack the rotation + console=tty0 +
 * fw_devlink=permissive bits that onboard.sh persists into env).
 *
 * Remap devno 2 -> 0 so stage-2 reads the SAME env partition stock
 * already populated. CONFIG_ENV_OFFSET (0x700000) lands at the right
 * physical offset on the eMMC user area regardless of which numerical
 * device-id we use, as long as we end up pointing at the user area
 * itself (which mmc 0 in stage-2 IS).
 */
int board_mmc_get_env_dev(int devno)
{
	return devno == 2 ? 0 : devno;
}

/*
 * Factory MAC adoption. Stage-2 keeps its env at 0x700000 and ships with
 * CONFIG_NET_RANDOM_ETHADDR; with no `ethaddr` saved there, every boot
 * invented a fresh random MAC which fdt_fixup_ethernet() then wrote into
 * the DTB `local-mac-address` — the panel changed identity on every boot.
 *
 * The FACTORY MAC still lives in the STOCK 2018.03 u-boot env block at raw
 * eMMC byte offset 0x400000 (unpartitioned gap between the signed stock
 * bootloader region and the first GPT partition; 4-byte CRC header, then
 * NUL-separated key=value pairs): `ethaddr=<factory>` (Polycom OUI
 * 00:e0:db, matches the device-cert CN in the `cert` partition).
 *
 * Adopt exactly that ONE variable — do NOT env-import the whole block: its
 * `bootcmd` is the boot1 chainload (importing it would make stage-2
 * re-chainload in a loop), and nothing may ever saveenv over shared stock
 * state. Precedence: a MAC saved in stage-2's own env (0x700000) wins; the
 * stock factory value is the everyday default; the NET_RANDOM_ETHADDR
 * fallback only fires on units whose stock env carries no ethaddr (and the
 * rootfs tc8-hwaddr.sh then pins a SoC-UID-derived address anyway).
 */
#define TC8_STOCK_ENV_LBA	(0x400000 / 512)	/* stock env @ 4 MiB */
#define TC8_STOCK_ENV_BLKS	32			/* scan 16 KiB */

static void tc8_adopt_factory_ethaddr(void)
{
	ALLOC_CACHE_ALIGN_BUFFER(u8, buf, TC8_STOCK_ENV_BLKS * 512);
	struct mmc *mmc;
	u8 mac[6];
	int i;

	if (env_get("ethaddr"))
		return;

	mmc = find_mmc_device(0);		/* eMMC user area */
	if (!mmc || mmc_init(mmc))
		return;
	if (blk_dread(mmc_get_blk_desc(mmc), TC8_STOCK_ENV_LBA,
		      TC8_STOCK_ENV_BLKS, buf) != TC8_STOCK_ENV_BLKS)
		return;

	/* 4-byte CRC header, then a NUL-separated key=value pool */
	for (i = 4; i < TC8_STOCK_ENV_BLKS * 512 - 26; i++) {
		if (i > 4 && buf[i - 1] != '\0')
			continue;	/* not at a key start */
		if (memcmp(&buf[i], "ethaddr=", 8))
			continue;
		buf[i + 25] = '\0';	/* bound the value for the parser */
		string_to_enetaddr((char *)&buf[i + 8], mac);
		if (is_valid_ethaddr(mac)) {
			eth_env_set_enetaddr("ethaddr", mac);
			printf("Net:   factory ethaddr %pM (stock env @0x400000)\n",
			       mac);
		}
		return;
	}
}

int board_late_init(void)
{
#ifdef CONFIG_ENV_IS_IN_MMC
	board_late_mmc_env_init();
#endif

	tc8_adopt_factory_ethaddr();

	if (IS_ENABLED(CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG)) {
		env_set("board_name", "EVK");
		env_set("board_rev", "iMX8MM");
	}

	/*
	 * TC8 unlock F1+F2: bootcmd runs `gesture_sel` first. Default it to
	 * `bootsel` — the integrated boot-UX (U-Boot logo -> 4 s GT9271
	 * window -> mode icon -> action). Falls back to serial-only
	 * `gesture` if the panel isn't up. User-overridable in env; the
	 * 5-finger SDP hard-escape works whenever `bootsel`/`gesture` runs.
	 */
	if (!env_get("gesture_sel"))
		env_set("gesture_sel", "bootsel");

	/*
	 * imx8mm_evk_android.h does `#undef CONFIG_BOOTCOMMAND` when
	 * ANDROID_SUPPORT is on, so the Android default ("boota mmc0") would own
	 * bootcmd and skip our gesture+logo UX entirely. Force our boot flow here
	 * UNGUARDED (the Android default already occupies bootcmd, so a
	 * !env_get() guard would never fire). mmcboot itself is `boota`, so we
	 * still boot via the Android path — just after the gesture selector +
	 * logo + netboot, with the fastboot-provisioning fallback.
	 */
	env_set("bootcmd",
		"run gesture_sel;osprep;run dhcp66_boot;run mmcboot;"
		"echo '*** no bootable OS - entering fastboot for provisioning ***';"
		"fastboot usb 0");

	/*
	 * TC8 unlock F3: DHCP-66 netboot. bootcmd runs `dhcp66_boot` after
	 * the gesture selector. If a DHCP server hands out opt-66 (TFTP
	 * server, -> ${serverip}) + opt-67 (bootfile), TFTP that FIT image
	 * and bootm it; otherwise fall through to local distro/eMMC. The
	 * 4-finger gesture sets skip_net=1, which short-circuits this.
	 * User-overridable; defaulted here (no Android env header in the
	 * unlock build).
	 */
	if (!env_get("dhcp66_boot"))
		env_set("dhcp66_boot",
			"if test \"${skip_net}\" = 1; then "
			"echo 'dhcp66: skipped (gesture/4-finger)'; "
			"else setenv autoload no; "
			"if dhcp; then "
			"if test -n \"${bootfile}\"; then "
			"echo \"dhcp66: TFTP ${serverip}:${bootfile}\"; "
			"if tftpboot ${loadaddr} ${bootfile}; then "
			"bootm ${loadaddr}; fi; "
			"fi; fi; fi");

	/*
	 * TC8 unlock F2: boot splash. NXP imx convention — a BMP preloaded
	 * at ${splashimage} is auto-shown centered (${splashpos}) at video
	 * init (CONFIG_SPLASH_SCREEN/_ALIGN). Asset: targets/tc8-chainload-uboot/
	 * splash.bmp (Colloid start-here, 360x360 24bpp). The load of that
	 * BMP -> 0x50000000 is finalized on-device at #8 (panel is dark until
	 * the DSIM PMS lock is proven — UNLOCK_SPEC.md §7b/§8).
	 */
	if (!env_get("splashpos"))
		env_set("splashpos", "m,m");
	if (!env_get("splashimage"))
		env_set("splashimage", "0x50000000");

	/*
	 * Stage-2-always-in-charge: on no gesture, bootsel falls through
	 * `bootcmd = run gesture_sel; osprep; run dhcp66_boot; run mmcboot`
	 * to `mmcboot` — the deterministic LOCAL OS boot. `mmcboot` is now
	 * `boota`: it boots the active slot's Android image (boot_<slot> +
	 * dtbo_<slot> + vbmeta_<slot>) via the unlocked NXP boota path. eMMC
	 * is `mmc dev 0` in stage-2 (DTS alias mmc0=usdhc3). tc8_bootargs
	 * below is the fallback cmdline for the dev-path bootm/booti only.
	 */
	/*
	 * Default must match profiles/emmc.env KERNEL_CMDLINE so the
	 * panel renders correctly (rotate=270 + fbcon=rotate:3 + cage -r
	 * once = transform 1 = WL_OUTPUT_TRANSFORM_90 — see tc8-kernel-patches
	 * 0001/0002/0006 + tc8-rootfs kiosk.service). Stage-2's CONFIG_ENV_OFFSET
	 * (0x700000) differs from stock 2018.03's env at 0x400000, so
	 * fw_setenv from Linux can't reliably reach stage-2's env block;
	 * keep the working cmdline in defaults so we don't depend on
	 * saved env state.
	 */
	if (!env_get("tc8_bootargs"))
		env_set("tc8_bootargs",
			"console=ttymxc1,115200 console=tty0 "
			"earlycon=ec_imx6q,0x30890000,115200 "
			"keep_bootcon panic=10 rw rootwait "
			"fw_devlink=permissive "
			"video=DSI-1:rotate=270 fbcon=rotate:3 "
			"systemd.show_status=true "
			"vt.global_cursor_default=0 "
			/* KEEP stock GPT, rootfs lives in the stock `userdata`
			 * partition. PARTLABEL is robust vs. p-number
			 * (stock GPT has ~16 entries). If PARTLABEL fails on-device,
			 * fall back to the explicit /dev/mmcblk2pN for userdata. */
			"root=PARTLABEL=userdata");
	/*
	 * FORCE-override mmcboot UNCONDITIONALLY (no !env_get guard).
	 * Stock imx8mm_evk's built-in default env ALREADY defines a
	 * distro `mmcboot` (echo Booting from mmc; run mmcargs; run
	 * loadfdt; booti ... ${fdt_addr_r}) that targets `mmc 2` (the
	 * EVK's removable SD) — which does not exist on TC8 (eMMC is
	 * `mmc dev 0` in stage-2). With a !env_get guard our override
	 * was SKIPPED (stock value present), so autonomous `run bootcmd`
	 * ran stock mmcboot -> "** Bad device specification mmc 2 **" ->
	 * "WARN: Cannot load the DT" -> drop to u-boot=> (never booted).
	 * Deterministically root-caused via diag7 printenv. gesture_sel/
	 * dhcp66_boot/tc8_bootargs don't collide (stock doesn't define
	 * them) so they keep the !env_get guard; only mmcboot must be
	 * forced.
	 */
	/*
	 * UNIFIED ANDROID BOOT (2026-06-27): boot via NXP `boota` — the
	 * established Android path, run UNLOCKED (fastboot_get_lock_stat stub
	 * -> FASTBOOT_UNLOCK) so it boots UNSIGNED images. `boota` reads the
	 * active slot from the `misc` bootctrl, loads boot_<slot> + the DTB from
	 * dtbo_<slot> (Android DTBO container) + vbmeta_<slot>, AVB-checks
	 * (tolerant when unlocked), builds the cmdline, and booti's. Same method
	 * boots stock Android (slot A) and our Linux packaged as a slot image
	 * (slot B: boot.img + dtbo + vbmeta). Switch with `fastboot set_active`.
	 * On failure boota drops to fastboot itself; bootcmd's trailing
	 * `fastboot usb 0` is the belt-and-suspenders fallback.
	 */
	env_set("mmcboot", "boota");

	return 0;
}

/*
 * --- TC8 F1: Goodix GT9271 gesture boot-selector --------------------------
 * Polls the GT9271 status register 0x814E (bit7 = data-ready, low nibble =
 * touch-point count) over I2C. The stock TC8 wires it on I2C2 @ 0x5d, but
 * GT9-series answers at 0x14 if INT was held at reset, and the u-boot DM
 * i2c bus seq is enumeration-order dependent — so sweep busnum 0..3 x
 * {0x5d,0x14} and use whichever responds. Debounced over a ~180 ms window
 * (max count seen — fingers land staggered). See UNLOCK_SPEC.md §7a.
 *
 *   5 fingers -> hard SDP/UUU re-entry (env-independent anti-brick escape)
 *   4 fingers -> fastboot gadget (`fastboot usb 0`) — the web provisioner's entry
 *   3 fingers -> DHCP-66 netboot
 *   else      -> normal bootcmd chain
 */
#define GT9271_STATUS_REG	0x814e
#define GESTURE_SAMPLES		18
#define GESTURE_SAMPLE_MS	10

/*
 * GT9271 power-on reset: RESET = GPIO2_IO3 (SD1_DATA1 / ALT5, active-low),
 * INT = GPIO1_IO9. Goodix GT9xx sequence: hold RESET low with INT low
 * (INT level at reset-release latches the I2C addr -> low = 0x5d, which is
 * what the controller already answers), release RESET, brief hold, release
 * INT to input, then wait for firmware. WITHOUT this the controller ACKs
 * I2C but its firmware never scans -> status 0x814E stays 0 -> gestures
 * never register (observed: 3 cycles, user did 3 fingers, count == 0).
 */
#define GT9_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_PUE | PAD_CTL_PE)
#define GT9_RST_GPIO	IMX_GPIO_NR(2, 3)
#define GT9_INT_GPIO	IMX_GPIO_NR(1, 9)

static iomux_v3_cfg_t const gt9_pads[] = {
	IMX8MM_PAD_SD1_DATA1_GPIO2_IO3 | MUX_PAD_CTRL(GT9_PAD_CTRL),
	IMX8MM_PAD_GPIO1_IO09_GPIO1_IO9 | MUX_PAD_CTRL(GT9_PAD_CTRL),
};

/*
 * int_high selects the latched I2C address via the INT level at reset-release:
 *   1 -> 0x14  (what bootsel polls during the gesture window)
 *   0 -> 0x5d  (the GT9271 power-on default; what the Linux DT expects, and the
 *               DT has NO reset-gpios so the OS can't re-address it). osprep
 *               re-latches 0x5d at OS handoff so touch works in the booted OS.
 */
static void gt9271_reset(int int_high)
{
	imx_iomux_v3_setup_multiple_pads(gt9_pads, ARRAY_SIZE(gt9_pads));
	gpio_request(GT9_RST_GPIO, "gt9_rst");
	gpio_request(GT9_INT_GPIO, "gt9_int");
	/*
	 * Canonical Goodix GTP V2.8.0.2 gtp_reset_guitar() + gtp_int_sync()
	 * (stock is the GTP vendor driver w/ goodix,int-sync=<1>). Without
	 * the int-sync step the GT9xx never starts producing coordinate
	 * frames -> 0x814E buffer-status never sets -> "no gesture".
	 */
	gpio_direction_output(GT9_RST_GPIO, 0);	/* assert reset (active-low) */
	mdelay(20);				/* hold in reset (GTP: 20 ms) */
	gpio_direction_output(GT9_INT_GPIO, int_high);	/* INT @reset latches addr:
						 * HIGH(1)=0x14, LOW(0)=0x5d */
	udelay(2000);				/* GTP: 2 ms before reset release */
	gpio_set_value(GT9_RST_GPIO, 1);	/* deassert reset (addr latched) */
	mdelay(6);				/* GTP: 6 ms */
	gpio_direction_input(GT9_RST_GPIO);	/* RST -> input (GTP) */
	/* gtp_int_sync(50): INT low, 50 ms, INT input — starts the scan */
	gpio_direction_output(GT9_INT_GPIO, 0);
	mdelay(50);
	gpio_direction_input(GT9_INT_GPIO);	/* INT -> input (IRQ line) */
	mdelay(50);				/* firmware settle + first scan */
}

/*
 * Stock lcc.dtb gt9xx@14 has goodix,driver-send-cfg=<1> + this exact
 * 186-byte goodix,cfg-group2 blob. With driver-send-cfg the controller
 * will NOT scan/report touches until the host writes this config to the
 * GT9xx config register (0x8047). Verbatim stock blob -> Goodix-generated
 * checksum (byte[-2]=0x09) + config_fresh (byte[-1]=0x01) already valid.
 */
#define GT9271_CFG_REG	0x8047
static const u8 gt9271_cfg[] = {
	0x50, 0x20, 0x03, 0x00, 0x05, 0x0a, 0x3d, 0x00, 0x01, 0xca, 0x28, 0x0a,
	0x5a, 0x3c, 0x0a, 0x25, 0x00, 0x00, 0x2f, 0x5a, 0x00, 0x00, 0x03, 0x18,
	0x1b, 0x1e, 0x14, 0x90, 0x30, 0xaa, 0x19, 0x1b, 0xd9, 0x0b, 0x00, 0x00,
	0x02, 0x21, 0x34, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x13, 0x19, 0x41, 0x94, 0xc0, 0x02, 0x00, 0x00, 0x00, 0x04,
	0x9a, 0x1b, 0x00, 0x85, 0x21, 0x00, 0x74, 0x28, 0x00, 0x66, 0x31, 0x00,
	0x5b, 0x3b, 0x00, 0x5b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x78, 0x33, 0x45, 0x00, 0x00, 0x88, 0x00, 0x00, 0x21, 0x02,
	0x00, 0x75, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x25, 0x24,
	0x23, 0x22, 0x21, 0x20, 0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x19, 0x18, 0x17,
	0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e, 0x0d, 0x0c, 0x0b,
	0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0x09, 0x01,
};

/* Find the GT9271 (post-reset addr 0x14) and push the stock config once. */
static void gt9271_send_cfg(void)
{
	const int addrs[] = { 0x14, 0x5d };
	struct udevice *dev;
	int busnum, ai;

	for (busnum = 0; busnum < 4; busnum++) {
		for (ai = 0; ai < 2; ai++) {
			u8 probe;

			if (i2c_get_chip_for_busnum(busnum, addrs[ai], 2, &dev))
				continue;
			if (dm_i2c_read(dev, GT9271_STATUS_REG, &probe, 1))
				continue;	/* not the controller */
			if (dm_i2c_write(dev, GT9271_CFG_REG, gt9271_cfg,
					 sizeof(gt9271_cfg))) {
				printf("gesture: GT9271 cfg write FAILED "
				       "(i2c%d@0x%02x)\n", busnum, addrs[ai]);
				return;
			}
			printf("gesture: GT9271 cfg sent (i2c%d@0x%02x, %d B)\n",
			       busnum, addrs[ai], (int)sizeof(gt9271_cfg));
			mdelay(150);		/* controller applies config */
			return;
		}
	}
	printf("gesture: GT9271 not found for cfg push\n");
}

static int gt9271_finger_count(void)
{
	const int addrs[] = { 0x14, 0x5d };	/* stock reg=<0x14> first */
	struct udevice *dev;
	int busnum, ai, i, best = -1;

	for (busnum = 0; busnum < 4; busnum++) {
		for (ai = 0; ai < 2; ai++) {
			if (i2c_get_chip_for_busnum(busnum, addrs[ai], 2, &dev))
				continue;
			/* device present on this bus/addr — sample it */
			for (i = 0; i < GESTURE_SAMPLES; i++) {
				u8 st = 0;

				if (dm_i2c_read(dev, GT9271_STATUS_REG, &st, 1) == 0) {
					if (st & 0x80) {
						int n = st & 0x0f;

						if (n > best)
							best = n;
						/* ack so the controller refreshes */
						st = 0;
						dm_i2c_write(dev, GT9271_STATUS_REG, &st, 1);
					} else if (best < 0) {
						best = 0; /* responding, no touch yet */
					}
				}
				mdelay(GESTURE_SAMPLE_MS);
			}
			if (best >= 0) {
				/* announce the controller once, not on every poll
				 * (bootsel calls this ~100x across its window). */
				static bool gt_announced;
				if (!gt_announced) {
					printf("gesture: GT9271 on i2c%d@0x%02x\n",
					       busnum, addrs[ai]);
					gt_announced = true;
				}
				return best;
			}
		}
	}
	return -1;
}

static int do_gesture(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	int n;

	gt9271_reset(1);		/* bring up @0x14 for gesture polling */
	gt9271_send_cfg();		/* push stock cfg -> start scanning */
	n = gt9271_finger_count();

	if (n < 0) {
		printf("gesture: GT9271 not found — normal boot\n");
		env_set("skip_net", NULL);
		return 0;
	}

	printf("gesture: %d finger(s) -> ", n);

	if (n >= 5) {
		printf("SDP/UUU (hard escape)\n");
		/* env-independent: enter u-boot SDP gadget; uuu drives this. */
		run_command("sdp 0", 0);
		return 0;
	}
	if (n == 4) {
		printf("fastboot (web provisioner)\n");
		/* Path B: launch the fastboot gadget the WebUSB tool drives.
		 * Blocks until the host sends `fastboot reboot`. */
		run_command("fastboot usb 0", 0);
		return 0;
	}
	printf("normal bootcmd\n");
	env_set("skip_net", NULL);
	return 0;
}

U_BOOT_CMD(gesture, 1, 0, do_gesture,
	   "TC8 GT9271 touch boot-selector (5=SDP, 4=fastboot, else normal)",
	   "");

/*
 * --- F2 panel visual test: R -> G -> B -> colorbars -> checkerboard ------
 * Run once the video uclass is up (mxsfb/LCDIF -> sec-dsi -> lcc-proto
 * panel). Fills the framebuffer + video_sync(). Strong visual cue + proves
 * the LCDIF->DSIM->panel pixel path. xRGB8888 (BPP32) and RGB565 (BPP16).
 */
static inline u32 vt_to565(u32 c)
{
	return ((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f);
}

static void vt_fill(struct video_priv *p, u32 c)
{
	int n = p->xsize * p->ysize, k;

	if (p->bpix == VIDEO_BPP32)
		for (k = 0; k < n; k++)
			((u32 *)p->fb)[k] = c;
	else if (p->bpix == VIDEO_BPP16)
		for (k = 0; k < n; k++)
			((u16 *)p->fb)[k] = vt_to565(c);
}

static void vt_put(struct video_priv *p, int x, int y, u32 c)
{
	if (p->bpix == VIDEO_BPP32)
		((u32 *)p->fb)[y * p->xsize + x] = c;
	else if (p->bpix == VIDEO_BPP16)
		((u16 *)p->fb)[y * p->xsize + x] = vt_to565(c);
}

static int do_vtest(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	static const u32 rgb[3] = { 0x00ff0000, 0x0000ff00, 0x000000ff };
	static const char * const nm[3] = { "RED", "GREEN", "BLUE" };
	static const u32 bar[8] = { 0x000000, 0x0000ff, 0x00ff00, 0x00ffff,
				    0xff0000, 0xff00ff, 0xffff00, 0xffffff };
	struct udevice *vid;
	struct video_priv *p;
	int x, y, i, loops = 1;

	if (uclass_first_device_err(UCLASS_VIDEO, &vid)) {
		printf("vtest: no video device (display not up)\n");
		return CMD_RET_FAILURE;
	}
	p = dev_get_uclass_priv(vid);
	if (argc > 1)
		loops = simple_strtoul(argv[1], NULL, 10);
	printf("vtest: %dx%d bpix=%d fb=%p sz=%d, %d loop(s)\n",
	       p->xsize, p->ysize, p->bpix, p->fb, p->fb_size, loops);

	while (loops-- > 0) {
		for (i = 0; i < 3; i++) {
			vt_fill(p, rgb[i]);
			video_sync(vid, true);
			printf("vtest: %s\n", nm[i]);
			mdelay(1500);
		}
		for (y = 0; y < p->ysize; y++)
			for (x = 0; x < p->xsize; x++)
				vt_put(p, x, y, bar[(x * 8) / p->xsize]);
		video_sync(vid, true);
		printf("vtest: colorbars\n");
		mdelay(2500);
		for (y = 0; y < p->ysize; y++)
			for (x = 0; x < p->xsize; x++)
				vt_put(p, x, y,
				       (((x >> 5) + (y >> 5)) & 1) ?
				       0xffffff : 0x000000);
		video_sync(vid, true);
		printf("vtest: checkerboard\n");
		mdelay(2500);
	}
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(vtest, 2, 0, do_vtest,
	   "F2 panel test: R/G/B + colorbars + checkerboard [loops]",
	   "[loops]");

/*
 * --- OS handoff prep: quiesce the panel + reset touch before booting -------
 * Called from bootcmd right before the OS boots. (1) Clear the panel to black
 * so the OS doesn't inherit the bootsel logo / show garbage while its DRM
 * driver re-inits. (2) Reset the GT9271 to a clean post-power-on state (addr
 * 0x14) so the OS touch driver starts from a known reset rather than our
 * scanning cfg (fixes intermittent touch in the booted OS).
 */
static int do_osprep(struct cmd_tbl *cmdtp, int flag, int argc,
		     char *const argv[])
{
	struct udevice *vid;

	if (!uclass_first_device_err(UCLASS_VIDEO, &vid)) {
		struct video_priv *p = dev_get_uclass_priv(vid);

		vt_fill(p, 0x000000);		/* paint a black frame... */
		video_sync(vid, true);
		/*
		 * ...then STOP the eLCDIF scan-out (HW_LCDIF_CTRL bit0 RUN, cleared
		 * atomically via HW_LCDIF_CTRL_CLR @ base+0x08). The kernel overwrites
		 * the framebuffer DRAM during boot; if the LCDIF keeps scanning that
		 * region the panel shows garbage until the DRM driver re-inits. Freezing
		 * on the black frame above eliminates the u-boot->Linux transition garble.
		 */
		writel(BIT(0), (void __iomem *)(ulong)(0x32e00000 + 0x08));
	}
	/*
	 * Re-latch the GT9271 to 0x5d — its power-on default and the address the
	 * Linux DT (touchscreen@5d, NO reset-gpios) expects. bootsel latched 0x14
	 * for gesture polling; without this the OS would talk to an empty 0x5d and
	 * touch would be dead (worked before stage-2 = chip was still at 0x5d).
	 */
	gt9271_reset(0);
	return 0;
}
U_BOOT_CMD(osprep, 1, 0, do_osprep,
	   "TC8: quiesce panel + reset GT9271 touch before OS handoff", "");

/*
 * --- F1+F2 boot-UX: `bootsel` ---------------------------------------------
 * Panel up -> black -> U-Boot logo (submarine) centered -> poll GT9271 for
 * BOOTSEL_WIN_MS -> on a finger-mode, swap to that mode's icon centered,
 * hold briefly, then perform the action. No gesture -> clear -> fall
 * through to the normal bootcmd chain.
 *
 * SELF-CONTAINED LOGOS (2026-06-27): the 5 BMPs (256x256, 24bpp, ~192 KB each)
 * are EMBEDDED in the binary as const arrays (tc8_logos.h) and `bmp_display`d
 * straight from .rodata — NO eMMC blob, NO partition/slot dependency. This grows
 * the stage-2 binary by ~960 KB (~1 MB -> ~2 MB); the chainload `mmc read` length
 * + boot1 capacity must cover it (boot1 is typically 4 MiB).
 * The ONLY remaining eMMC bootsel touch is the 1-sector "last selection" sticky
 * state @ LBA 0x4C00 (magic "BSEL" + mode byte). If full zero-eMMC self-
 * containment is wanted, move that to the stage-2 env or drop the sticky default.
 * NB: stage-2 `u-boot.bin` itself lives in the eMMC boot1 HW partition.
 */
#define BOOTSEL_WIN_MS		3000	/* gesture window at the logo (prod:
					 * 3 s — was 20 s bench value). Env
					 * `bootsel_win_ms` overrides without
					 * a rebuild. */
#define BOOTSEL_HOLD_MS		1500	/* how long the mode icon shows */
/* Bootsel logos are EMBEDDED in the binary (self-contained — no eMMC blob,
 * no partition/slot dependency). Arrays: ublogo_bmp / m2_emmc_bmp / m3_net_bmp
 * / m4_fastboot_bmp / m5_sdp_bmp. ~192 KB each, 256x256 24bpp. */
#include "tc8_logos.h"

/* Persistent last-selection ("sticky default") — 1 sector @ LBA 0x4C00
 * (within dtbo_a, just past the BMP blob; see layout note above). */
#define BSEL_STATE_LBA		0x4c00
#define BSEL_SCRATCH		0x50200000UL
#define BSEL_RD	"mmc dev 0; mmc read 0x50200000 0x4c00 1"
#define BSEL_WR	"mmc dev 0; mmc write 0x50200000 0x4c00 1"

/* return last sticky mode (2=emmc | 3=net) or 0 if unset/invalid */
static int bsel_load(void)
{
	volatile u8 *b = (volatile u8 *)BSEL_SCRATCH;

	if (run_command(BSEL_RD, 0))
		return 0;
	if (b[0] != 'B' || b[1] != 'S' || b[2] != 'E' || b[3] != 'L')
		return 0;
	return (b[4] == 2 || b[4] == 3) ? b[4] : 0;
}

/* persist a sticky boot mode (only 2=emmc / 3=net are made default) */
static void bsel_save(int mode)
{
	volatile u8 *b = (volatile u8 *)BSEL_SCRATCH;
	int i;

	for (i = 0; i < 512; i++)
		b[i] = 0;
	b[0] = 'B'; b[1] = 'S'; b[2] = 'E'; b[3] = 'L'; b[4] = (u8)mode;
	if (run_command(BSEL_WR, 0))
		printf("bootsel: WARN could not persist last selection\n");
}

static void bootsel_show(struct video_priv *p, struct udevice *vid,
			 const unsigned char *bmp)
{
	int x = ((int)p->xsize - 256) / 2;
	int y = ((int)p->ysize - 256) / 2;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	vt_fill(p, 0x000000);			/* black background */
	video_sync(vid, true);
	/*
	 * Logos are EMBEDDED in the binary (self-contained, no eMMC blob/slot).
	 * bmp_display reads the BMP straight from its .rodata address — no
	 * `mmc read`, no dependency on any partition.
	 */
	if (bmp_display((ulong)bmp, x, y))
		printf("bootsel: bmp_display failed\n");
	video_sync(vid, true);
}

static int do_bootsel(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	struct udevice *vid;
	struct video_priv *p;
	ulong t0, win_ms;
	int n = -1;

	/* No panel? degrade gracefully to the serial-only gesture path. */
	if (uclass_first_device_err(UCLASS_VIDEO, &vid)) {
		printf("bootsel: no video device — gesture-only\n");
		return do_gesture(cmdtp, flag, argc, argv);
	}
	p = dev_get_uclass_priv(vid);

	win_ms = env_get_ulong("bootsel_win_ms", 10, BOOTSEL_WIN_MS);

	bootsel_show(p, vid, ublogo_bmp);
	gt9271_reset(1);			/* once: bring up @0x14 for polling */
	gt9271_send_cfg();			/* once: stock cfg -> scanning */
	printf("bootsel: U-Boot logo; %lu ms gesture window "
	       "(2=eMMC 3=net 4=fastboot 5=SDP)\n", win_ms);

	t0 = get_timer(0);
	while (get_timer(t0) < win_ms) {
		int c;

		schedule();			/* service the i.MX WDOG —
						 * stock/ATF armed it; this
						 * busy poll-loop must pet it
						 * or the SoC resets mid-window
						 * and re-chainloads forever */
		c = gt9271_finger_count();	/* ~180 ms+ per call */

		if (c >= 2) {			/* 2/3/4/5 are the modes */
			/*
			 * Don't commit instantly — fingers land staggered
			 * (a 5-finger gesture passes through 2,3,4 first).
			 * Settle ~2 s and take the MAX count so the intended
			 * gesture fully registers before we act.
			 */
			ulong s0 = get_timer(0);

			n = c;
			printf("bootsel: gesture detected (%d) — "
			       "settling 2 s...\n", c);
			while (get_timer(s0) < 2000) {
				int c2;

				schedule();	/* pet WDOG in the settle too */
				c2 = gt9271_finger_count();
				if (c2 > n)
					n = c2;
			}
			printf("bootsel: settled on %d finger(s)\n", n);
			break;
		}
		if (ctrlc())			/* let console abort to prompt */
			break;
	}

	/* Maintenance modes (4/5): one-shot, NOT made the sticky default
	 * (a sticky fastboot/SDP would loop the device into a maintenance state
	 * and look bricked). They block until the host is done. */
	if (n >= 5) {
		printf("bootsel: 5 fingers -> SDP/UUU (hard escape)\n");
		bootsel_show(p, vid, m5_sdp_bmp);
		mdelay(BOOTSEL_HOLD_MS);
		run_command("sdp 0", 0);	/* uuu drives this; blocks */
		return 0;
	}
	if (n == 4) {
		/* 4 fingers -> fastboot gadget = the web provisioner's entry
		 * (Path B). Repurposed from UMS 2026-06-27: the WebUSB tool
		 * speaks fastboot, not mass-storage. `fastboot usb 0` blocks
		 * serving the gadget until the host sends `fastboot reboot`.
		 * (m4 BMP icon retained; now means "fastboot/provision".) */
		printf("bootsel: 4 fingers -> fastboot (web provisioner)\n");
		bootsel_show(p, vid, m4_fastboot_bmp);
		mdelay(BOOTSEL_HOLD_MS);
		run_command("fastboot usb 0", 0);	/* host provisions; blocks */
		return 0;
	}

	/* Boot modes (2/3): persisted as the new sticky default. */
	if (n == 3) {
		printf("bootsel: 3 fingers -> DHCP-66 netboot (sticky)\n");
		bootsel_show(p, vid, m3_net_bmp);
		mdelay(BOOTSEL_HOLD_MS);
		bsel_save(3);
		env_set("skip_net", NULL);	/* -> run dhcp66_boot */
		return 0;
	}
	if (n == 2) {
		printf("bootsel: 2 fingers -> eMMC boot (sticky)\n");
		bootsel_show(p, vid, m2_emmc_bmp);
		mdelay(BOOTSEL_HOLD_MS);
		bsel_save(2);
		env_set("skip_net", "1");	/* skip dhcp66 -> mmcboot */
		return 0;
	}

	/* No gesture: replay the last sticky selection (default = eMMC). */
	{
		int last = bsel_load();

		if (last == 3) {
			printf("bootsel: no gesture -> last=netboot\n");
			bootsel_show(p, vid, m3_net_bmp);
			env_set("skip_net", NULL);
		} else {
			printf("bootsel: no gesture -> last=eMMC (default)\n");
			bootsel_show(p, vid, m2_emmc_bmp);
			env_set("skip_net", "1");
		}
		mdelay(BOOTSEL_HOLD_MS);
	}
	return 0;
}

U_BOOT_CMD(bootsel, 1, 0, do_bootsel,
	   "TC8 boot-UX: logo + GT9271 gesture (3=net 4=eMMC 5=UUU) + mode icon",
	   "");

#ifdef CONFIG_ANDROID_SUPPORT
bool is_power_key_pressed(void) {
	return (bool)(!!(readl(SNVS_HPSR) & (0x1 << 6)));
}
#endif

#ifdef CONFIG_FSL_FASTBOOT
#ifdef CONFIG_ANDROID_RECOVERY
int is_recovery_key_pressing(void)
{
	return 0; /* TODO */
}
#endif /* CONFIG_ANDROID_RECOVERY */
#endif /* CONFIG_FSL_FASTBOOT */

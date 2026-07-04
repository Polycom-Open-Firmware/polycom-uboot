// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 */
#include <common.h>
#include <efi_loader.h>
#include <env.h>
#include <init.h>
#include <miiphy.h>
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
#include <command.h>
#include <dm.h>
#include <linux/delay.h>
#include <video.h>
#include <backlight.h>
#include <panel.h>
#include <mipi_dsi.h>
#include <pwm.h>
#include <cyclic.h>		/* schedule() — services the i.MX WDOG */

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
 * Power up the i.MX8MM DISPMIX GPC power domain EARLY, before the video uclass /
 * disp_blk_ctrl probe. On the C60 the panel came up DARK in u-boot: touching
 * disp_blk_ctrl@0x32e28000 (the LCDIF/DSIM block-ctrl) before the DISPMIX GPC
 * domain (PGC 26) is powered faults/stalls, so the video probe never completes
 * and the panel stays in its dark power-on state. The DM power-domain framework
 * did not bring it up on this boot path, so do it explicitly — same sequence the
 * proven TC8 stage-2 uses (u-boot drivers/power/domain/imx8m-power-domain.c).
 */
#define GPC_BASE_ADDR        0x303a0000
#define GPC_PGC_CPU_MAPPING  0x0ec
#define GPC_PU_PGC_SW_PUP    0x0f8
#define GPC_PGC_CTRL_DISPMIX (0x800 + 26 * 0x40)	/* PGC 26 */
#define DISPMIX_A53_DOMAIN   (1u << 12)
#define DISPMIX_SW_PXX_REQ   (1u << 10)
#define GPC_PGC_CTRL_PCR     (1u << 0)
extern void enable_display_clk(unsigned char enable);

static void c60_power_up_dispmix(void)
{
	void __iomem *g = (void __iomem *)GPC_BASE_ADDR;
	int t = 10000;

	enable_display_clk(1);	/* disp-mix bus/pixel/DSI clocks + CCGR_DISPMIX */

	setbits_le32(g + GPC_PGC_CPU_MAPPING, DISPMIX_A53_DOMAIN);
	setbits_le32(g + GPC_PU_PGC_SW_PUP, DISPMIX_SW_PXX_REQ);
	while ((readl(g + GPC_PU_PGC_SW_PUP) & DISPMIX_SW_PXX_REQ) && --t)
		udelay(1);
	clrbits_le32(g + GPC_PGC_CTRL_DISPMIX, GPC_PGC_CTRL_PCR);
	udelay(10);
	printf("c60: dispmix GPC power-up %s\n", t ? "ok" : "TIMEOUT");
}

int board_init(void)
{
#ifdef CONFIG_USB_TCPC
	setup_typec();
#endif

	if (IS_ENABLED(CONFIG_FEC_MXC))
		setup_fec();

#if IS_ENABLED(CONFIG_VIDEO)
	c60_power_up_dispmix();	/* before the video/panel probe in board_late_init */

	/*
	 * Deassert the TCA6408 GPIO-expander reset (gpio2_9 / SD1_DATA7,
	 * active-low). Linux holds this HIGH; the LED backlight boost is gated
	 * on the expander being out of reset. u-boot left it asserted, so the
	 * panel was fully driven (DSIM/LCDIF up, video streaming) but dark. This
	 * is the one display-relevant delta vs the backlight-lit Linux state.
	 */
	writel(0x5, (void __iomem *)0x303300c4);	/* SD1_DATA7 MUX -> GPIO2_IO9 */
	writel(0x16, (void __iomem *)0x3033032c);	/* SD1_DATA7 pad ctl */
	setbits_le32((void __iomem *)0x30210004, (1u << 9));	/* GPIO2 GDIR9 = out */
	setbits_le32((void __iomem *)0x30210000, (1u << 9));	/* GPIO2 DR9 = high */

	/*
	 * Enable BD71837 LDO6 + LDO7 (i2c1@0x4b regs 0x1d/0x1e) to match the
	 * backlight-lit Linux PMIC state. u-boot's SPL leaves them OFF
	 * (0x1d=0x03, 0x1e=0x00); Linux has them ON (0x43, 0x80). The DT has no
	 * backlight enable, so the LED-backlight rail is one of these PMIC LDOs
	 * the SPL doesn't bring up. REGLOCK (0x2f) bit0 gates the power-seq
	 * on/off, so unlock, write, relock.
	 */
	{
		struct udevice *bus, *pmic;

		if (!uclass_get_device_by_seq(UCLASS_I2C, 0, &bus) &&
		    !dm_i2c_probe(bus, 0x4b, 0, &pmic)) {
			dm_i2c_reg_write(pmic, 0x2f, 0x00);	/* REGLOCK: clear PWRSEQ+VREG */
			dm_i2c_reg_write(pmic, 0x1d, 0x43);	/* LDO6 (HW-seq, VRMON+volt) */
			dm_i2c_reg_write(pmic, 0x1e, 0x80);	/* LDO7 SEL=1 -> SW-enable */
			/*
			 * Leave REGLOCK UNLOCKED — exactly as the kernel bd718x7
			 * probe does (clears PWRSEQ|VREG, never re-locks). The old
			 * code re-locked PWRSEQ (0x2f=0x01), which made the PMIC
			 * re-assert its boot power-sequence; that sequence omits
			 * LDO7, so it cleared LDO7's SEL bit straight back to 0x00
			 * (verified via disp_pmic: 0x1e read 0x00 not 0x80),
			 * dropping the backlight-VIN rail. LDO7 is the SOLE PMIC
			 * delta vs the backlight-lit Linux state.
			 */
		}
	}
#endif

	return 0;
}

#if IS_ENABLED(CONFIG_VIDEO) && !defined(CONFIG_SPL_BUILD)
/*
 * PHASE A test: bring up the panel and paint three horizontal R/G/B bands so
 * a bench camera can confirm the LCDIF+DSIM+RM67191 pipeline is lit and the
 * colour order is correct (the point of the non-burst SYNC-PULSE init).
 * Probing UCLASS_VIDEO drives the whole chain: mxsfb -> imx_sec_dsim ->
 * dsi-host + the C60 panel (Table-B init) -> PWM4 backlight. Then hold the
 * frame (petting the watchdog) so it is stable for the camera. This is a
 * RAM-load test build — it deliberately does NOT boot on to Linux, so what
 * the camera captures is unambiguously u-boot's own frame; PoE-cycle to
 * recover. Phase B replaces this with the finger-boot icon menu.
 */
static void __maybe_unused c60_display_test(void)
{
	struct udevice *vid;
	struct video_priv *pr;
	unsigned int x, y, t1, t2;
	char buf[64];
	int ret;

	/*
	 * Enable LDO7 here, not in board_init: the PMIC i2c bus may not be up
	 * that early, so the board_init write silently no-op'd (disp_pmic still
	 * read 0x1e=0x00). Do it now (i2c is live — the later disp_pmic read
	 * works) and instrument REGLOCK + LDO7 before/after each write so we can
	 * see exactly what is gated. LDO7 is the SOLE PMIC delta vs lit-Linux.
	 */
	{
		struct udevice *bus, *pm;
		int lk0, lk1, l7b, l7a;

		if (!uclass_get_device_by_seq(UCLASS_I2C, 0, &bus) &&
		    !dm_i2c_probe(bus, 0x4b, 0, &pm)) {
			lk0 = dm_i2c_reg_read(pm, 0x2f);
			l7b = dm_i2c_reg_read(pm, 0x1e);
			dm_i2c_reg_write(pm, 0x2f, 0x00);	/* clear PWRSEQ+VREG */
			lk1 = dm_i2c_reg_read(pm, 0x2f);
			dm_i2c_reg_write(pm, 0x1e, 0x80);	/* LDO7 SEL=1 */
			l7a = dm_i2c_reg_read(pm, 0x1e);
			snprintf(buf, sizeof(buf), "lk %x-%x l7 %x-%x",
				 lk0, lk1, l7b, l7a);
			env_set("fastboot.disp_ldo7", buf);
		}
	}

	/*
	 * Diagnostics over USB: with no UART on this bench, publish the probe
	 * result as `fastboot.disp_*` env vars and drop into fastboot at the
	 * end. From the host: `fastboot getvar disp_ret|disp_dim|disp_bpix`.
	 * The board enumerating as fastboot (1fc9:0152) at all = the video
	 * probe did NOT hang; no enum = it hung (e.g. dispmix/DSIM reg stall).
	 */
	/*
	 * POWER RAILS (found 2026-07-03 via stock DTB + full PMIC diff): the
	 * PMIC is a BD71847 (stock DTS!), NOT the EVK's BD71837 — and under
	 * u-boot LDO1-4 are OFF (SPL never enables them) while Linux's PMIC
	 * probe turns them on right before its display bring-up. LDO3 is the
	 * 1.8V analog rail, LDO4 0.9V. Enable them with the lit-Linux values
	 * BEFORE the video/panel probe so the panel module powers up with
	 * its rails present. (Writes verified to stick; the BD71837-only
	 * regs 0x07/0x08/0x1E bounce on this silicon — regmap-cache mirage
	 * in Linux debugfs.)
	 */
	{
		struct udevice *bus, *pm;

		if (!uclass_get_device_by_seq(UCLASS_I2C, 0, &bus) &&
		    !dm_i2c_probe(bus, 0x4b, 0, &pm)) {
			int l1, l2, l3, l4;

			dm_i2c_reg_write(pm, 0x18, 0x62);	/* LDO1 EN */
			dm_i2c_reg_write(pm, 0x19, 0x60);	/* LDO2 EN */
			dm_i2c_reg_write(pm, 0x1a, 0x40);	/* LDO3 EN 1.8V */
			dm_i2c_reg_write(pm, 0x1b, 0x40);	/* LDO4 EN 0.9V */
			mdelay(20);
			l1 = dm_i2c_reg_read(pm, 0x18);
			l2 = dm_i2c_reg_read(pm, 0x19);
			l3 = dm_i2c_reg_read(pm, 0x1a);
			l4 = dm_i2c_reg_read(pm, 0x1b);
			snprintf(buf, sizeof(buf), "%02x,%02x,%02x,%02x",
				 l1, l2, l3, l4);
			env_set("fastboot.disp_ldo", buf);
			printf("c60: LDO1-4 enabled: %s\n", buf);
		}
	}

	/*
	 * DISPLAYMIX block-ctrl (disp_blk_ctrl @ 0x32e28000): u-boot's driver
	 * leaves the LCDIF->DSIM PIXEL-PATH resets (GPR0 bits 0-2) asserted and
	 * clocks (GPR4 bits 0-5) gated — lit-Linux has GPR0=0x67, GPR4=0x13ff.
	 * This was the LAST register block that still differed after eLCDIF
	 * timing/DOTCLK, the full DSIM config, the DSI PLL/bit-clock, and the
	 * panel init sequence were ALL matched to Linux, yet the panel stayed
	 * black-with-backlight-lit. Release them here BEFORE the video probe so
	 * the LCDIF/DSIM configure with the pixel datapath live.
	 */
	ret = uclass_first_device_err(UCLASS_VIDEO, &vid);
	snprintf(buf, sizeof(buf), "%d", ret);
	env_set("fastboot.disp_ret", buf);

	if (ret) {
		printf("c60: video probe failed (%d) - panel not up\n", ret);
		env_set("fastboot.disp_stage", "probe-fail");
		goto diag_fastboot;
	}
	pr = dev_get_uclass_priv(vid);
	snprintf(buf, sizeof(buf), "%ux%u", pr->xsize, pr->ysize);
	env_set("fastboot.disp_dim", buf);
	snprintf(buf, sizeof(buf), "%d", pr->bpix);
	env_set("fastboot.disp_bpix", buf);
	printf("c60: panel up %ux%u line=%u bpix=%d\n",
	       pr->xsize, pr->ysize, pr->line_length, pr->bpix);

	/*
	 * Flashlight-test pattern: high-contrast white-on-black STRUCTURE so a
	 * human shining a torch at the (backlight-off) panel can confirm the
	 * pixels are individually addressed, not just a uniform field.
	 *   - 8px white border  = full extent + all four edges driven
	 *   - centre crosshair  = centring / no half-panel dropout
	 *   - top-left solid 100x100 white square = orientation fiducial
	 *   - 16 alternating horizontal white/black bands = per-row "barcode"
	 * White transmits the torch, black blocks it, so the structure reads
	 * through a dead backlight.
	 */
	t1 = pr->xsize;
	t2 = pr->ysize;
	for (y = 0; y < pr->ysize; y++) {
		void *row = (u8 *)pr->fb + (size_t)y * pr->line_length;

		for (x = 0; x < pr->xsize; x++) {
			int border = (x < 8 || x >= (int)t1 - 8 ||
				      y < 8 || y >= (int)t2 - 8);
			int cross  = (x >= (int)t1 / 2 - 2 && x <= (int)t1 / 2 + 2) ||
				     (y >= (int)t2 / 2 - 2 && y <= (int)t2 / 2 + 2);
			int fid    = (x >= 16 && x < 116 && y >= 16 && y < 116);
			int band   = ((y * 16) / (int)t2) & 1;	/* alt rows */
			int on     = border || cross || fid || band == 0;
			u32 v      = on ? 0xff : 0x00;

			if (pr->bpix == VIDEO_BPP32)
				((u32 *)row)[x] = (v << 16) | (v << 8) | v;
			else if (pr->bpix == VIDEO_BPP16)
				((u16 *)row)[x] = ((v & 0xf8) << 8) |
						  ((v & 0xfc) << 3) |
						   (v >> 3);
		}
	}
	video_sync(vid, true);
	env_set("fastboot.disp_stage", "drawn");


	/*
	 * Backlight isolation: force the PWM4 pwm-backlight fully on, independent
	 * of the panel driver's own enable, to tell backlight-off from a
	 * DSIM/video fault. The panel reads BLACK (not white/grey) on camera —
	 * an LCD with backlight ON but no valid video reads bright, so black
	 * points at the backlight not driving. `disp_bl` reports whether the
	 * backlight device bound and enabled.
	 */
	{
		struct udevice *bl;
		int r = uclass_get_device(UCLASS_PANEL_BACKLIGHT, 0, &bl);

		if (!r) {
			int e = backlight_enable(bl);
			int b = backlight_set_brightness(bl, BACKLIGHT_MAX);

			snprintf(buf, sizeof(buf), "dev0 en=%d br=%d", e, b);
		} else {
			snprintf(buf, sizeof(buf), "nodev=%d", r);
		}
		env_set("fastboot.disp_bl", buf);
	}

	/*
	 * Hardware-state diagnostics (no UART, so read the regs and export them).
	 *   disp_pwm : IOMUXC SAI3_MCLK mux (want mux_mode=1=PWM4_OUT) + PWM4
	 *              PWMCR/PWMSAR(duty)/PWMPR(period) — is the backlight PWM
	 *              actually configured + enabled + non-zero duty?
	 *   disp_hw  : DSIM_STATUS (bit31 STATUS_PLLSTABLE = DSIM PLL locked;
	 *              bit10 TXREADYHSCLK), DSIM_PLLCTRL, and LCDIF CTRL (bit0
	 *              RUN = LCDIF scanning). Tells backlight-fault from
	 *              DSIM-PLL-not-locked / LCDIF-not-running.
	 */
	{
		u32 pmux = readl((void __iomem *)0x303301e4);	/* SAI3_MCLK MUX_CTL */
		u32 pcr  = readl((void __iomem *)0x30690000);	/* PWM4 PWMCR */
		u32 psar = readl((void __iomem *)0x3069000c);	/* PWM4 PWMSAR (duty) */
		u32 ppr  = readl((void __iomem *)0x30690010);	/* PWM4 PWMPR (period) */

		snprintf(buf, sizeof(buf), "mux=%x cr=%x sar=%x pr=%x",
			 pmux, pcr, psar, ppr);
		env_set("fastboot.disp_pwm", buf);
	}
	{
		u32 dsts  = readl((void __iomem *)0x32e10004);	/* DSIM_STATUS */
		u32 dpll  = readl((void __iomem *)0x32e10094);	/* DSIM_PLLCTRL */
		u32 lctrl = readl((void __iomem *)0x32e00000);	/* LCDIF CTRL */

		snprintf(buf, sizeof(buf), "dsim_sts=%x pll=%x lcdif=%x",
			 dsts, dpll, lctrl);
		env_set("fastboot.disp_hw", buf);
	}

	/*
	 * Proper raw PWM4 backlight. Video pipeline is confirmed up (DSIM PLL
	 * locked, LCDIF running) so the dark panel is the LED backlight not
	 * driving. The pwm_backlight path left PWMCR EN clear; and a 100% duty
	 * is constant-high (no toggling), which an LED-boost backlight driver
	 * can read as OFF. Program a real 50% toggling duty: reset EN, refill
	 * the 4-deep sample FIFO, then set EN last. CLKSRC/prescaler bits in CR
	 * are already correct from the driver's set_config.
	 */
	{
		struct udevice *pwm = NULL, *d;

		/* Find PWM4 (pwm@30690000) and drive it via the pwm-imx driver with
		 * the EXACT DT/Linux values. set_config(period=100000ns) makes the
		 * driver compute PWMPR from the *real* PWM4 clock, so the output is
		 * exactly 10 kHz whatever the clock rate — the LED-boost backlight is
		 * frequency/polarity-sensitive, and a raw PWMPR guess could be a
		 * wrong frequency. Duty 98039 ns (~98%, Linux level 250/255). */
		for (uclass_first_device(UCLASS_PWM, &d); d; uclass_next_device(&d)) {
			if (dev_read_addr(d) == 0x30690000) {
				pwm = d;
				break;
			}
		}
		if (pwm) {
			int e1 = pwm_set_config(pwm, 0, 100000, 98039);
			int e2 = pwm_set_enable(pwm, 0, true);

			/*
			 * Live-register diff vs the backlight-lit Linux: the ONLY
			 * PWM4 difference is PWMCR bit25 (STOPEN) — Linux CR
			 * 0x03C20001 vs u-boot 0x01C20001. Linux's pwm-imx sets
			 * STOPEN; u-boot's does not. Set it to match exactly.
			 */
			setbits_le32((void __iomem *)0x30690000, (1u << 25));
			mdelay(50);
			snprintf(buf, sizeof(buf),
				 "cfg=%d en=%d cr=%x pr=%x sar=%x", e1, e2,
				 readl((void __iomem *)0x30690000),
				 readl((void __iomem *)0x30690010),
				 readl((void __iomem *)0x3069000c));
		} else {
			snprintf(buf, sizeof(buf), "pwm4-not-found");
		}
		env_set("fastboot.disp_pwmen", buf);
	}

	/*
	 * Panel power (gpio2_4) + DSIM RGB video state. DSIM PLL-locked +
	 * LCDIF-running does NOT prove the panel glass is powered — a DSI write
	 * "succeeds" SoC-side regardless. Read GPIO2 DR/GDIR (want bit4 high =
	 * panel power on) then force gpio2_4 output-high in case the panel
	 * driver's power-gpio never drove it. rgb = DSIM_RGB_STATUS (is the DSIM
	 * actually in RGB video-transfer state, not stuck in LP/command mode).
	 */
	{
		void __iomem *g2 = (void __iomem *)0x30210000;	/* GPIO2 */
		void __iomem *rgbr = (void __iomem *)0x32e10008; /* DSIM_RGB_STATUS */
		u32 dr0 = readl(g2 + 0x00);
		u32 r1, r2, r3, r4;

		setbits_le32(g2 + 0x04, (1u << 4));	/* gpio2_4 output */
		setbits_le32(g2 + 0x00, (1u << 4));	/* gpio2_4 high (panel power) */

		/* Sample the DSIM RGB FSM 4x — cycling => video is streaming
		 * (=> backlight fault); frozen => DSIM not transmitting. */
		r1 = readl(rgbr); mdelay(4);
		r2 = readl(rgbr); mdelay(4);
		r3 = readl(rgbr); mdelay(4);
		r4 = readl(rgbr);
		snprintf(buf, sizeof(buf), "pwr4=%x rgb=%x,%x,%x,%x",
			 (dr0 >> 4) & 1, r1 & 0x1fff, r2 & 0x1fff,
			 r3 & 0x1fff, r4 & 0x1fff);
		env_set("fastboot.disp_gpio", buf);
	}

	/*
	 * PMIC LDO state (BD71837 i2c1@0x4b regs 0x1c-0x22) + the SAI3_MCLK pad
	 * config. Diff vs the backlight-lit Linux state (ldo=8f,43,80,48,c0,01,00)
	 * to find a backlight-VIN rail u-boot's SPL leaves off.
	 */
	{
		struct udevice *bus, *pmic;
		u8 v[7] = { 0 };
		u32 pad = readl((void __iomem *)0x3033044c);	/* SAI3_MCLK pad */

		if (!uclass_get_device_by_seq(UCLASS_I2C, 0, &bus) &&
		    !dm_i2c_probe(bus, 0x4b, 0, &pmic))
			dm_i2c_read(pmic, 0x1c, v, 7);
		snprintf(buf, sizeof(buf), "ldo=%x,%x,%x,%x,%x,%x,%x pad=%x",
			 v[0], v[1], v[2], v[3], v[4], v[5], v[6], pad);
		env_set("fastboot.disp_pmic", buf);
	}
	printf("c60: test pattern drawn; entering fastboot for diagnostics\n");

diag_fastboot:
	/*
	 * Enter fastboot: holds the panel frame for the camera, lets the host
	 * read the diag vars (`fastboot getvar disp_*`) and `fastboot reboot`
	 * to recover. If fastboot ever RETURNS (e.g. USB init failure), hold
	 * the frame ~45 s for the camera then reset to SDP — never pet the WDOG
	 * forever, which would wedge the board with no console to recover.
	 * Phase B replaces all of this with the finger-boot menu.
	 */
	run_command("fastboot usb 0", 0);
	for (x = 0; x < 4500; x++) {
		schedule();
		mdelay(10);
	}
	run_command("reset", 0);
}
#endif

#ifndef CONFIG_SPL_BUILD
/*
 * ===========================================================================
 * C60 light-bar bootsel (Phase B)
 * ===========================================================================
 * The C60's user interface is a 3-zone RGB light bar, not the panel: three
 * TI LP5569 9-channel controllers @ 0x32, one per zone, on separate I2C
 * buses -- CENTRE on i2c2 (u-boot seq 1, shared with the FT5x06 touch),
 * LEFT on i2c3 (seq 2), RIGHT on i2c4 (seq 3). Each drives 3 RGB LEDs
 * (9 outputs: OUT0..8 = r1,g1,b1,r2,g2,b2,r3,g3,b3; PWM base 0x16, current
 * base 0x22). We drive them raw over DM-I2C in direct-PWM mode (no engine),
 * exactly as the mainline lp5569 driver's post_init does.
 *
 * Boot UX (panel-independent, so it works even though the LED backlight is
 * unsolved): bar breathes amber = "touch a zone"; a finger on the FT5x06
 * lights that zone green and latches a selection; the window closes and the
 * chosen slot is exported as ${boot_slot} for preboot to honour.
 */
#define LP5569_ADDR		0x32
#define LP5569_REG_ENABLE	0x00
#define LP5569_REG_OP_MODE	0x02
#define LP5569_REG_PWM_BASE	0x16
#define LP5569_REG_CURRENT_BASE	0x22
#define LP5569_REG_MISC		0x2f
#define LP5569_REG_STATUS	0x3c
#define LP5569_REG_RESET	0x3f
#define LP5569_CHIP_EN		0x40
#define LP5569_STARTUP_BUSY	0x40
/* MISC: auto-increment | power-save | pwm-power-save | CP=auto(3) | int-clk.
 * All three run on their own internal oscillator here (independent) -- the
 * kernel phase-locks them for animations, but a bootsel cue does not need
 * it, and independence is more robust with a cold bar. */
#define LP5569_MISC_CFG		0x7d
#define LP5569_LED_CURRENT	0xaf	/* stock led-cur */

/* chip index: 0 = centre (seq1), 1 = left (seq2), 2 = right (seq3) */
static struct udevice *ledbar_dev[3];
static const int ledbar_seq[3] = { 1, 2, 3 };

/* forward decls -- defined in the FT5x06 touch section further down */
static void c60_ft_reset(void);
static int c60_ft_read(int *points, int *x, int *y);

static void ledbar_w(int i, u8 reg, u8 val)
{
	if (ledbar_dev[i])
		dm_i2c_reg_write(ledbar_dev[i], reg, val);
}

static void ledbar_chip_init(int i)
{
	int t;

	ledbar_w(i, LP5569_REG_RESET, 0xff);	/* clear all registers */
	mdelay(10);
	ledbar_w(i, LP5569_REG_ENABLE, LP5569_CHIP_EN);
	mdelay(2);
	ledbar_w(i, LP5569_REG_MISC, LP5569_MISC_CFG);
	mdelay(2);
	/* wait for startup-busy to clear */
	for (t = 0; t < 10; t++) {
		if (ledbar_dev[i] &&
		    !(dm_i2c_reg_read(ledbar_dev[i], LP5569_REG_STATUS) &
		      LP5569_STARTUP_BUSY))
			break;
		mdelay(1);
	}
	ledbar_w(i, LP5569_REG_OP_MODE, 0x00);	/* direct PWM, no engine */
	for (t = 0; t < 9; t++) {
		ledbar_w(i, LP5569_REG_CURRENT_BASE + t, LP5569_LED_CURRENT);
		ledbar_w(i, LP5569_REG_PWM_BASE + t, 0x00);
	}
}

/* Paint one zone's three RGB LEDs a single colour. */
static void ledbar_zone(int i, u8 r, u8 g, u8 b)
{
	int grp;

	for (grp = 0; grp < 3; grp++) {
		u8 base = LP5569_REG_PWM_BASE + grp * 3;

		ledbar_w(i, base + 0, r);
		ledbar_w(i, base + 1, g);
		ledbar_w(i, base + 2, b);
	}
}

static void ledbar_all(u8 r, u8 g, u8 b)
{
	int i;

	for (i = 0; i < 3; i++)
		ledbar_zone(i, r, g, b);
}

static int ledbar_init(void)
{
	struct udevice *bus;
	int i, n = 0;

	for (i = 0; i < 3; i++) {
		ledbar_dev[i] = NULL;
		if (uclass_get_device_by_seq(UCLASS_I2C, ledbar_seq[i], &bus))
			continue;
		if (dm_i2c_probe(bus, LP5569_ADDR, 0, &ledbar_dev[i]))
			ledbar_dev[i] = NULL;
		if (ledbar_dev[i]) {
			ledbar_chip_init(i);
			n++;
		}
	}
	return n;
}

/*
 * Boot selection over the light bar + touch. Returns having set ${boot_slot}
 * (a|b) and, for a centre press, ${boot_fastboot}=1. Non-destructive: if
 * nothing is present it defaults to slot A and returns so preboot proceeds.
 *
 * Touch->zone mapping uses native-portrait X (0..719); the physical
 * left/centre/right split is confirmed on the bench and flipped here if the
 * bar and finger disagree.
 */
static void c60_bootsel(void)
{
	ulong start;
	int nbar, points, x = 0, y = 0;
	int sel = -1, hold = 0, confirmed = -1;
	int breath, dir = 1;

	nbar = ledbar_init();
	printf("c60_bootsel: %d/3 light-bar controllers up\n", nbar);

#ifdef C60_BAR_DIAG
	/* Diagnostic build: report probe results, hold the bar solid amber,
	 * and drop to fastboot so the state is camera-stable and readable via
	 * `fastboot getvar bar_n|bar_p|bar_s`. */
	{
		struct udevice *bus;
		int s, ack[3] = { 0, 0, 0 };
		char b[48];

		for (s = 0; s < 3; s++)
			if (!uclass_get_device_by_seq(UCLASS_I2C, ledbar_seq[s], &bus))
				ack[s] = !dm_i2c_probe(bus, LP5569_ADDR, 0,
						       &(struct udevice *){ NULL });
		snprintf(b, sizeof(b), "%d", nbar);
		env_set("fastboot.bar_n", b);
		snprintf(b, sizeof(b), "dev c%d l%d r%d  i2cack c%d l%d r%d",
			 !!ledbar_dev[0], !!ledbar_dev[1], !!ledbar_dev[2],
			 ack[0], ack[1], ack[2]);
		env_set("fastboot.bar_p", b);
		if (ledbar_dev[0]) {
			int st = dm_i2c_reg_read(ledbar_dev[0], LP5569_REG_STATUS);
			int en = dm_i2c_reg_read(ledbar_dev[0], LP5569_REG_ENABLE);
			snprintf(b, sizeof(b), "c:status=%02x enable=%02x", st, en);
			env_set("fastboot.bar_s", b);
		}
		ledbar_all(0xff, 0x55, 0x00);	/* solid bright amber, held */
		run_command("fastboot usb 0", 0);
		return;
	}
#endif

	c60_ft_reset();

	/* Cue: ~2s amber breathe on all zones = "touch a zone to choose". */
	for (breath = 0; breath >= 0 && breath <= 255;) {
		ledbar_all(breath, breath / 3, 0);	/* amber */
		mdelay(8);
		schedule();
		breath += dir * 16;
		if (breath > 255) { breath = 255; dir = -1; }
		if (breath < 0 && dir < 0) break;
	}
	ledbar_all(0x20, 0x0a, 0x00);	/* idle dim amber */

	/* ~8s selection window. */
	start = get_timer(0);
	while (get_timer(start) < 8000) {
		schedule();
		points = 0;
		if (c60_ft_read(&points, &x, &y) < 0)
			points = 0;

		if (points > 0) {
			/* map X third -> chip index (centre=0,left=1,right=2) */
			int zone = (x < 240) ? 1 : (x < 480) ? 0 : 2;

			ledbar_zone(0, 0, 0, 0);
			ledbar_zone(1, 0, 0, 0);
			ledbar_zone(2, 0, 0, 0);
			ledbar_zone(zone, 0, 0xff, 0);	/* touched = green */

			if (zone == sel) {
				if (++hold >= 6) {	/* ~held 300ms */
					confirmed = zone;
					break;
				}
			} else {
				sel = zone;
				hold = 0;
			}
		} else {
			sel = -1;
			hold = 0;
			ledbar_all(0x20, 0x0a, 0x00);
		}
		mdelay(50);
	}

	/* Apply. centre(0)=fastboot, left(1)=slot A, right(2)=slot B. */
	if (confirmed == 0) {
		env_set("boot_fastboot", "1");
		ledbar_all(0, 0, 0);
		ledbar_zone(0, 0xff, 0x40, 0);	/* centre orange = fastboot */
		printf("c60_bootsel: centre -> fastboot\n");
	} else if (confirmed == 2) {
		env_set("boot_slot", "b");
		ledbar_all(0, 0, 0);
		ledbar_zone(2, 0, 0xff, 0);
		printf("c60_bootsel: right -> slot B\n");
	} else {
		env_set("boot_slot", "a");	/* left or timeout */
		ledbar_all(0, 0, 0);
		ledbar_zone(1, 0, 0xff, 0);
		printf("c60_bootsel: %s -> slot A\n",
		       confirmed == 1 ? "left" : "timeout");
	}
	mdelay(700);		/* let the confirmation register on camera/eye */

	if (env_get("boot_fastboot"))
		run_command("fastboot usb 0", 0);
}
#endif /* !CONFIG_SPL_BUILD */

int board_late_init(void)
{
#ifdef CONFIG_ENV_IS_IN_MMC
	board_late_mmc_env_init();
#endif

	if (IS_ENABLED(CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG)) {
		env_set("board_name", "EVK");
		env_set("board_rev", "iMX8MM");
	}

#ifndef CONFIG_SPL_BUILD
#ifdef C60_DISPLAY_TEST
	/* Diagnostic build: draw the flashlight test pattern and hold it. */
	c60_display_test();
#else
	/*
	 * PHASE B: light-bar + touch bootsel. Replaces the Phase-A display
	 * diagnostic (c60_display_test, still compiled for reference) now that
	 * the panel backlight work is parked -- the bar is the real UI and is
	 * fully camera-verifiable. Returns so preboot/bootcmd runs.
	 */
	c60_bootsel();		/* sets ${boot_slot} (defaults a) */

	/*
	 * Set `preboot` from board code, not CONFIG_PREBOOT. The Android env
	 * header (imx8mm_evk_android.h) supplies the compiled default env
	 * (bootcmd="boota mmc0") and does NOT carry the defconfig CONFIG_PREBOOT
	 * through, so on any clean/erased env `preboot` is empty and the board
	 * drops to fastboot. Setting it here — unconditionally, the way the
	 * Android header sets bootcmd — guarantees the A/B local-boot runs
	 * regardless of saved-env state. `preboot` (not bootcmd) because in the
	 * SDP RAM-load path the FSL USB-boot detector overrides bootcmd with
	 * fastboot; preboot still runs first. Self-contained + parameterised by
	 * ${boot_slot} (boot.img v0 parse -> booti; see BOOT_RECIPES.md).
	 */
	if (!env_get("boot_slot"))
		env_set("boot_slot", "a");
	env_set("preboot",
		"mmc dev 0; "
		"part start mmc 0 boot_${boot_slot} ba; "
		"part size mmc 0 boot_${boot_slot} bn; "
		"mmc read 0x50000000 ${ba} ${bn}; "
		"setexpr.l hks *0x50000008; setexpr.l hrs *0x50000010; "
		"setexpr.l hss *0x50000018; setexpr.l hps *0x50000024; "
		"setexpr pm1 ${hps} - 1; "
		"setexpr kpd ${hks} + ${pm1}; setexpr kpd ${kpd} / ${hps}; "
		"setexpr kpd ${kpd} * ${hps}; "
		"setexpr rpd ${hrs} + ${pm1}; setexpr rpd ${rpd} / ${hps}; "
		"setexpr rpd ${rpd} * ${hps}; "
		"setexpr dof ${hps} + ${kpd}; setexpr dof ${dof} + ${rpd}; "
		"setexpr ksr 0x50000000 + ${hps}; setexpr dsr 0x50000000 + ${dof}; "
		"cp.b ${ksr} 0x40080000 ${hks}; cp.b ${dsr} 0x46000000 ${hss}; "
		"setenv bootargs console=tty0 console=ttymxc1,115200 "
		"earlycon=ec_imx6q,0x30890000,115200 panic=10 rw rootwait "
		"root=PARTLABEL=system_${boot_slot} systemd.unit=multi-user.target; "
		"booti 0x40080000 - 0x46000000");
#endif
#endif

	return 0;
}

#ifndef CONFIG_SPL_BUILD
/*
 * FocalTech FT5x46-class capacitive touch controller (chip id 0x54 at
 * register 0xa3) on i2c2 @ 0x38, reset on GPIO2_IO3 (pad SD1_DATA1,
 * active-low). polycom-uboot has no touch driver; c60_touch muxes and
 * deasserts the reset line, then reads the standard FocalTech touch-report
 * registers (0x02 = point count, 0x03..0x06 = touch1 XH/XL/YH/YL) so a
 * finger press can gate boot selection.
 */
#define FT5X06_I2C_SEQ		1		/* alias i2c1 = &i2c2 */
#define FT5X06_ADDR		0x38
#define FT5X06_RST_GPIO		IMX_GPIO_NR(2, 3)
#define FT5X06_REG_TD_STATUS	0x02

static iomux_v3_cfg_t const ft5x06_reset_pads[] = {
	IMX8MM_PAD_SD1_DATA1_GPIO2_IO3 | MUX_PAD_CTRL(PAD_CTL_DSE6),
};

/*
 * Reset once, then poll many times: the controller needs a ~300ms firmware
 * boot after reset, so c60_bootsel resets a single time up front (c60_ft_reset)
 * and then reads in a tight loop (c60_ft_read) without re-toggling the line.
 */
static void c60_ft_reset(void)
{
	imx_iomux_v3_setup_multiple_pads(ft5x06_reset_pads,
					 ARRAY_SIZE(ft5x06_reset_pads));
	gpio_request(FT5X06_RST_GPIO, "ft5x06_reset");
	gpio_direction_output(FT5X06_RST_GPIO, 0);	/* assert reset */
	mdelay(5);
	gpio_direction_output(FT5X06_RST_GPIO, 1);	/* deassert reset */
	mdelay(300);					/* controller fw boot */
}

/* One touch-report read, no reset. Returns point count (>=0) or <0 on error. */
static int c60_ft_read(int *points, int *x, int *y)
{
	struct udevice *bus, *dev;
	u8 buf[5];
	int ret;

	ret = uclass_get_device_by_seq(UCLASS_I2C, FT5X06_I2C_SEQ, &bus);
	if (ret)
		return ret;
	ret = dm_i2c_probe(bus, FT5X06_ADDR, 0, &dev);
	if (ret)
		return ret;
	ret = dm_i2c_read(dev, FT5X06_REG_TD_STATUS, buf, sizeof(buf));
	if (ret)
		return ret;

	*points = buf[0] & 0x0f;
	*x      = ((buf[1] & 0x0f) << 8) | buf[2];
	*y      = ((buf[3] & 0x0f) << 8) | buf[4];
	return *points;
}

static int ft5x06_read_point(int *points, int *event, int *x, int *y)
{
	int ret;

	c60_ft_reset();
	ret = c60_ft_read(points, x, y);
	if (ret < 0)
		return ret;
	*event = 0;	/* event field folded out of the fast path */
	return 0;
}

static int do_c60_touch(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	int points = 0, event = 0, x = 0, y = 0, ret;

	ret = ft5x06_read_point(&points, &event, &x, &y);
	if (ret) {
		printf("c60_touch: FT5x06 read failed (%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	printf("c60_touch: points=%d event=%d x=%d y=%d\n",
	       points, event, x, y);
	env_set_ulong("touch_points", points);
	env_set_ulong("touch_x", x);
	env_set_ulong("touch_y", y);

	return points ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

U_BOOT_CMD(c60_touch, 1, 0, do_c60_touch,
	   "read FocalTech FT5x06 touch point (i2c2 @ 0x38)",
	   "\n"
	   "    - deassert touch reset, read one touch point\n"
	   "    - sets touch_points/touch_x/touch_y; returns 0 when touched");
#endif /* !CONFIG_SPL_BUILD */

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

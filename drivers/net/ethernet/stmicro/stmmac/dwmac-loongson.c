// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Loongson Corporation
 */

#include <linux/clk-provider.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/of_irq.h>
#include "stmmac.h"
#include "dwmac_dma.h"
#include "dwmac1000.h"

/* Normal Loongson Tx Summary */
#define DMA_INTR_ENA_NIE_TX_LOONGSON	0x00040000
/* Normal Loongson Rx Summary */
#define DMA_INTR_ENA_NIE_RX_LOONGSON	0x00020000

#define DMA_INTR_NORMAL_LOONGSON	(DMA_INTR_ENA_NIE_TX_LOONGSON | \
					 DMA_INTR_ENA_NIE_RX_LOONGSON | \
					 DMA_INTR_ENA_RIE | DMA_INTR_ENA_TIE)

/* Abnormal Loongson Tx Summary */
#define DMA_INTR_ENA_AIE_TX_LOONGSON	0x00010000
/* Abnormal Loongson Rx Summary */
#define DMA_INTR_ENA_AIE_RX_LOONGSON	0x00008000

#define DMA_INTR_ABNORMAL_LOONGSON	(DMA_INTR_ENA_AIE_TX_LOONGSON | \
					 DMA_INTR_ENA_AIE_RX_LOONGSON | \
					 DMA_INTR_ENA_FBE | DMA_INTR_ENA_UNE)

#define DMA_INTR_DEFAULT_MASK_LOONGSON	(DMA_INTR_NORMAL_LOONGSON | \
					 DMA_INTR_ABNORMAL_LOONGSON)

/* Normal Loongson Tx Interrupt Summary */
#define DMA_STATUS_NIS_TX_LOONGSON	0x00040000
/* Normal Loongson Rx Interrupt Summary */
#define DMA_STATUS_NIS_RX_LOONGSON	0x00020000

/* Abnormal Loongson Tx Interrupt Summary */
#define DMA_STATUS_AIS_TX_LOONGSON	0x00010000
/* Abnormal Loongson Rx Interrupt Summary */
#define DMA_STATUS_AIS_RX_LOONGSON	0x00008000

/* Fatal Loongson Tx Bus Error Interrupt */
#define DMA_STATUS_FBI_TX_LOONGSON	0x00002000
/* Fatal Loongson Rx Bus Error Interrupt */
#define DMA_STATUS_FBI_RX_LOONGSON	0x00001000

#define DMA_STATUS_MSK_COMMON_LOONGSON	(DMA_STATUS_NIS_TX_LOONGSON | \
					 DMA_STATUS_NIS_RX_LOONGSON | \
					 DMA_STATUS_AIS_TX_LOONGSON | \
					 DMA_STATUS_AIS_RX_LOONGSON | \
					 DMA_STATUS_FBI_TX_LOONGSON | \
					 DMA_STATUS_FBI_RX_LOONGSON)

#define DMA_STATUS_MSK_RX_LOONGSON	(DMA_STATUS_ERI | DMA_STATUS_RWT | \
					 DMA_STATUS_RPS | DMA_STATUS_RU  | \
					 DMA_STATUS_RI  | DMA_STATUS_OVF | \
					 DMA_STATUS_MSK_COMMON_LOONGSON)

#define DMA_STATUS_MSK_TX_LOONGSON	(DMA_STATUS_ETI | DMA_STATUS_UNF | \
					 DMA_STATUS_TJT | DMA_STATUS_TU  | \
					 DMA_STATUS_TPS | DMA_STATUS_TI  | \
					 DMA_STATUS_MSK_COMMON_LOONGSON)

#define PCI_DEVICE_ID_LOONGSON_GMAC	0x7a03
#define PCI_DEVICE_ID_LOONGSON_GNET	0x7a13
#define LOONGSON_DWMAC_CORE_1_00	0x10	/* Loongson custom IP */
#define CHANNEL_NUM			8

struct loongson_data {
	u32 gmac_verion;
	struct device *dev;
};

struct stmmac_pci_info {
	int (*setup)(struct pci_dev *pdev, struct plat_stmmacenet_data *plat);
};

static void loongson_default_data(struct pci_dev *pdev,
				  struct plat_stmmacenet_data *plat)
{
	/* Get bus_id, this can be overloaded later */
	plat->bus_id = (pci_domain_nr(pdev->bus) << 16) |
			PCI_DEVID(pdev->bus->number, pdev->devfn);

	plat->clk_csr = 2;	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->has_gmac = 1;
	plat->force_sf_dma_mode = 1;

	plat->mac_interface = PHY_INTERFACE_MODE_GMII;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	/* Disable Priority config by default */
	plat->tx_queues_cfg[0].use_prio = false;
	plat->rx_queues_cfg[0].use_prio = false;

	/* Disable RX queues routing by default */
	plat->rx_queues_cfg[0].pkt_route = 0x0;

	plat->clk_ref_rate = 125000000;
	plat->clk_ptp_rate = 125000000;

	/* Default to phy auto-detection */
	plat->phy_addr = -1;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	plat->multicast_filter_bins = 256;
}

static int loongson_gmac_data(struct pci_dev *pdev,
			      struct plat_stmmacenet_data *plat)
{
	loongson_default_data(pdev, plat);

	plat->mdio_bus_data->phy_mask = 0;
	plat->phy_interface = PHY_INTERFACE_MODE_RGMII_ID;

	return 0;
}

static struct stmmac_pci_info loongson_gmac_pci_info = {
	.setup = loongson_gmac_data,
};

static void loongson_gnet_dma_init_channel(struct stmmac_priv *priv,
					   void __iomem *ioaddr,
					   struct stmmac_dma_cfg *dma_cfg,
					   u32 chan)
{
	int txpbl = dma_cfg->txpbl ?: dma_cfg->pbl;
	int rxpbl = dma_cfg->rxpbl ?: dma_cfg->pbl;
	u32 value;

	value = readl(ioaddr + DMA_CHAN_BUS_MODE(chan));

	if (dma_cfg->pblx8)
		value |= DMA_BUS_MODE_MAXPBL;

	value |= DMA_BUS_MODE_USP;
	value &= ~(DMA_BUS_MODE_PBL_MASK | DMA_BUS_MODE_RPBL_MASK);
	value |= (txpbl << DMA_BUS_MODE_PBL_SHIFT);
	value |= (rxpbl << DMA_BUS_MODE_RPBL_SHIFT);

	/* Set the Fixed burst mode */
	if (dma_cfg->fixed_burst)
		value |= DMA_BUS_MODE_FB;

	/* Mixed Burst has no effect when fb is set */
	if (dma_cfg->mixed_burst)
		value |= DMA_BUS_MODE_MB;

	if (dma_cfg->atds)
		value |= DMA_BUS_MODE_ATDS;

	if (dma_cfg->aal)
		value |= DMA_BUS_MODE_AAL;

	writel(value, ioaddr + DMA_CHAN_BUS_MODE(chan));

	/* Mask interrupts by writing to CSR7 */
	writel(DMA_INTR_DEFAULT_MASK_LOONGSON, ioaddr +
	       DMA_CHAN_INTR_ENA(chan));
}

static int loongson_gnet_dma_interrupt(struct stmmac_priv *priv,
				       void __iomem *ioaddr,
				       struct stmmac_extra_stats *x,
				       u32 chan, u32 dir)
{
	struct stmmac_pcpu_stats *stats = this_cpu_ptr(priv->xstats.pcpu_stats);
	u32 abnor_intr_status;
	u32 nor_intr_status;
	u32 fb_intr_status;
	u32 intr_status;
	int ret = 0;

	/* read the status register (CSR5) */
	intr_status = readl(ioaddr + DMA_CHAN_STATUS(chan));

	if (dir == DMA_DIR_RX)
		intr_status &= DMA_STATUS_MSK_RX_LOONGSON;
	else if (dir == DMA_DIR_TX)
		intr_status &= DMA_STATUS_MSK_TX_LOONGSON;

	nor_intr_status = intr_status & (DMA_STATUS_NIS_TX_LOONGSON |
		DMA_STATUS_NIS_RX_LOONGSON);
	abnor_intr_status = intr_status & (DMA_STATUS_AIS_TX_LOONGSON |
		DMA_STATUS_AIS_RX_LOONGSON);
	fb_intr_status = intr_status & (DMA_STATUS_FBI_TX_LOONGSON |
		DMA_STATUS_FBI_RX_LOONGSON);

	/* ABNORMAL interrupts */
	if (unlikely(abnor_intr_status)) {
		if (unlikely(intr_status & DMA_STATUS_UNF)) {
			ret = tx_hard_error_bump_tc;
			x->tx_undeflow_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_TJT))
			x->tx_jabber_irq++;
		if (unlikely(intr_status & DMA_STATUS_OVF))
			x->rx_overflow_irq++;
		if (unlikely(intr_status & DMA_STATUS_RU))
			x->rx_buf_unav_irq++;
		if (unlikely(intr_status & DMA_STATUS_RPS))
			x->rx_process_stopped_irq++;
		if (unlikely(intr_status & DMA_STATUS_RWT))
			x->rx_watchdog_irq++;
		if (unlikely(intr_status & DMA_STATUS_ETI))
			x->tx_early_irq++;
		if (unlikely(intr_status & DMA_STATUS_TPS)) {
			x->tx_process_stopped_irq++;
			ret = tx_hard_error;
		}
		if (unlikely(fb_intr_status)) {
			x->fatal_bus_error_irq++;
			ret = tx_hard_error;
		}
	}
	/* TX/RX NORMAL interrupts */
	if (likely(nor_intr_status)) {
		if (likely(intr_status & DMA_STATUS_RI)) {
			u32 value = readl(ioaddr + DMA_INTR_ENA);
			/* to schedule NAPI on real RIE event. */
			if (likely(value & DMA_INTR_ENA_RIE)) {
				u64_stats_update_begin(&stats->syncp);
				u64_stats_inc(&stats->rx_normal_irq_n[chan]);
				u64_stats_update_end(&stats->syncp);
				ret |= handle_rx;
			}
		}
		if (likely(intr_status & DMA_STATUS_TI)) {
			u64_stats_update_begin(&stats->syncp);
			u64_stats_inc(&stats->tx_normal_irq_n[chan]);
			u64_stats_update_end(&stats->syncp);
			ret |= handle_tx;
		}
		if (unlikely(intr_status & DMA_STATUS_ERI))
			x->rx_early_irq++;
	}
	/* Optional hardware blocks, interrupts should be disabled */
	if (unlikely(intr_status &
		     (DMA_STATUS_GPI | DMA_STATUS_GMI | DMA_STATUS_GLI)))
		pr_warn("%s: unexpected status %08x\n", __func__, intr_status);

	/* Clear the interrupt by writing a logic 1 to the CSR5[15-0] */
	writel((intr_status & 0x7ffff), ioaddr + DMA_CHAN_STATUS(chan));

	return ret;
}

static void loongson_gnet_fix_speed(void *priv, unsigned int speed,
				    unsigned int mode)
{
	struct loongson_data *ld = (struct loongson_data *)priv;
	struct net_device *ndev = dev_get_drvdata(ld->dev);
	struct stmmac_priv *ptr = netdev_priv(ndev);

	/* The controller and PHY don't work well together.
	 * We need to use the PS bit to check if the controller's status
	 * is correct and reset PHY if necessary.
	 * MAC_CTRL_REG.15 is defined by the GMAC_CONTROL_PS macro.
	 */
	if (speed == SPEED_1000) {
		if (readl(ptr->ioaddr + MAC_CTRL_REG) &
		    GMAC_CONTROL_PS)
			/* Word around hardware bug, restart autoneg */
			phy_restart_aneg(ndev->phydev);
	}
}

static int loongson_gnet_data(struct pci_dev *pdev,
			      struct plat_stmmacenet_data *plat)
{
	loongson_default_data(pdev, plat);

	plat->phy_interface = PHY_INTERFACE_MODE_GMII;
	plat->mdio_bus_data->phy_mask = ~(u32)BIT(2);
	plat->fix_mac_speed = loongson_gnet_fix_speed;

	/* GNET devices with dev revision 0x00 do not support manually
	 * setting the speed to 1000.
	 */
	if (pdev->revision == 0x00)
		plat->flags |= STMMAC_FLAG_DISABLE_FORCE_1000;

	return 0;
}

static struct stmmac_pci_info loongson_gnet_pci_info = {
	.setup = loongson_gnet_data,
};

static int loongson_dwmac_config_legacy(struct pci_dev *pdev,
					struct plat_stmmacenet_data *plat,
					struct stmmac_resources *res,
					struct device_node *np)
{
	if (np) {
		res->irq = of_irq_get_byname(np, "macirq");
		if (res->irq < 0) {
			dev_err(&pdev->dev, "IRQ macirq not found\n");
			return -ENODEV;
		}

		res->wol_irq = of_irq_get_byname(np, "eth_wake_irq");
		if (res->wol_irq < 0) {
			dev_info(&pdev->dev,
				 "IRQ eth_wake_irq not found, using macirq\n");
			res->wol_irq = res->irq;
		}

		res->lpi_irq = of_irq_get_byname(np, "eth_lpi");
		if (res->lpi_irq < 0) {
			dev_err(&pdev->dev, "IRQ eth_lpi not found\n");
			return -ENODEV;
		}
	} else {
		res->irq = pdev->irq;
		res->wol_irq = res->irq;
	}

	return 0;
}

static int loongson_dwmac_config_msi(struct pci_dev *pdev,
				     struct plat_stmmacenet_data *plat,
				     struct stmmac_resources *res,
				     struct device_node *np)
{
	int i, ret, vecs;

	vecs = roundup_pow_of_two(CHANNEL_NUM * 2 + 1);
	ret = pci_alloc_irq_vectors(pdev, vecs, vecs, PCI_IRQ_MSI);
	if (ret < 0) {
		dev_info(&pdev->dev,
			 "MSI enable failed, Fallback to legacy interrupt\n");
		return loongson_dwmac_config_legacy(pdev, plat, res, np);
	}

	res->irq = pci_irq_vector(pdev, 0);
	res->wol_irq = 0;

	/* INT NAME | MAC | CH7 rx | CH7 tx | ... | CH0 rx | CH0 tx |
	 * --------- ----- -------- --------  ...  -------- --------
	 * IRQ NUM  |  0  |   1    |   2    | ... |   15   |   16   |
	 */
	for (i = 0; i < CHANNEL_NUM; i++) {
		res->rx_irq[CHANNEL_NUM - 1 - i] =
			pci_irq_vector(pdev, 1 + i * 2);
		res->tx_irq[CHANNEL_NUM - 1 - i] =
			pci_irq_vector(pdev, 2 + i * 2);
	}

	plat->flags |= STMMAC_FLAG_MULTI_MSI_EN;

	return 0;
}

static struct mac_device_info *loongson_dwmac_setup(void *apriv)
{
	struct stmmac_priv *priv = apriv;
	struct mac_device_info *mac;
	struct stmmac_dma_ops *dma;
	struct loongson_data *ld;
	struct pci_dev *pdev;

	ld = priv->plat->bsp_priv;
	pdev = to_pci_dev(priv->device);

	mac = devm_kzalloc(priv->device, sizeof(*mac), GFP_KERNEL);
	if (!mac)
		return NULL;

	dma = devm_kzalloc(priv->device, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return NULL;

	/* The original IP-core version is 0x37 in all Loongson GNET
	 * (ls2k2000 and ls7a2000), but the GNET HW designers have changed the
	 * GMAC_VERSION.SNPSVER field to the custom 0x10 value on the Loongson
	 * ls2k2000 MAC to emphasize the differences: multiple DMA-channels,
	 * AV feature and GMAC_INT_STATUS CSR flags layout. Get back the
	 * original value so the correct HW-interface would be selected.
	 */
	if (ld->gmac_verion == LOONGSON_DWMAC_CORE_1_00) {
		priv->synopsys_id = DWMAC_CORE_3_70;
		*dma = dwmac1000_dma_ops;
		dma->init_chan = loongson_gnet_dma_init_channel;
		dma->dma_interrupt = loongson_gnet_dma_interrupt;
		mac->dma = dma;
	}

	priv->dev->priv_flags |= IFF_UNICAST_FLT;

	/* Pre-initialize the respective "mac" fields as it's done in
	 * dwmac1000_setup()
	 */
	mac->pcsr = priv->ioaddr;
	mac->multicast_filter_bins = priv->plat->multicast_filter_bins;
	mac->unicast_filter_entries = priv->plat->unicast_filter_entries;
	mac->mcast_bits_log2 = 0;

	if (mac->multicast_filter_bins)
		mac->mcast_bits_log2 = ilog2(mac->multicast_filter_bins);

	/* The GMAC devices with PCI ID 0x7a03 does not support any pause mode.
	 * The GNET devices without CORE ID 0x10 does not support half-duplex.
	 */
	if (pdev->device == PCI_DEVICE_ID_LOONGSON_GMAC) {
		mac->link.caps = MAC_10 | MAC_100 | MAC_1000;
	} else {
		if (ld->gmac_verion == LOONGSON_DWMAC_CORE_1_00)
			mac->link.caps = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
					 MAC_10 | MAC_100 | MAC_1000;
		else
			mac->link.caps = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
					 MAC_10FD | MAC_100FD | MAC_1000FD;
	}

	mac->link.duplex = GMAC_CONTROL_DM;
	mac->link.speed10 = GMAC_CONTROL_PS;
	mac->link.speed100 = GMAC_CONTROL_PS | GMAC_CONTROL_FES;
	mac->link.speed1000 = 0;
	mac->link.speed_mask = GMAC_CONTROL_PS | GMAC_CONTROL_FES;
	mac->mii.addr = GMAC_MII_ADDR;
	mac->mii.data = GMAC_MII_DATA;
	mac->mii.addr_shift = 11;
	mac->mii.addr_mask = 0x0000F800;
	mac->mii.reg_shift = 6;
	mac->mii.reg_mask = 0x000007C0;
	mac->mii.clk_csr_shift = 2;
	mac->mii.clk_csr_mask = GENMASK(5, 2);

	return mac;
}

static int loongson_dwmac_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct plat_stmmacenet_data *plat;
	int ret, i, bus_id, phy_mode;
	struct stmmac_pci_info *info;
	struct stmmac_resources res;
	struct loongson_data *ld;
	struct device_node *np;

	np = dev_of_node(&pdev->dev);

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return -ENOMEM;

	plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
					   sizeof(*plat->mdio_bus_data),
					   GFP_KERNEL);
	if (!plat->mdio_bus_data)
		return -ENOMEM;

	plat->dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*plat->dma_cfg), GFP_KERNEL);
	if (!plat->dma_cfg)
		return -ENOMEM;

	ld = devm_kzalloc(&pdev->dev, sizeof(*ld), GFP_KERNEL);
	if (!ld)
		return -ENOMEM;

	/* Enable pci device */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n", __func__);
		goto err_put_node;
	}

	/* Get the base address of device */
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
		if (ret)
			goto err_disable_device;
		break;
	}

	pci_set_master(pdev);

	info = (struct stmmac_pci_info *)id->driver_data;
	ret = info->setup(pdev, plat);
	if (ret)
		goto err_disable_device;

	if (np) {
		plat->mdio_node = of_get_child_by_name(np, "mdio");
		if (plat->mdio_node) {
			dev_info(&pdev->dev, "Found MDIO subnode\n");
			plat->mdio_bus_data->needs_reset = true;
		}

		bus_id = of_alias_get_id(np, "ethernet");
		if (bus_id >= 0)
			plat->bus_id = bus_id;

		phy_mode = device_get_phy_mode(&pdev->dev);
		if (phy_mode < 0) {
			dev_err(&pdev->dev, "phy_mode not found\n");
			ret = phy_mode;
			goto err_disable_device;
		}
		plat->phy_interface = phy_mode;
	}

	plat->bsp_priv = ld;
	plat->setup = loongson_dwmac_setup;
	ld->dev = &pdev->dev;

	memset(&res, 0, sizeof(res));
	res.addr = pcim_iomap_table(pdev)[0];
	ld->gmac_verion = readl(res.addr + GMAC_VERSION) & 0xff;

	switch (ld->gmac_verion) {
	case LOONGSON_DWMAC_CORE_1_00:
		plat->rx_queues_to_use = CHANNEL_NUM;
		plat->tx_queues_to_use = CHANNEL_NUM;

		/* Only channel 0 supports checksum,
		 * so turn off checksum to enable multiple channels.
		 */
		for (i = 1; i < CHANNEL_NUM; i++)
			plat->tx_queues_cfg[i].coe_unsupported = 1;

		ret = loongson_dwmac_config_msi(pdev, plat, &res, np);
		break;
	default:	/* 0x35 device and 0x37 device. */
		plat->tx_queues_to_use = 1;
		plat->rx_queues_to_use = 1;

		ret = loongson_dwmac_config_legacy(pdev, plat, &res, np);
		break;
	}

	ret = stmmac_dvr_probe(&pdev->dev, plat, &res);
	if (ret)
		goto err_disable_device;

	return ret;

err_disable_device:
	pci_disable_device(pdev);
err_put_node:
	of_node_put(plat->mdio_node);
	return ret;
}

static void loongson_dwmac_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int i;

	of_node_put(priv->plat->mdio_node);
	stmmac_dvr_remove(&pdev->dev);

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		pcim_iounmap_regions(pdev, BIT(i));
		break;
	}

	pci_disable_msi(pdev);
	pci_disable_device(pdev);
}

static int __maybe_unused loongson_dwmac_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = stmmac_suspend(dev);
	if (ret)
		return ret;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;

	pci_disable_device(pdev);
	pci_wake_from_d3(pdev, true);
	return 0;
}

static int __maybe_unused loongson_dwmac_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	pci_restore_state(pdev);
	pci_set_power_state(pdev, PCI_D0);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	return stmmac_resume(dev);
}

static SIMPLE_DEV_PM_OPS(loongson_dwmac_pm_ops, loongson_dwmac_suspend,
			 loongson_dwmac_resume);

static const struct pci_device_id loongson_dwmac_id_table[] = {
	{ PCI_DEVICE_DATA(LOONGSON, GMAC, &loongson_gmac_pci_info) },
	{ PCI_DEVICE_DATA(LOONGSON, GNET, &loongson_gnet_pci_info) },
	{}
};
MODULE_DEVICE_TABLE(pci, loongson_dwmac_id_table);

static struct pci_driver loongson_dwmac_driver = {
	.name = "dwmac-loongson-pci",
	.id_table = loongson_dwmac_id_table,
	.probe = loongson_dwmac_probe,
	.remove = loongson_dwmac_remove,
	.driver = {
		.pm = &loongson_dwmac_pm_ops,
	},
};

module_pci_driver(loongson_dwmac_driver);

MODULE_DESCRIPTION("Loongson DWMAC PCI driver");
MODULE_AUTHOR("Qing Zhang <zhangqing@loongson.cn>");
MODULE_AUTHOR("Yanteng Si <siyanteng@loongson.cn>");
MODULE_LICENSE("GPL v2");

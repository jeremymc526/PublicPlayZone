/*
 * IPROP AHCI SATA platform driver
 * Copyright 2014 NAI, Inc.
 *
 * based on the AHCI SATA platform driver by Jeff Garzik and Anton Vorontsov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/export.h>
#include "ahci.h"

#define DRV_NAME "iprop-ahci"

static const struct ata_port_info ahci_iprop_port_info = {
	.flags          = AHCI_FLAG_COMMON,
	.pio_mask       = ATA_PIO4,
	.udma_mask      = ATA_UDMA6,
	.port_ops       = &ahci_platform_ops,
};

static struct scsi_host_template ahci_iprop_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int ahci_iprop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	int rc;

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	rc = ahci_platform_enable_resources(hpriv);
	if (rc)
		return rc;

	of_property_read_u32(dev->of_node,
			     "ports-implemented", &hpriv->force_port_map);

	rc = ahci_platform_init_host(pdev, hpriv, &ahci_iprop_port_info,
				     &ahci_iprop_platform_sht);
	if (rc)
		ahci_platform_disable_resources(hpriv);
	
	return rc;
}

static SIMPLE_DEV_PM_OPS(ahci_iprop_pm_ops, ahci_platform_suspend,
			 ahci_platform_resume);

static const struct of_device_id ahci_iprop_of_match[] = {
	{ .compatible = "IntelliProp,iprop-ahci-1.0" },
	{},
};
MODULE_DEVICE_TABLE(of, ahci_iprop_of_match);

static struct platform_driver ahci_iprop_driver = {
	.probe = ahci_iprop_probe,
	.remove = ata_platform_remove_one,
	.shutdown = ahci_platform_shutdown,
        .driver = {
		.name = DRV_NAME,
                .of_match_table = ahci_iprop_of_match,
                .pm = &ahci_iprop_pm_ops,
        },
};

module_platform_driver(ahci_iprop_driver);

MODULE_DESCRIPTION("IProp AHCI SATA platform driver");
MODULE_AUTHOR("Tony Yang <tyang@naii.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("sata:iprop");

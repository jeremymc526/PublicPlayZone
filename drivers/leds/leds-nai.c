/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/io.h>
#include <linux/slab.h>


/* TODO is the hw blink rate on/off or on then off */
#define NAI_LED_BLINK_RATE_SLOW_MS      1000
#define NAI_LED_BLINK_RATE_MEDIUM_MS    250
#define NAI_LED_BLINK_RATE_FAST_MS      100
#define NAI_LED_BLINK_RATE_SOLID_MS     0

#define NAI_LED_BASE_OFFSET             0x20
#define NAI_LED_BASE_ADDR_OFFSET(i)     (NAI_LED_BASE_OFFSET + (i * 4))

#define NAI_LED_BLINK_RATE_MASK         0x00000300
#define NAI_LED_BLINK_RATE_SOLID        0x00000000
#define NAI_LED_BLINK_RATE_SLOW         0x00000100
#define NAI_LED_BLINK_RATE_MEDIUM       0x00000200
#define NAI_LED_BLINK_RATE_FAST         0x00000300

#define NAI_LED_ONESHOT                 0x00000002
#define NAI_LED_ENABLE                  0x00000001
#define NAI_LED_ENABLE_MASK				0x00000001

#define NAI_DRV_NAME_VER				"NAI,led-1.0"

struct nai_led_dev {
	struct led_classdev cdev;
	void __iomem        *led_addr;
};

static void nai_led_set(struct led_classdev *cdev, enum led_brightness b) {
	struct nai_led_dev *dev;
	u32 val;

	dev = container_of(cdev, struct nai_led_dev, cdev);
	if (b != LED_OFF) {
		/* Ensure LED is enabled, do not modify blink rate */
		val = ioread32(dev->led_addr);
		val |= NAI_LED_ENABLE;
	}
	else {
		/* Ensure LED is disabled, clear blink rate (set it to solid) */
		val = 0;
	}
	iowrite32(val, dev->led_addr);
}

static enum led_brightness nai_led_get(struct led_classdev *cdev) {
	struct nai_led_dev *dev;
	u32 val;
	
	dev = container_of(cdev, struct nai_led_dev, cdev);
	val = ioread32(dev->led_addr);
	val &= NAI_LED_ENABLE_MASK;
	cdev->brightness = val;
	
	return cdev->brightness;
}

static int nai_led_blink_set(struct led_classdev *cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off) {
	struct nai_led_dev *dev;
	u32 rate = 0;
	u32 blink;
	u32 val;
	
	dev = container_of(cdev, struct nai_led_dev, cdev);
	
	/* We do not have separate on/off rates, just use the max */
	if ( *delay_on )
		rate = *delay_on;
	
	//printk("nai_blink delay_on %ld delay_off %ld rate %d \n",*delay_on, *delay_off, rate);
	if ((rate > NAI_LED_BLINK_RATE_SOLID_MS) && (rate <= NAI_LED_BLINK_RATE_FAST_MS)) {
		*delay_on = *delay_off = NAI_LED_BLINK_RATE_FAST_MS;
		blink = NAI_LED_BLINK_RATE_FAST;
	}
	else if((rate > NAI_LED_BLINK_RATE_FAST_MS) && (rate <= NAI_LED_BLINK_RATE_MEDIUM_MS)) {
		*delay_on = *delay_off = NAI_LED_BLINK_RATE_MEDIUM_MS;
		blink = NAI_LED_BLINK_RATE_MEDIUM;
	}
	else if((rate > NAI_LED_BLINK_RATE_MEDIUM_MS) && (rate <= NAI_LED_BLINK_RATE_SLOW_MS)) {
		*delay_on = *delay_off = NAI_LED_BLINK_RATE_SLOW_MS;
		blink = NAI_LED_BLINK_RATE_SLOW;
	}
	else {
		*delay_on = *delay_off = NAI_LED_BLINK_RATE_SOLID_MS;
		blink = NAI_LED_BLINK_RATE_SOLID;
	}

	val = ioread32(dev->led_addr);
	val &= ~NAI_LED_BLINK_RATE_MASK;
	val |= blink;
	iowrite32(val, dev->led_addr);

	return 0;
}

static const struct of_device_id of_nai_leds_match[] = {
	{ .compatible = NAI_DRV_NAME_VER, },
	{},
};

static int nai_led_probe(struct platform_device *pdev) {
	struct nai_led_dev *pled;
	struct device_node *pNode, *curr_cNode;
	void __iomem *base_addr;
	int ret = 0;
	int i = 0;
	int cNodeCount = 0;
	const char *led_desc;
	struct resource regs;
	
	dev_info(&pdev->dev, "\n");
	
	//get the parent node of the compatible device
	pNode = pdev->dev.of_node;
	if(!pNode) {
		dev_err(&pdev->dev, "Unable to find compatible %s in DTB \n", NAI_DRV_NAME_VER);
		goto err;
	}

	//find number of LEDS from DTB
	cNodeCount = of_get_child_count(pNode);
	//dev_err(&pdev->dev, "Child node # %d \n", cNodeCount);
	if(!cNodeCount) {
		dev_err(&pdev->dev, "Unable to find any LED defined in DTB \n");
		goto err;
	}
	
	pled = devm_kzalloc(&pdev->dev,
			    sizeof(struct nai_led_dev) * cNodeCount,
			    GFP_KERNEL);
	if (!pled) {
		dev_err(&pdev->dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto err;
	}
	
	for_each_child_of_node(pNode, curr_cNode) {
		led_desc = of_get_property(curr_cNode, "label", NULL);
		/*dev_info(&pdev->dev, "LED Desc %s \n", led_desc);*/
		
		ret = of_address_to_resource(curr_cNode, 0, &regs);
		if (ret) {
			dev_err(&pdev->dev, "%s missing \"reg\" property\n", led_desc);
			goto err_register;
		}

		base_addr = devm_ioremap_resource(&pdev->dev, &regs);
		if (IS_ERR(base_addr)) {
			dev_err(&pdev->dev, "unable to map %s regs\n", led_desc);
			ret = PTR_ERR(base_addr);
			goto err_register;
		}

		/*dev_info(&pdev->dev, "LED %s BaseAddr %p \n",led_desc, base_addr);*/
		
		pled[i].cdev.name = led_desc;
		pled[i].led_addr = base_addr;
		pled[i].cdev.brightness_set = nai_led_set;
		pled[i].cdev.brightness_get = nai_led_get;
		pled[i].cdev.blink_set = nai_led_blink_set;
		pled[i].cdev.max_brightness = LED_FULL;
		pled[i].cdev.brightness = LED_OFF;
		/* Default to off, blink rate is solid */
		/* Removed the default OFF state */
		/*iowrite32(0, pled[i].led_addr);*/
		
		ret = led_classdev_register(pdev->dev.parent, &pled[i].cdev);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to register %s led, err=%d\n",
				pled[i].cdev.name, ret);
			goto err_register;
		}
		++i;
	}

	platform_set_drvdata(pdev, pled);
	
	return 0;

err_register:
	while (--i >= 0) {
		led_classdev_unregister(&pled[i].cdev);
	}
	kfree(pled);
err:
	return ret;
}

static int nai_led_remove(struct platform_device *pdev) {
	struct nai_led_dev *pled = platform_get_drvdata(pdev);
	struct led_platform_data *pdata  = pdev->dev.platform_data;
	int i;

	for (i = 0; i < pdata->num_leds; i++) {
		pled[i].cdev.brightness = LED_OFF;
		nai_led_set(&pled[i].cdev, pled[i].cdev.brightness);
		led_classdev_unregister(&pled[i].cdev);
	}
	platform_set_drvdata(pdev, NULL);
	kfree(pled);

	return 0;
}

static struct platform_driver nai_led_driver = {
	.driver		= {
		.name	= NAI_DRV_NAME_VER,
		.owner	= THIS_MODULE,	
		.of_match_table = of_match_ptr(of_nai_leds_match),
	},
	.probe		= nai_led_probe,
	.remove		= nai_led_remove,
};

module_platform_driver(nai_led_driver);

MODULE_AUTHOR("North Atlantic Industries Inc");
MODULE_DESCRIPTION("LED driver for NAI Board");
MODULE_LICENSE("GPL");

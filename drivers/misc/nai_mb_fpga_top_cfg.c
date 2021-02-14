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
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <asm/device.h>
#include <linux/semaphore.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/miscdevice.h>
#include <linux/mod_devicetable.h>
#include <linux/fs.h>
#include <linux/delay.h>

#include "nai_mb_fpga_top_cfg.h"
								
#define NAI_DRV_NAME_VER       			"NAI,mb-fpga-top-cfg-1.0"
#define NAI_HPS_READY_OFFSET            0x04
#define NAI_HPS_READY_ALL_READY         0x80000000

#define NAI_RESET_DURATION_OFFSET       0x08

#define NAI_RESETS_OFFSET               0x0C
#define NAI_RESETS_MODULE_1             0x00000200

#define NAI_MODULE_ADDR_OFFSET_1        0x00
#define NAI_MODULE_ADDR_MASK            0x03FF
#define NAI_MODULE_ADDR_START_SHIFT     18
#define NAI_MODULE_ADDR_END_SHIFT       2
#define NAI_MODULE_ADDR_OFFSET(m)       (NAI_MODULE_ADDR_OFFSET_1 + (m * 8))

#define NAI_MODULE_MASK_OFFSET_1        0x04
#define NAI_MODULE_MASK_MASK            0x00FFFFFF
#define NAI_MODULE_MASK_OFFSET(m)       (NAI_MODULE_MASK_OFFSET_1 + (m * 8))

#define NAI_ALL_MODULES_MASK_OFFSET     0x38 //module_addr baseaddr (0x0000_0030) + 0x38 = 0x0000_0068

struct nai_common_dev {
	struct miscdevice miscdev;
	void __iomem      *revision_base;
	void __iomem      *ps_ready_base;
	void __iomem      *ps_reset_base;
	void __iomem      *module_addr_config_base;
	void __iomem      *mb_common_base;
	u32               mb_common_size;
	
	struct semaphore  sem;
};

static int nai_common_open(struct inode *inode, struct file *filp) {
	/* Open must be implemented to force the miscdevice open
	 * function to store dev pointer in the file private data.
	 */
	return 0;
}

static int nai_common_release(struct inode *inode, struct file *filp) {
	return 0;
}

static long get_params(struct nai_common_dev *dev, u32 mode,
		       void __iomem **base_addr, u32 *size) {
	long ret = 0;

	switch (mode) {
	case NAI_COMMON_MODE_COMMON:
		*base_addr = dev->mb_common_base;
		*size = dev->mb_common_size;
		break;
	default:
		ret = -EINVAL;
		break;
	}


	return ret;
}

static long _rdwr_reg(nai_common_reg __user *arg, u32 rd,
		      void __iomem *base_addr, u32 size) {
	long ret = 0;
	u32 offset;
	u32 type;
	u32 end;

	(void)get_user(offset, &arg->offset);
	(void)get_user(type, &arg->type);

        switch(type) {
	case NAI_COMMON_TYPE_8_BIT:
		end = size - sizeof(u8);
		if (offset <= end) {
			u8 val;

			if (rd) {
				val = ioread8(base_addr + offset);
				(void)put_user(val, &arg->val8);
			}
			else {
				(void)get_user(val, &arg->val8);
				iowrite8(val, base_addr + offset);
			}
		}
		else
			ret = -EINVAL;
		break;

	case NAI_COMMON_TYPE_16_BIT:
		end = size - sizeof(u16);
		if (!(offset & 0x01) && (offset <= end)) {
			u16 val;

			if (rd) {
				val = ioread16(base_addr + offset);
				(void)put_user(val, &arg->val16);
			}
			else {
				(void)get_user(val, &arg->val16);
				iowrite16(val, base_addr + offset);
			}
		}
		else
			ret = -EINVAL;
		break;

	case NAI_COMMON_TYPE_32_BIT:
		end = size - sizeof(u32);
		if (!(offset & 0x03) && (offset <= end)) {
			u32 val;

			if (rd) {
				val = ioread32(base_addr + offset);
				(void)put_user(val, &arg->val32);
			}
			else {
				(void)get_user(val, &arg->val32);
				iowrite32(val, base_addr + offset);
			}
		}
		else
			ret = -EINVAL;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static long rdwr_reg(struct nai_common_dev *dev,
		     nai_common_reg __user *arg, u32 rd) {
	long ret = 0;
	u32 mode;
	void __iomem *base_addr;
	u32 size;

	(void)get_user(mode, &arg->mode);

	ret = get_params(dev, mode, &base_addr, &size);

	if (likely(!ret))
		ret = _rdwr_reg(arg, rd, base_addr, size);

	return ret;
}

static long _rdwr_blk(nai_common_blk __user *arg, u32 rd,
		      void __iomem *base_addr, u32 size) {
	long ret = 0;
	u32 offset;
	u32 type;
	u32 count;
	u32 status;
	void __user *user_val = NULL;
	void *val = NULL;
	u32 len;
	u32 i;

	(void)get_user(offset, &arg->offset);
	(void)get_user(type, &arg->type);
	(void)get_user(count, &arg->count);
	(void)get_user(user_val, &arg->val);

	switch (type) {
	case NAI_COMMON_TYPE_8_BIT:
		len = count * sizeof(u8);
		break;

	case NAI_COMMON_TYPE_16_BIT:
		len = count * sizeof(u16);
		if (offset & 0x01)
			ret = -EINVAL;
		break;

	case NAI_COMMON_TYPE_32_BIT:
		len = count * sizeof(u32);
		if (offset & 0x03)
			ret = -EINVAL;
		break;

	default:
		ret = -EINVAL;
	}

	if ((offset + len) > size) {
		ret = -EINVAL;
	}

	if (unlikely(ret))
		goto exit;

	ret = !access_ok(VERIFY_WRITE, user_val, len);
	if (unlikely(ret)) {
		ret = -EFAULT;
		goto exit;
	}

	val = kmalloc(len, GFP_KERNEL);
	if (unlikely(!val)) {
		ret = -ENOMEM;
		goto exit;
	}

	if (!rd)
		status = copy_from_user(val, user_val, len);

        switch(type ) {
	case NAI_COMMON_TYPE_8_BIT:
		if (rd)
			for (i = 0; i < len; ++i)
				*(u8 *)(val + i) =
					ioread8(base_addr + offset + i);
		else
			for (i = 0; i < len; ++i)
				iowrite8(*(u8 *)(val + i),
					 base_addr + offset + i);
		break;

	case NAI_COMMON_TYPE_16_BIT:
		if (rd)
			for (i = 0; i < len; i += 2)
				*(u16 *)(val + i) =
					ioread16(base_addr + offset + i);
		else
			for (i = 0; i < len; i += 2)
				iowrite16(*(u16 *)(val + i),
					  base_addr + offset + i);
		break;

	case NAI_COMMON_TYPE_32_BIT:
		if (rd)
			for (i = 0; i < len; i += 4)
				*(u32 *)(val + i) =
					ioread32(base_addr + offset + i);
		else
			for (i = 0; i < len; i += 4)
				iowrite32(*(u32 *)(val + i),
					  base_addr + offset + i);

	default: /* Will never happen */
		break;
	}

	if (rd)
		status = copy_to_user(user_val, val, len);

	kfree(val);
exit:
	return ret;
}

static long rdwr_blk(struct nai_common_dev *dev,
		     nai_common_blk __user *arg, u32 rd) {
	long ret = 0;
	u32 mode;
	void __iomem *base_addr;
	u32 size;

	(void)get_user(mode, &arg->mode);

	ret = get_params(dev, mode, &base_addr, &size);

	if (likely(!ret))
		ret = _rdwr_blk(arg, rd, base_addr, size);

	return ret;
}

static long get_fw_revs(struct nai_common_dev *dev,
			nai_firmware_revs __user *arg) {
	long ret = 0;
	int i;
	u32 val;

	for (i = 0; i < NAI_COMMON_NUM_FW_REVS; ++i) {
		val = ioread32(dev->revision_base + (i * sizeof(u32)));
		(void)put_user(val, &arg->revisions[i]);
	}

	return ret;
}

static long rdwr_hps_ready(struct nai_common_dev *dev,
			   u32 __user *arg, u32 rd) {
	long ret = 0;
	u32 val;
	u32 reg;

	if (rd) {
		reg = ioread32(dev->ps_ready_base);
		val = (reg & NAI_HPS_READY_ALL_READY) ? 1 : 0;

		(void)put_user(val, arg);
	}
	else {
		(void)get_user(val, arg);

		ret = down_interruptible(&dev->sem);
		if (unlikely(ret)) {
			ret = -ERESTARTSYS;
			goto exit;
		}

		val = val ? NAI_HPS_READY_ALL_READY : 0;
		reg = ioread32(dev->ps_ready_base);
		reg &= ~NAI_HPS_READY_ALL_READY;
		reg |= val;

		iowrite32(val, dev->ps_ready_base);

		up(&dev->sem);
	}
exit:
	return ret;
}

long rdwr_module_addr(struct nai_common_dev *dev,
		      nai_module_addr __user *arg, u32 rd) {
	long ret = 0;
	u32 module_id;
	u16 start;
	u16 end;
	u32 mask;
	u32 addr;

	(void)get_user(module_id, &arg->module_id);

	if (module_id > NAI_COMMON_MODULE_7 || module_id < 0) {
		ret = -EINVAL;
		goto exit;
	}

	if (rd) {
		addr = ioread32(dev->module_addr_config_base +
				NAI_MODULE_ADDR_OFFSET(module_id));
		start = (addr >> NAI_MODULE_ADDR_START_SHIFT) &
			NAI_MODULE_ADDR_MASK;
		end = (addr >> NAI_MODULE_ADDR_END_SHIFT) &
			NAI_MODULE_ADDR_MASK;
		mask = ioread32(dev->module_addr_config_base +
				NAI_MODULE_MASK_OFFSET(module_id));

		(void)put_user(start, &arg->start);
		(void)put_user(end, &arg->end);
		(void)put_user(mask, &arg->mask);
	}
	else {
		(void)get_user(start, &arg->start);
		(void)get_user(end, &arg->end);
		(void)get_user(mask, &arg->mask);
/* NAI: Remove the error checking for the input values from userspace
 * 0 is a valid value for start , end  and mask
 */
#if 0
		if (!(start & ~NAI_MODULE_ADDR_MASK) ||
		    !(end   & ~NAI_MODULE_ADDR_MASK) ||
		    !(mask  & ~NAI_MODULE_MASK_MASK)) {
			ret = -EINVAL;
			goto exit;
		}
#endif		
		/* TODO does end need to be or'ed with 0x03? */
		addr = (start << NAI_MODULE_ADDR_START_SHIFT) |
		       (end  << NAI_MODULE_ADDR_END_SHIFT) |
			0x03;
		iowrite32(addr, dev->module_addr_config_base +
			  NAI_MODULE_ADDR_OFFSET(module_id));
		iowrite32(mask, dev->module_addr_config_base +
			  NAI_MODULE_MASK_OFFSET(module_id));
	}
exit:
	return ret;
}

static long rdwr_all_modules_mask(struct nai_common_dev *dev,
				  u32 __user *arg, u32 rd) {
	long ret = 0;
	u32 val;

	if (rd) {
		val = ioread32(dev->module_addr_config_base +
			       NAI_ALL_MODULES_MASK_OFFSET);
		(void)put_user(val, arg);
	}
	else {
		(void)get_user(val, arg);
		if (!(val & ~NAI_MODULE_MASK_MASK))
			iowrite32(val, dev->module_addr_config_base +
				  NAI_ALL_MODULES_MASK_OFFSET);
		else
			ret = -EINVAL;
	}

	return ret;
}

static long nai_common_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg) {
	struct nai_common_dev *dev;
	long ret = 0;
	u32 rd = 0;

	dev = container_of(filp->private_data, struct nai_common_dev, miscdev);

	if (_IOC_TYPE(cmd) != NAI_COMMON_MAGIC) {
		ret = -ENOTTY;
		goto exit;
	}

	if (_IOC_DIR(cmd) & _IOC_READ)
		ret = !access_ok(VERIFY_WRITE, (void __user *)arg,
				 _IOC_SIZE(cmd));
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
		ret = !access_ok(VERIFY_READ, (void __user *)arg,
				 _IOC_SIZE(cmd));
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	switch (cmd) {
	case NAI_IOC_COMMON_RD_REG:
		rd = 1; /* Intentional dropthrough */
	case NAI_IOC_COMMON_WR_REG:
		ret = rdwr_reg(dev, (nai_common_reg __user *)arg, rd);
		break;

	case NAI_IOC_COMMON_RD_BLK:
		rd = 1; /* Intentional dropthrough */
	case NAI_IOC_COMMON_WR_BLK:
		ret = rdwr_blk(dev, (nai_common_blk __user *)arg, rd);
		break;

	case NAI_IOC_COMMON_GET_FW_REVS:
	        ret = get_fw_revs(dev, (nai_firmware_revs __user *)arg);
		break;

	case NAI_IOC_COMMON_GET_HPS_READY:
		rd = 1; /* Intentional dropthrough */
	case NAI_IOC_COMMON_SET_HPS_READY:
		ret = rdwr_hps_ready(dev, (u32 __user *)arg, rd);
		break;

	case NAI_IOC_COMMON_GET_MODULE_ADDR:
		rd = 1; /* Intentional dropthrough */
	case NAI_IOC_COMMON_SET_MODULE_ADDR:
		rdwr_module_addr(dev, (nai_module_addr __user *)arg, rd);
		break;
	case NAI_IOC_COMMON_GET_ALL_MODULES_MASK:
		rd = 1; /* Intentioanl dropthrough */
	case NAI_IOC_COMMON_SET_ALL_MODULES_MASK:
		rdwr_all_modules_mask(dev, (u32 __user *)arg, rd);
		break;
	default:
		ret = -ENOTTY;
		break;
	}
exit:
	return ret;
}

static const struct file_operations nai_common_fops = {
	.open           = nai_common_open,
	.release        = nai_common_release,
	.unlocked_ioctl = nai_common_ioctl,
	.compat_ioctl   = nai_common_ioctl,
};

static const struct of_device_id of_nai_mb_fgpa_top_cfg_match[] = {
	{ .compatible = NAI_DRV_NAME_VER, },
	{},
};

static int nai_common_probe(struct platform_device *pdev) {
	int ret = 0;
	struct nai_common_dev *dev = NULL;
	struct resource res;
	struct device_node *pNode, *cNode;
	
	dev_info(&pdev->dev, "\n");
	
	/* Allocate and initialize device */
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto exit;
	}
	memset(dev, 0, sizeof(*dev));

	sema_init(&dev->sem, 1);

	/* Initialize and register miscdevice */
	dev->miscdev.name = NAI_MISC_DEV_NAME;
	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev->miscdev.fops = &nai_common_fops;
	ret = misc_register((struct miscdevice *)dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to allocate misc device\n");
		goto err_misc;
	}

	//get the parent node of the compatible device
	pNode = of_find_compatible_node(NULL, NULL, NAI_DRV_NAME_VER);
	if(!pNode) {
		dev_err(&pdev->dev, "Unable to find compatible %s in DTB \n", NAI_DRV_NAME_VER);
		goto err_find_comp_node;
	}
	
	cNode = of_get_child_by_name(pNode, "top-rev");
	if(cNode != NULL) {
		
		dev->revision_base = of_iomap(cNode,0);
		if (!dev->revision_base) {
			dev_err(&pdev->dev, "Failed to map revision registers\n");
			ret = -ENOMEM;
			goto err_top_rev_res;
		}
		//dev_info(&pdev->dev, "rev %p \n", dev->revision_base);
		
	} else {
		goto err_top_rev_res;
	}
	
	/* Map ps ready config memory */
	cNode = of_get_child_by_name(pNode, "ps-ready");
	if(cNode != NULL) {
		
		dev->ps_ready_base = of_iomap(cNode,0);
		if (!dev->ps_ready_base) {
			dev_err(&pdev->dev, "Failed to map ps-ready registers\n");
			ret = -ENOMEM;
			goto err_ps_ready_res;
		}
		//dev_info(&pdev->dev, "ready %p \n", dev->ps_ready_base);
	} else {
		goto err_ps_ready_res;
	}
	
	/* Map ps reset config memory */
	cNode = of_get_child_by_name(pNode, "ps-reset");
	if(cNode != NULL) {
		
		dev->ps_reset_base = of_iomap(cNode,0);
		if (!dev->ps_reset_base) {
			dev_err(&pdev->dev, "Failed to map ps-reset registers\n");
			ret = -ENOMEM;
			goto err_ps_reset_res;
		}
		//dev_info(&pdev->dev, "reset %p \n", dev->ps_reset_base);
	} else {
		goto err_ps_reset_res;
	}	
	
	/* Map Module Address config memory */
	cNode = of_get_child_by_name(pNode, "module-addr-config");
	if(cNode != NULL) {
		
		dev->module_addr_config_base = of_iomap(cNode,0);
		if (!dev->module_addr_config_base) {
			dev_err(&pdev->dev, "Failed to map module-addr-config registers\n");
			ret = -ENOMEM;
			goto err_module_addr_config_res;
		}
		//dev_info(&pdev->dev, "module addr %p \n", dev->module_addr_config_base);
	} else {
		goto err_module_addr_config_res;
	}

	/* Map Motherboard common area memory */
	cNode = of_get_child_by_name(pNode, "mb-common");
	if(cNode != NULL) {
		
	
		dev->mb_common_base = of_iomap(cNode,0);
		of_address_to_resource(cNode, 0, &res);
		dev->mb_common_size = resource_size(&res);
		if (!dev->mb_common_base) {
			dev_err(&pdev->dev, "Failed to map mb-common registers\n");
			ret = -ENOMEM;
			goto err_mb_common_res;
		}
		//dev_info(&pdev->dev, "mb_common_base %p \n", dev->mb_common_base);
		//dev_info(&pdev->dev, "mb_common size %d \n", dev->mb_common_size);
	} else {
		goto err_mb_common_res;
	}
					
	/* Save misc device in platform dev */
	platform_set_drvdata(pdev, dev);

	return 0;
	
err_mb_common_res:
	iounmap(dev->module_addr_config_base);
err_module_addr_config_res:
	iounmap(dev->ps_reset_base);
err_ps_reset_res:
	iounmap(dev->ps_ready_base);
err_ps_ready_res:
	iounmap(dev->revision_base);
err_top_rev_res:
err_find_comp_node:
	misc_deregister((struct miscdevice *)dev);
err_misc:
	kfree(dev);
exit:
	return ret;
}

static int nai_common_remove(struct platform_device *pdev) {
	struct nai_serdes_dev *dev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	misc_deregister((struct miscdevice *)dev);
	kfree(dev);

	return 0;
}

static struct platform_driver nai_common_drv = {
	.driver   = {
		.name  = NAI_DRV_NAME_VER,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(of_nai_mb_fgpa_top_cfg_match)
	},
	.probe    = nai_common_probe,
	.remove   = nai_common_remove,
};

static int __init nai_common_init(void) {
	return platform_driver_register(&nai_common_drv);
}


static void __exit nai_common_exit(void) {
	platform_driver_unregister(&nai_common_drv);
}

module_init(nai_common_init);
module_exit(nai_common_exit);

MODULE_AUTHOR("Obi Okafor <ookafor@naii.com>");
MODULE_DESCRIPTION("NAI Common driver");
MODULE_LICENSE("GPL");

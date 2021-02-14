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
//#define pr_fmt(fmt) "%s:%d:: " fmt, KBUILD_MODNAME,  __LINE__
#define pr_fmt(fmt) "NAI %s:%s:%d::" fmt, strrchr(__FILE__,'/'), __func__, __LINE__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <asm/barrier.h>

#include "nai-ep-shrm.h"

//#define DEBUG_SHRM

#define DRV_NAME             		"nai-ep-shrm"
#define DRV_VER              		"v1.0"


#define FPGA_PCIE_DDR_MAP_OFFSET       	0x00000000
#define FPGA_PCIE_DDR_MSK_OFFSET       	0x00000004
#define FPGA_VME_CPCI_DDR_MAP_OFFSET   	0x00000000
#define FPGA_VME_CPCI_DDR_MSK_OFFSET   	0x00000004


#define IRQ_2_BIT_SHIFT                	2
#define MOD7_BIT0_IRQ_BIT_SHIFT        	28


/* TODO: Get memory alloc size from DTB */
/* if 1M, Failed To Allocated Contiguous Memory for CFP_KERNEL flag */
#define MEM_ALLOC_SIZE                 	4 * 1024 * 1024 
#define DDR_SHRM_MASK                  	1 * 1024 * 1024 - 1
#define EP_SHRM_MINOR                  	0

struct ep_shrm_drv {
   	/* Kernel virtual base address of the ring memory. */
   	void __iomem        *fpga_pcie_ddr_base_addr;
   	void __iomem        *fpga_vme_cpci_ddr_base_addr;
   	void __iomem        *fpga_irq_clr;
   	void __iomem        *fpga_mod7_vec_addr;
   	void __iomem        *fpga_mod7_steer_addr;
   	void __iomem        *fpga_mod7_ack_irq;
   	void                *virt_addr;
	void	  	    *phy_addr; 
  	void                *kern_buf;
   	char                cdev_name[16];
   	unsigned int        cdev_major;
   	unsigned int        cdev_minor;
   	struct device       *parent;
   	struct class        *ep_shrm_sysfs_class;   /* Sysfs class */
   	struct device       *ep_shrm_sysfs_device;  /* Sysfs device */
   	struct cdev         cdev;
   	int                 irq;
   	unsigned char       ackirqsteer;
   	unsigned int        opened;
   	atomic_t            counter;
   	size_t              mem_size;
   	dma_addr_t          dma;
   	spinlock_t          rx_lock;
   	wait_queue_head_t   rx_waitq;
};

struct ep_shrm_sysfs_entry {
  	struct kobj_attribute kobj_attr;
   	struct ep_shrm_drv    *drv;
   	int                   genIrq;
};

static const char driver_name[] = DRV_NAME;
static DEFINE_MUTEX(minors_lock);


static void printk_hex_info(unsigned char* hex_info, unsigned int info_size) {
   	const int column_num = 16;
	/* including 2 bytes hex num and 1 byte field delimiter ',' */
   	const int element_size = 3;
	/* sizeof(int) is used for last str delimiter '\0' and mem alignment */
   	char work_buf[element_size * column_num + sizeof(int)];
   	unsigned int i;

   	memset(work_buf, 0, sizeof(work_buf));
   	for (i = 0; i < info_size; i++) {
      		sprintf(work_buf + ((i * element_size) % (column_num * element_size)),
			"%02x,", hex_info[i]);
      		if ((i == (info_size - 1)) || ((i + 1) % column_num == 0)) {
         		printk(KERN_INFO "%s\n", work_buf);
         		memset(work_buf, 0, sizeof(work_buf));
      		}
   	}
}

static void gen_ack_irq(struct ep_shrm_sysfs_entry *entry) {
   
   	struct ep_shrm_sysfs_entry *priv = entry;
   	unsigned int ret = 0;
   
   	/* we are using reserved module 7 to trigger IRQ over cPCI/PCIe/VME */
   	/* trigger ack irq */
   	/* TODO: vector data is not been use, hardward code to bit0 */
   	/* set up module 7 bit0 vector */
   	iowrite32(0, priv->drv->fpga_mod7_vec_addr);
   	/* set up module 7 bit0 steering */
   	iowrite32(priv->drv->ackirqsteer, priv->drv->fpga_mod7_steer_addr);
   	/* trigger module 7 bit0 ack irq */
   	ret = ioread32(priv->drv->fpga_mod7_ack_irq);
   	ret |= (1 << MOD7_BIT0_IRQ_BIT_SHIFT);
   	iowrite32(ret, priv->drv->fpga_mod7_ack_irq);
   
  	return;
}

static ssize_t gen_irq_store (struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t count) {
   	int irqgen;
   	struct ep_shrm_sysfs_entry *entry;
   	int ret;

   	entry = container_of(attr, struct ep_shrm_sysfs_entry, kobj_attr);

   	ret = kstrtoint(buf, 0, &irqgen);
   
   	if (ret)
     	 	return ret;
   
   	irqgen = !!irqgen;

   	if (irqgen == entry->genIrq)
      		return count;

   	if (irqgen > 0) {
      		gen_ack_irq(entry);
   	}

   	entry->genIrq = irqgen;

   	return count;
}

static struct kobj_attribute int_genirq_attribute = {
      .attr = { .name = "genirq", .mode = 0222 },
      .store = gen_irq_store,
};

static int ep_shrm_open(struct inode *inode, struct file *filp) {
   	struct ep_shrm_drv *drv = container_of(inode->i_cdev,
		struct ep_shrm_drv, cdev);
	int err = 0;

   	filp->private_data = drv;

   	mutex_lock(&minors_lock);
   
   	/* TODO: Currently we only allow one I/O open */
   	if (drv->opened >= 1) {
      		err = -EBUSY;
      		goto error;
   	}
   
   	if (drv->opened <= 0) {
      		drv->kern_buf = kzalloc(drv->mem_size, GFP_KERNEL);
      		if(!drv->kern_buf) {
         		printk("Failed to allocated kern buf \n");
		        err = -ENOMEM;
         		goto error;
      		}   
   	}
   
   	drv->opened++;
error:
	mutex_unlock(&minors_lock);      
   
  	return err;
}

static int ep_shrm_release(struct inode *inode, struct file *filp) {
   	struct ep_shrm_drv *drv = filp->private_data;
   
   	mutex_lock(&minors_lock);
   
   	if((drv->opened == 1) && drv->kern_buf)
         	kfree(drv->kern_buf);

   	if(drv->opened > 0)
      		drv->opened--;
   
   	mutex_unlock(&minors_lock);
   
   	return 0;
}

static loff_t ep_shrm_llseek(struct file *filp, loff_t off, int whence) {
   	struct ep_shrm_drv *drv = filp->private_data;
   	loff_t absolute = -1;

   	/*printk("ep_shrm_llseek offset %llx\n",off);*/
   
   	mutex_lock(&minors_lock);   
   
   	switch (whence) {
     	case SEEK_SET:
         	absolute = off;
         	break;

      	case SEEK_CUR:
         	absolute = filp->f_pos + off;
         	break;

      	case SEEK_END:
         	absolute = drv->mem_size + off;
         	break;

      	default:
      		absolute = -EINVAL;
     		goto error;
   	}

   	if ((absolute < 0) || (absolute >= drv->mem_size)) {
      		absolute = -EINVAL;
     		goto error;
   	}

   	filp->f_pos = absolute;

   	/*printk("ep_shrm_llseek offset %llx\n", filp->f_pos);*/
error:
   	mutex_unlock(&minors_lock);   
   	return absolute;
}

static ssize_t ep_shrm_read(struct file *filp, char __user *buf,
		            size_t count, loff_t *ppos) {
   	struct ep_shrm_drv *drv = filp->private_data;
   	int err = 0;
   	ssize_t okcount = 0;
   	loff_t offset = *ppos;
   
   	mutex_lock(&minors_lock);   
#ifdef DEBUG_SHRM
//pr_info("file name:\"%s\",minor:%d\n",filp->f_path.dentry->d_iname,iminor(filp->f_path.dentry->d_inode));
#endif
   	/* Ensure we are starting at a valid location */
   	if ((*ppos < 0) || 
       	    (*ppos > (drv->mem_size - 1)) || 
      	    (!drv->kern_buf)) {
      		err = -EFAULT;
      		goto error;
   	}
   
   	/* Ensure not reading past end of the image */
   	if (*ppos + count > drv->mem_size) {
      		okcount = drv->mem_size - *ppos;
   	}
   	else {
      		okcount = count;
   	}
      
   	offset = *ppos;

   	memcpy_fromio(drv->kern_buf, drv->virt_addr+offset, okcount);

   	err = copy_to_user(buf, drv->kern_buf, okcount);
	
	// Hardware memory barrier
	mb();
	if (err) {
      		okcount = 0;
   	}
   	else {
       		*ppos += okcount;
   	}

   	mutex_unlock(&minors_lock);
   
   	return err ? -EFAULT : okcount;
error:
   	mutex_unlock(&minors_lock);
   	return err;
}

static ssize_t ep_shrm_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *ppos) {
   	struct ep_shrm_drv *drv = filp->private_data;
   	int err = 0;
   	ssize_t okcount = 0;
   	loff_t offset = *ppos;
#ifdef DEBUG_SHRM
   pr_info("file name:\"%s\",minor:%d\n",filp->f_path.dentry->d_iname,iminor(filp->f_path.dentry->d_inode));
#endif
   	mutex_lock(&minors_lock);
   
   	/* Ensure we are starting at a valid location */
   	if ((*ppos < 0) || 
      	    (*ppos > (drv->mem_size - 1)) || 
     	    (!drv->kern_buf)) {
      		err = -EFAULT;
      		goto error;
   	}
      
   	/* Ensure not reading past end of the image */
   	if (*ppos + count > drv->mem_size) {
      		okcount = drv->mem_size - *ppos;
   	}
   	else {
      		okcount = count;
   	}

   	/* on failure, set the len to 0 to return empty packet to the device */
	err = copy_from_user(drv->kern_buf, buf, okcount);
   	if (err) {
      		okcount = 0;
   	}
   	else {
       		*ppos+=okcount;
   	}
   
   	memcpy_toio(drv->virt_addr+offset, drv->kern_buf, okcount);
   	mb();
	mutex_unlock(&minors_lock);
   
   	return err ? -EFAULT : okcount;
error:
   	mutex_unlock(&minors_lock);   
   	return err;
}

static unsigned int ep_shrm_poll(struct file *filp, poll_table *wait) {
   	struct ep_shrm_drv *drv = filp->private_data;
   	unsigned int mask = 0;
   
   	poll_wait(filp, &drv->rx_waitq, wait);
   
   	if(atomic_read(&drv->counter)) {
      		mask = POLLIN | POLLRDNORM;
      		atomic_set(&drv->counter, 0);
   	}
   
   	/*TODO: Do we need to return timeout poll to user app*/
   	return mask;
}

static void ep_shrm_clr_irq(struct ep_shrm_drv *data) {
   	struct ep_shrm_drv *drv = data;
   	u32 clear_irq_data = 0; 
   
   	/*clear FPGA2PS irq 2*/
   	clear_irq_data |= (1 << IRQ_2_BIT_SHIFT);
   	iowrite32(clear_irq_data, drv->fpga_irq_clr);   
   
  	return;
}

static irqreturn_t ep_shrm_isr(int irq, void *data) {
   	struct ep_shrm_drv *drv = data;
   
   	spin_lock(&drv->rx_lock);
   
   	/*inc count*/
   	atomic_inc(&drv->counter);
   
   	/*wakeup waiting event*/
   	wake_up_interruptible(&drv->rx_waitq);

   	/*clear irq*/
   	ep_shrm_clr_irq(drv);
   
   	spin_unlock(&drv->rx_lock);
      
   	return IRQ_HANDLED;
}

static int ep_shrm_dtb(struct ep_shrm_drv *drv) {
   	int result = 0;
   	int irq = 0;
   	unsigned char ackirqsteer = 0;
   	struct device_node *parentnode, *childnode;
   	struct ep_shrm_drv *privdrv = drv; 
   	void __iomem *tmp;
   
   	parentnode = of_find_compatible_node(NULL, NULL, "nai,nai-ep-shrm-v0.1");	
   
   	if (!parentnode) {
      		dev_warn(privdrv->parent, "failed to find dt file\n");
      		result = -EAGAIN;
      		goto err_dtb_parent;
   	}
   
   	/* read master slot from dtb property */
   	result = of_property_read_u8(parentnode, "ack-irq-steer", &ackirqsteer);
   	if (result < 0) {
      		dev_warn(privdrv->parent, "failed to find ack-irq-steer\n");
      		result = -EAGAIN;
      		goto err_dtb_ack_steer;
   	}
   
   	privdrv->ackirqsteer = ackirqsteer;
      
   	/* get IRQ */
   	irq = irq_of_parse_and_map(parentnode, 0);
   
   	if (irq == 0) {
      		dev_warn(privdrv->parent, "failed to find IRQ in dtb\n");
      		result = -EAGAIN;
      		goto err_dtb_irq;
   	}
   
   	privdrv->irq = irq;
      
   	/* request irq  */
   	result = request_irq(privdrv->irq, ep_shrm_isr, IRQF_SHARED, DRV_NAME, privdrv);
   	if (result) {
      		dev_warn(privdrv->parent, "Failed to allocate request irq\n");
      		goto err_req_irq;
   	}
      
   	/* map fpga register */
   	childnode = of_get_child_by_name(parentnode, "pcie_ddr_base");
   	if (childnode != NULL) {
       		tmp = of_iomap(childnode, 0);
   
       		if (!tmp) {
          		dev_warn(privdrv->parent, "failed iomap pcie_ddr_base\n");
          		result = -ENOMEM;
          		goto err_dtb_iomap_pcie_ddr_base;
       		}
        	privdrv->fpga_pcie_ddr_base_addr = tmp;
   	}
  	else {
        	privdrv->fpga_pcie_ddr_base_addr = NULL;
   	}
#ifdef DEBUG_SHRM
	pr_info("privdrv->fpga_pcie_ddr_base_addr=0x%p\n",
		privdrv->fpga_pcie_ddr_base_addr);
#endif
   	/* map fpga register */
   	childnode = of_get_child_by_name(parentnode, "vme_cpci_ddr_base");
   	if (childnode != NULL) {
       		tmp = of_iomap(childnode, 0);
      
       		if (!tmp) {
           		dev_warn(privdrv->parent, "failed iomap vme_cpci_ddr_base\n");
           		result = -ENOMEM;
           		goto err_dtb_iomap_vme_cpci_ddr_base;
       		}
   
       		privdrv->fpga_vme_cpci_ddr_base_addr = tmp;
   	}
   	else {
       		privdrv->fpga_vme_cpci_ddr_base_addr = NULL;
   	}
#ifdef DEBUG_SHRM
	pr_info("privdrv->fpga_vme_cpci_ddr_base_addr=0x%p\n",
	      	privdrv->fpga_vme_cpci_ddr_base_addr);
#endif
   	/* map fpga register */          
   	childnode = of_get_child_by_name(parentnode, "fpga_irq_clear_reg");
      
   	tmp = of_iomap(childnode, 0);
   
   	if (!tmp) {
      		dev_warn(privdrv->parent, "failed iomap fpga_irq_clear_reg\n");
      		result = -ENOMEM;
      		goto err_dtb_iomap_fpga_irq_clear_reg;
   	}
   
   	privdrv->fpga_irq_clr = tmp;
   
   	/* map fpga register */          
   	childnode = of_get_child_by_name(parentnode,"fpga_mod7_vec_addr");
      
   	tmp = of_iomap(childnode,0);
   
   	if (!tmp) {
      		dev_warn(privdrv->parent, "failed iomap fpga_mod7_vec_addr\n");
      		result = -ENOMEM;
      		goto err_dtb_iomap_fpga_mod7_vec_addr;
   	}
   
   	privdrv->fpga_mod7_vec_addr = tmp;
   
   	/* map fpga register */          
   	childnode = of_get_child_by_name(parentnode, "fpga_mod7_steer_addr");
      
   	tmp = of_iomap(childnode,0);
   
   	if (!tmp) {
      		dev_warn(privdrv->parent, "failed iomap fpga_mod7_steer_addr\n");
      		result = -ENOMEM;
      		goto err_dtb_iomap_fpga_mod7_steer_addr;
  	}
   
   	privdrv->fpga_mod7_steer_addr = tmp;
   
   	/* map fpga register */          
   	childnode = of_get_child_by_name(parentnode, "fpga_mod7_ack_irq");
      
   	tmp = of_iomap(childnode,0);
   
   	if (!tmp) {
      		dev_warn(privdrv->parent, "failed iomap fpga_mod7_ack_irq\n");
      		result = -ENOMEM;
      		goto err_dtb_iomap_fpga_mod7_ack_irq;
   	}
   
   	privdrv->fpga_mod7_ack_irq = tmp;
   
   	return 0;
   
err_dtb_iomap_fpga_mod7_ack_irq:
   	iounmap(privdrv->fpga_mod7_steer_addr);
err_dtb_iomap_fpga_mod7_steer_addr:
   	iounmap(privdrv->fpga_mod7_vec_addr);   
err_dtb_iomap_fpga_mod7_vec_addr:
   	iounmap(privdrv->fpga_irq_clr);   
err_dtb_iomap_fpga_irq_clear_reg:
   	iounmap(privdrv->fpga_vme_cpci_ddr_base_addr);   
err_dtb_iomap_vme_cpci_ddr_base:
   	iounmap(privdrv->fpga_pcie_ddr_base_addr);
err_dtb_iomap_pcie_ddr_base:
   	free_irq(privdrv->irq, privdrv);
err_req_irq:   
err_dtb_irq:
err_dtb_ack_steer:
err_dtb_parent:
   	return result;
}

static int init_sysfs(struct platform_device *pdev, struct ep_shrm_drv *drv) {
   	struct kobject *kobj;
   	struct ep_shrm_sysfs_entry *entry;
   	int err;
   
   	kobj = kobject_create_and_add("irq", &pdev->dev.kobj);
   
   	if(!kobj)
      		return -ENOMEM;
   
   	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
   
   	if (!entry)
      		return -ENOMEM;
   
   	entry->genIrq = 0;
   	entry->drv = drv;
   	entry->kobj_attr = int_genirq_attribute;
   	err = sysfs_create_file(kobj, &entry->kobj_attr.attr);
   	if (err) {
      		kfree(entry);
      	return err;
   	}

   	return 0;
}

static const struct file_operations ep_shrm_fops =
{
   .owner =   THIS_MODULE,
   .open =    ep_shrm_open,
   .release = ep_shrm_release,
   .read =    ep_shrm_read,
   .write =   ep_shrm_write,
   .poll =    ep_shrm_poll,
   .llseek =  ep_shrm_llseek,
};

static int ep_shrm_probe(struct platform_device *pdev) {
   	int result = 0;
   	struct device *privdev = &pdev->dev;
   	struct ep_shrm_drv *drv = NULL;
   	void *virt_addr_ptr;
   	phys_addr_t phys_base_addr;
   	dev_t dev;

   	dev_info(&pdev->dev, "\n");

   	/* Allocate and initialize device */
   	drv = kzalloc(sizeof(struct ep_shrm_drv), GFP_KERNEL);
   	if (!drv) {
      		result = -ENOMEM;
      		goto err_drv;
   	}
   
   	drv->parent = privdev;
   
   	/* get dtb config */
   	result = ep_shrm_dtb(drv);
   	if (result < 0) {
      		dev_warn(&pdev->dev, "Failed to allocate process dtb\n");
      		goto err_ep_shrm_dtb;
   	}

   	/* int spin lock */
   	spin_lock_init(&drv->rx_lock);
   	/* init wait queue  */
   	init_waitqueue_head(&drv->rx_waitq);
   
   	/* TODO: hard code total memory size  */
   	drv->mem_size = MEM_ALLOC_SIZE;
   
   	/* Allocate contiguous physical memory */   
   	virt_addr_ptr = dma_zalloc_coherent(&pdev->dev, drv->mem_size,
					    &(drv->dma), GFP_KERNEL);

   	if (!virt_addr_ptr) {
      		dev_err(&pdev->dev, "Failed To Allocated Contiguous Memory.\n");
		pr_err("Failed To Allocated Contiguous Memory.\n");
      		result = -ENOMEM;
      		goto err_allocate_mem;
   	}

   	if (dma_set_mask_and_coherent(&pdev->dev,DMA_BIT_MASK(32))) {
      		pr_warn("not suitable DMA available for DMA_BIT_MASK(32)0x%llx!!!!\n",
			DMA_BIT_MASK(32));
   	}

   	if (dma_set_mask_and_coherent(&pdev->dev,DMA_BIT_MASK(64))) {
      		pr_warn("not suitable DMA available for DMA_BIT_MASK(64)0x%llx!!!!\n",
			DMA_BIT_MASK(64));
   	}

   
   	drv->virt_addr = virt_addr_ptr;
   
   	/* get phys address */
   	phys_base_addr = virt_to_phys(virt_addr_ptr);          
	drv->phy_addr = phys_base_addr; 
#ifdef DEBUG_SHRM
   	/*dev_info(&pdev->dev, "virt addr 0x%08x \n",(unsigned int)(drv->virt_addr));*/
	pr_info("virt addr 0x%p\n",drv->virt_addr);
   	/*dev_info(&pdev->dev, "phy addr 0x%08x \n",phys_base_addr);*/
	pr_info("phy addr 0x%p!!!!\n",phys_base_addr);
#endif   
   	/* set memory start address and mask to FPGA register */
   	/* for the DDR in the ARM core on the ARM board */
  	if (drv->fpga_vme_cpci_ddr_base_addr) {
      		iowrite32((u32)drv->dma,
           		  (drv->fpga_vme_cpci_ddr_base_addr + FPGA_VME_CPCI_DDR_MAP_OFFSET));

       		iowrite32(DDR_SHRM_MASK,
           		  (drv->fpga_vme_cpci_ddr_base_addr + FPGA_VME_CPCI_DDR_MSK_OFFSET));
#ifdef DEBUG_SHRM
		pr_info("virt_to_phys(drv->fpga_vme_cpci_ddr_base_addr)=0x%08x,"
			"drv->fpga_vme_cpci_ddr_base_addr=0x%p\n",
			virt_to_phys(drv->fpga_vme_cpci_ddr_base_addr),
			drv->fpga_vme_cpci_ddr_base_addr);
#endif
   	}
   	else {
        	pr_info("cPCI_VME not configured\n");
   	}
 
   	if(drv->fpga_pcie_ddr_base_addr) { 
       		/* for the DDR in the ARM core on the Intel board and 79G5 */
       		iowrite32((u32)drv->dma,
           		  (drv->fpga_pcie_ddr_base_addr + FPGA_PCIE_DDR_MAP_OFFSET));

       		iowrite32(DDR_SHRM_MASK,
           		  (drv->fpga_pcie_ddr_base_addr + FPGA_PCIE_DDR_MSK_OFFSET));
#ifdef DEBUG_SHRM
		pr_info("virt_to_phys(drv->fpga_pcie_ddr_base_addr)=0x%08x,"
			"drv->fpga_pcie_ddr_base_addr=0x%p\n",
			virt_to_phys(drv->fpga_pcie_ddr_base_addr),
			drv->fpga_pcie_ddr_base_addr);
#endif
   	}
   	else {
       		pr_info("PCIe not configured"); 
   	}
	
	/* Memset memory */
   	memset(virt_addr_ptr,0,drv->mem_size);
   	
	/* Allocate a char dev  */
   	result = alloc_chrdev_region(&dev, 0, 1, driver_name);
   	if (result) {
      		dev_warn(&pdev->dev, "Failed to allocate char device\n");
      		goto err_alloc_chrdev;
   	}

   	drv->cdev_major = MAJOR(dev);
   	drv->cdev_minor = EP_SHRM_MINOR;
   
   	cdev_init(&drv->cdev, &ep_shrm_fops);
   	drv->cdev.owner = THIS_MODULE;

   	result = cdev_add(&drv->cdev, MKDEV(drv->cdev_major, drv->cdev_minor), 1);
   	if (result) {
      		dev_err(&pdev->dev, "chardev registration failed\n");
      		goto err_add_cdev;
   	}
                    
   	/* Create sysfs class - on udev systems this creates the dev files */
   	drv->ep_shrm_sysfs_class = class_create(THIS_MODULE, driver_name);
   	if (IS_ERR(drv->ep_shrm_sysfs_class))
   	{
      		dev_err(&pdev->dev, "Error creating class.\n");
      		result = PTR_ERR(drv->ep_shrm_sysfs_class);
      		goto err_sysfs_class;
   	}
   
   	sprintf(drv->cdev_name, "%s-%u", driver_name, EP_SHRM_MINOR);
   	dev_info(&pdev->dev, "dev_name:%s, major:%u, minor:%u\n",
		 drv->cdev_name, drv->cdev_major, drv->cdev_minor);
   	dev = MKDEV(drv->cdev_major, drv->cdev_minor);
   
   	/* create sysfs device entry */
   	drv->ep_shrm_sysfs_device = device_create(drv->ep_shrm_sysfs_class, 
		&pdev->dev, dev, NULL, drv->cdev_name);
   
   	if (IS_ERR(drv->ep_shrm_sysfs_device)) {
      		dev_info(&pdev->dev, "Error creating sysfs device\n");
      		result = PTR_ERR(drv->ep_shrm_sysfs_device);
      		goto err_sysfs_device;
   	}            
   
   	init_sysfs(pdev, drv);
   
   	/* set dev opened only allow single user app to access this driver */
   	drv->opened = 0;
   
   	platform_set_drvdata(pdev, drv);
   
   	return 0;

err_sysfs_device:
   	class_destroy(drv->ep_shrm_sysfs_class);      
err_sysfs_class:
   	cdev_del(&drv->cdev);
err_add_cdev:
   	unregister_chrdev(drv->cdev_major,driver_name);
err_alloc_chrdev:
   	dma_free_coherent(&pdev->dev, drv->mem_size, drv->virt_addr, drv->dma);
err_allocate_mem:
err_ep_shrm_dtb:
   	kfree(drv);
err_drv:
   	return result;
}

static int ep_shrm_remove(struct platform_device *pdev) {
   	struct ep_shrm_drv *drv = platform_get_drvdata(pdev);

   	platform_set_drvdata(pdev, NULL);

   	class_destroy(drv->ep_shrm_sysfs_class);      

   	cdev_del(&drv->cdev);
pr_info("un reg dev:%s-%d\n",driver_name,drv->cdev_major);
   	unregister_chrdev(drv->cdev_major,driver_name);

   	free_irq(drv->irq, drv);

   	dma_free_coherent(&pdev->dev, drv->mem_size, drv->virt_addr, drv->dma);

   	kfree(drv);

   	return 0;
}

static const struct of_device_id of_ep_shrm_match[] = {
   { .compatible = "nai,nai-ep-shrm-v0.1" },
   { },
};

MODULE_DEVICE_TABLE(of, of_ep_shrm_match);

static struct platform_driver ep_shrm_platdrv = {
    	.driver = {
      		.name  = DRV_NAME,
      		.owner = THIS_MODULE,
      		.of_match_table = of_match_ptr(of_ep_shrm_match)
    	},
   	.probe    = ep_shrm_probe,
   	.remove   = ep_shrm_remove,
};

static int __init nai_ep_shrm_init(void) {
   	return platform_driver_register(&ep_shrm_platdrv);
}


static void __exit nai_ep_shrm_exit(void) {
   	platform_driver_unregister(&ep_shrm_platdrv);
}

/* subsys_initcall() can only be used by a built-in (statically linked) module.
 * module_init can be used by either built-in or loadable modules.
 */
subsys_initcall(nai_ep_shrm_init);
module_exit(nai_ep_shrm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("naii@naii.com>");
MODULE_DESCRIPTION("NAI share memory end-point");

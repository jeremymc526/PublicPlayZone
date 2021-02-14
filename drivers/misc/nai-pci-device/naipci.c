/* =========================================================================
 *
 * Copyright (c) 2013 North Atlantic Industries, Inc.  All Rights Reserved.
 *
 * Author: North Atlantic Industries, Inc.
 *
 * SubSystem: Linux kernel 2.6.x device driver for PCI boards
 *
 * FileName: naipci.c
 *
 * History:
 *
 * 09-24-07 JC Added multiple PCI card support in x86 Linux.
 * 10-01-07 JC Added User level ISR support in x86 Linux
 * 12-12-07 JC Fedora 7 support, passing Bus#, Slot#, Card Index for NaiProbeDevice
 * 01-30-08 JC PCI hardware changed Interrupt handling
 * 03-10-10 JC supported Linux 2.6.31 - Fedora 12
 * 03-24-10 JC supported Linux 64-bit Fedora 12
 * 11-07-11 DT Modified to deal with kernel changes surrounding ioctl.
 *             (device_ioctl definition changed and file_operations struct changed)
 *             Also changed init_MUTEX calls to used sema_init when init_MUTEX
 *             is not defined.
 *
 * 11-03-20 JH Modified to fix kernel panic built with petalinux-v2018.2, should be
 *             backword campatible
 * ==========================================================================*/
#include <linux/module.h>
#ifdef  pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "NAI %s:%s:%d::" fmt, strrchr(__FILE__,'/'), __func__, __LINE__
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/errno.h>
#if (KERNEL_VERSION(2,6,18) > LINUX_VERSION_CODE)
#include <linux/config.h>
#endif
#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/wait.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/ioctl.h>
#include <asm/irq.h>
#include <asm/dma.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 3, 0)
#include <asm/switch_to.h>
#else
#include <asm/system.h>
#endif
//#if LINUX_VERSION_CODE > KERNEL_VERSION(4,0,0)
#include <linux/uaccess.h>
//#else
//#include <asm/uaccess.h>
//#endif
#include <asm/hardirq.h>
#include <asm/setup.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include "../nai_KerModRev.h"

#include "naioctl.h"

//#define DEBUG_MODULE

#define DRIVER_AUTHOR    "NAI Support <TechSupport@naii.com>"
#define DRIVER_DESC      "NAI Inc. Generic Driver"
#define DRIVER_VERSION	 "1.10"

#define DEVICE_NAME      "nai_pci"
#define NAI_MAX_DEVICES  8
#define PCI_VENDOR_ID_NAI              0x15AC
#define PCIE_DEVICE_ID_ARRAY_NAI 	   {0x7584, 0x6881, 0x6781}
#define GEN5_CPCI_DEVICE_ID_ARRAY_NAI  {0x7581}
#define HW_REV_BAR1      			   0x4
#define NAI_LEGACY_IRQ_READ_OFFSET     0x3400
#define NAI_LEGACY_IRQ_WRITE_OFFSET    0x3404
#define NAI_GEN5_IRQ_READ_OFFSET       0x3F00
#define NAI_GEN5_IRQ_STATUS_OFFSET     0x3F04
#define NAI_GEN5_TRIG_IRQ_OFFSET 	   0x3F08

#define MAX_DWORD_IN_BLK_FRAME         125
#define MAX_WORD_IN_BLK_FRAME          MAX_DWORD_IN_BLK_FRAME * 2
#define NAI_GEN5_BLK_XFER_OFFSET_ADD   0x3F10
#define REG_WIDTH_8BIT                 1
#define REG_WIDTH_16BIT                2 
#define REG_WIDTH_32BIT                4
#define MASK_UPPER_WORD                0xFFFF0000
#define MASK_LOWER_WORD                0xFFFF
#define NAI_GEN5_BLK_CTRL_HW_DEFAULT   0x00000102
#define NAI_GEN5_BLK_CTRL_NULL         0x00000000
#define NAI_GEN5_BLK_CTRL_PACK         0x00010000
#define NAI_GEN5_BLK_CTRL_AUTO_REARM   0x80000000
#define NAI_GEN5_BLK_FOUR_BYTE         0x4

#define NAI_GEN5_SHRM_BAR_DISABLE      0x00
#define NAI_GEN5_SHRM_BAR_ENABLE       0x01
#define NAI_GEN5_SHRM_SIZE 		       (1 * (1024 * 1024));

#define MAX_SHR_MEM_SIZE               64

typedef struct _NAI_CONFIG
{
   ULONG BaseAdr;
   ULONG PortCount;
   UCHAR IrqLine;
   UCHAR Status;
   ULONG bPCIDevice;
   ULONG BusNum;
   ULONG DeviceId;
} NAI_CONFIG, *PNAI_CONFIG;

static int device_open(struct inode *ip, struct file *fp);
static int device_release(struct inode *ip, struct file *fp);

/* For kernel versions greater than or equal to 2.6.36 - the device_ioctl static function definition has changed.
   Additionally, the file_operations structure changed - the .ioctl structure field no longer exists - now .unlocked_ioctl is used instead.*/
#if (KERNEL_VERSION(2,6,36) <= LINUX_VERSION_CODE)
static long device_ioctl(struct file *fp, unsigned int ioctl_num, unsigned long ioctl_param);
#else
static int device_ioctl(struct inode *ip, struct file *fp, unsigned int ioctl_num, unsigned long ioctl_param);
#endif
#ifdef CONFIG_COMPAT
static long device_compat_ioctl(struct file *fp, unsigned int ioctl_num, unsigned long ioctl_param);
#endif

/* Globals */
static struct nai_state *g_dev[NAI_MAX_DEVICES];
static struct class *nai_class;
static int g_device_count = 0;
static int Major = 0;
static const unsigned short pcieDeviceIdArrayNai[] = PCIE_DEVICE_ID_ARRAY_NAI;
static const unsigned short gen5cpciDeviceIdArrayNai[] = GEN5_CPCI_DEVICE_ID_ARRAY_NAI;

static DECLARE_WAIT_QUEUE_HEAD(nai_wq);

struct nai_state {
   /* the corresponding pci_dev structure */
   struct pci_dev *pdev;

   /* hardware resources */
   unsigned long memstart;
   unsigned long memend;
   void __iomem *base_addr;
   void __iomem *shrm_base_addr;
   u32 			shrm_size;
   u8 			shrm_bar_enabled;
   unsigned int  irq;
   unsigned int  device_index;
   unsigned int  device_open;

   /* PCI ID's */
   u16 vendorId;  /* PCI Vendor ID */
   u16 deviceId;  /* PCI Device ID */
   u8 revId;      /* the chip revision */

   spinlock_t lock;
   struct semaphore open_sem;
   mode_t open_mode;
/*   wait_queue_head_t open_wait; */

   struct semaphore sem;
   unsigned int irqReadAddr;
   unsigned int irqWriteAddr;
   unsigned int irqWriteValue;
   unsigned short irqValue;
   unsigned int   irqValue32;
   unsigned int irqUsrFlag;
   
  /* PCI location info */
   u16 bus_number;
   u16 slot_number; /* dev number */
   u16 fn_number;   /* function number */
};

static u32 rw16Data(struct nai_state *s, u32 inRegAddr, u32 inStride, u32 inBlkCap, u32 inCount, void *dataBufferAddr, u32 rwOp, u32 *byteCount);
static u32 rw32Data(struct nai_state *s, u32 inRegAddr, u32 inStride, u32 inBlkCap, u32 inCount, void *dataBufferAddr, u32 rwOp, u32 *byteCount);
#ifdef CONFIG_COMPAT
static u32 rw16DataCompat(struct nai_state *s, u32 inRegAddr, u32 inStride, u32 inBlkCap, u32 inCount, void *dataBufferAddr, u32 rwOp, u32 *byteCount);
static u32 rw32DataCompat(struct nai_state *s, u32 inRegAddr, u32 inStride, u32 inBlkCap, u32 inCount, void *dataBufferAddr, u32 rwOp, u32 *byteCount);
#endif
static void configBlockCtrl(struct nai_state *s, u32 inModuleNum,  u32 inBlkCap, u32 inStride,  u32 blkXferSize);
static u32 getBlockFrame(u32 inCount, u32 in_BlkCap);
static u32 getRemainingCounts(u32 inCount, u32 inBlkCap);
static s32 get_pci_module_revision(NAI_MODULE_PCI_REVISION  __user *arg);

struct file_operations fops = {
  .open = device_open,
  .release = device_release,
#if (KERNEL_VERSION(2,6,36) <= LINUX_VERSION_CODE)
  .unlocked_ioctl = device_ioctl,
#else
  .ioctl = device_ioctl,
#endif
#ifdef CONFIG_COMPAT
  .compat_ioctl = device_compat_ioctl,
#endif
};

#define NAI_RESOURCE_SIZE_T
#define NAI_PCI_IOREMAP_BAR

#ifndef NAI_RESOURCE_SIZE_T
typedef unsigned long resource_size_t;
#endif

#ifndef NAI_PCI_IOREMAP_BAR
static inline void * pci_ioremap_bar(struct pci_dev *pdev, int bar)
{
        resource_size_t base, size;

		if(pci_resource_len(pdev, bar) == 0){
			 printk(KERN_ERR "BAR %d Length is 0:%x\n", bar);
			return NULL;
		}
        if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM)) {
                printk(KERN_ERR
                       "Cannot find proper PCI device base address for BAR %d.\n",
                       bar);
                return NULL;
        }

        base = pci_resource_start(pdev, bar);
        size = pci_resource_len(pdev, bar);

        return ioremap_nocache(base, size);
}
#endif

/* Basic 8-bit, 16-bit, 32-bit read and write functions */
static void nai_write8(struct nai_state *s, u32 off, u8 data)
{
        writeb(data, s->base_addr + off);
}

static u8 nai_read8(struct nai_state *s, u32 off)
{
        return (readb(s->base_addr + off));
}

static void nai_write16(struct nai_state *s, u32 off, u16 data)
{
        writew(data, s->base_addr + off);
}

static u16 nai_read16(struct nai_state *s, u32 off)
{
        return (readw(s->base_addr + off));
}

static void nai_write32(struct nai_state *s, u32 off, u32 data)
{
        writel(data, s->base_addr + off);
}

static u32 nai_read32(struct nai_state *s, u32 off)
{
        return (readl(s->base_addr + off));
}

#ifdef DEBUG_MODULE
static void printk_hex_info(unsigned char* hex_info,unsigned int info_size)
{
   #define COLUMN_NUM 16
   #define ELEMENT_SIZE 3  /*including 2 bytes hex num and 1 byte field delimiter ',' or '\n'*/
   char work_buf[ELEMENT_SIZE*COLUMN_NUM+sizeof(int)/*sizeof(int) is used for last str delimiter '\0' and mem alignment*/];
   unsigned int i;

   memset(work_buf,0,sizeof(work_buf));
   for(i=0;i<info_size;i++)
   {
      sprintf(work_buf+(i*ELEMENT_SIZE)%(COLUMN_NUM*ELEMENT_SIZE),"%02x%c",hex_info[i],(i==(info_size-1)||(i+1)%COLUMN_NUM==0)?'\n':',');
      if(i==(info_size-1)||(i+1)%COLUMN_NUM==0)
      {
         printk(KERN_INFO"%s",work_buf);
         memset(work_buf,0,sizeof(work_buf));
      }
   }
}
#endif

static int nai_shrm_access(struct nai_state *s, nai_shrm_data *usr_arg, u32 rd)
{
   int err = 0;
   u32 oklen = 0;
   nai_shrm_data tmp_nai_shrm_data;

   err=copy_from_user(&tmp_nai_shrm_data,usr_arg,sizeof(tmp_nai_shrm_data));
   if(err!=0)
   {
      err = -EFAULT;
      goto exit;
   }

   if(s->shrm_bar_enabled != NAI_GEN5_SHRM_BAR_ENABLE)
   {
      pr_err("device1 s->deviceId=0x%x not enabled\n",s->deviceId);
      err = -EFAULT;
      goto exit;
   }

   if (rd)
   {  //read operation
      err = !access_ok(VERIFY_WRITE, tmp_nai_shrm_data.pbuf, tmp_nai_shrm_data.len);
   }
   else
   {  //write operation
      err = !access_ok(VERIFY_READ, tmp_nai_shrm_data.pbuf, tmp_nai_shrm_data.len);
   }
   if (err)
   {
      pr_err("user did not allocate space currectly\n");
      err = -EFAULT;
      goto exit;
   }
#ifdef DEBUG_MODULE
   else
   {
      if(tmp_nai_shrm_data.len>1024)
      {
         pr_info("passed access_ok(%s,tmp_nai_shrm_data.pbuf,tmp_nai_shrm_data.len(%d))\n",rd==0?"VERIFY_READ":"VERIFY_WRITE",tmp_nai_shrm_data.len);
      }
   }
#endif

   /* Ensure we are starting at a valid location */
   if ((tmp_nai_shrm_data.offset < 0) || (tmp_nai_shrm_data.offset > (s->shrm_size - 1)))
   {
      pr_err("invalid ofset\n");
      err = -EFAULT;
      goto exit;
   }

   /* Ensure not reading past end of the image */
   if ((tmp_nai_shrm_data.offset + tmp_nai_shrm_data.len) > s->shrm_size)
   {
      oklen =  s->shrm_size - tmp_nai_shrm_data.offset;
   }
   else
   {
      oklen = tmp_nai_shrm_data.len;
   }

   if (rd)
   {  /*read op*/
      err = copy_to_user(tmp_nai_shrm_data.pbuf, s->shrm_base_addr+tmp_nai_shrm_data.offset, oklen);
      if(err)
      {
         pr_err("failed copy_to_user length: %d \n",oklen);
         oklen = 0;
      }
#ifdef DEBUG_MODULE
      //too many log info in /var/log/messages
      else
      {
         pr_info("read %d bytes from phy shrm offset 0x%0x, vir addr=%p\n",oklen,tmp_nai_shrm_data.offset,s->shrm_base_addr+tmp_nai_shrm_data.offset);
         printk_hex_info(s->shrm_base_addr+tmp_nai_shrm_data.offset,oklen<64?oklen:16);//not necassary to dump all read info
      }
#endif
   }
   else
   {  /*write op*/
      /* on failure, set the len to 0 to return empty packet to the device */
      err = copy_from_user(s->shrm_base_addr+tmp_nai_shrm_data.offset, tmp_nai_shrm_data.pbuf, oklen);   
      if (err)
      {
         pr_err("failed copy_from_user: length %d \n",oklen);
         oklen = 0;
      }
#ifdef DEBUG_MODULE
      else
      {
         if(oklen>4)
         {
           pr_info("write %d bytes to phy shrm off 0x%0x, vir addr=%p\n",oklen,tmp_nai_shrm_data.offset,s->shrm_base_addr+tmp_nai_shrm_data.offset);
           printk_hex_info(s->shrm_base_addr+tmp_nai_shrm_data.offset,oklen<64?oklen:16);//not necassary to dump all write info
         }
      }
#endif
   }
exit:

   return err;
}
/*TODO: 
	 * This is a temporary hack to support download via
	 * bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
	 * We have to create another PCI device driver 
	 * for Gen5 device to handle normal I/O operation and 
	 * download operation
	 * 
	 */
static void nai_gen_shrm_interrupt(struct nai_state *s)
{
   if(s->shrm_bar_enabled == NAI_GEN5_SHRM_BAR_ENABLE){
     writel(1, s->base_addr + NAI_GEN5_TRIG_IRQ_OFFSET);
   }
}

/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
static void config_shrm_pcie_bar2(struct pci_dev *pcidev, struct nai_state *drvdata)
{
   struct nai_state *privdrv = drvdata;
   u32 value = 0;

   /*check if bar2 address exist*/
   pci_read_config_dword(pcidev, PCI_BASE_ADDRESS_2, &value);
   /*printk(KERN_INFO*/pr_info( "----------------------------------%s:probe vendor:%x device:%x bar2: %x\n",DEVICE_NAME,pcidev->vendor, pcidev->device,value);
   if(value > 0)
   {	
      privdrv->shrm_base_addr = pci_ioremap_bar(pcidev,2);
      if (!privdrv->shrm_base_addr)
      { 
         printk(KERN_ERR "%s:Cannot map share memory bar2 \n",DEVICE_NAME);
      }
      else
      {
         privdrv->shrm_bar_enabled = NAI_GEN5_SHRM_BAR_ENABLE;
         privdrv->shrm_size = NAI_GEN5_SHRM_SIZE;
      }
   }

   return;   
}

/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
static void config_shrm_cpcie_bar1(struct pci_dev *pcidev, struct nai_state *drvdata)
{
	struct nai_state *privdrv = drvdata;
	u32 value = 0;

	/*check if bar1 address exist*/
	pci_read_config_dword(pcidev, PCI_BASE_ADDRESS_1, &value);
	/*printk(KERN_INFO*/pr_info("%s:probe vendor:%x,device:%x,bar1 phy addr: %x\n",DEVICE_NAME,pcidev->vendor, pcidev->device,value);
	if(value > 0)
	{	
		privdrv->shrm_base_addr = pci_ioremap_bar(pcidev, 1);
		if (!privdrv->shrm_base_addr)
		{ 
			printk(KERN_ERR "%s:Cannot map share memory bar2 \n",DEVICE_NAME);
		}
		else
		{
			privdrv->shrm_bar_enabled = NAI_GEN5_SHRM_BAR_ENABLE;
			privdrv->shrm_size = NAI_GEN5_SHRM_SIZE;
		}
	}
	
	return;  
}

/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
static void config_shrm_bar(struct pci_dev *pcidev, struct nai_state *drvdata)
{
	struct nai_state *privdrv = drvdata;
	int count = 0;
	
	privdrv->shrm_bar_enabled = NAI_GEN5_SHRM_BAR_DISABLE;
	privdrv->shrm_size = 0;
	
	for(count=0; count< sizeof(pcieDeviceIdArrayNai)/sizeof(unsigned short); count++)
	{
		if( pcieDeviceIdArrayNai[count] == privdrv->deviceId ){
			config_shrm_pcie_bar2(pcidev,drvdata);
			goto done;
		}
			
	}
	
	for(count=0; count< sizeof(gen5cpciDeviceIdArrayNai)/sizeof(unsigned short); count++)
	{
		if( gen5cpciDeviceIdArrayNai[count]  == privdrv->deviceId ){
			config_shrm_cpcie_bar1(pcidev, privdrv);
			goto done;
		}
	
	}

done:	
	return;
	
}

#if (KERNEL_VERSION(2,6,22) > LINUX_VERSION_CODE)
irqreturn_t nai_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#else
irqreturn_t nai_interrupt(int irq, void *dev_id)
#endif
{
   struct nai_state *s = dev_id;
   unsigned short IntType = 0;
   unsigned int   Int32Type =0;
   irqreturn_t irqStatus = IRQ_NONE;

   if(s->revId < NAI_HWREV_GEN5)
   {   
	 IntType = nai_read16(s, NAI_LEGACY_IRQ_READ_OFFSET);
	 if (IntType & 0x01) /* it's my interrupt */
	 {
		/* save IntStatus word */
		s->irqValue = IntType;
		/* clear interrupt, for backward compatible */
		nai_write16(s, NAI_LEGACY_IRQ_WRITE_OFFSET, s->irqWriteValue );
		s->irqUsrFlag = 1;
		wake_up_interruptible(&nai_wq);
		/* we did have a valid interrupt and handled it */
		irqStatus = IRQ_HANDLED;
	 }
      
   }
   else   /*for HW Revision 2 and above*/
   {
         //read status register 
         Int32Type = nai_read32(s, NAI_GEN5_IRQ_STATUS_OFFSET);
        
         if (Int32Type & 0x1)
         {
            //read vector register to clear IRQ
            s->irqValue32 = nai_read32(s, NAI_GEN5_IRQ_READ_OFFSET);
            s->irqUsrFlag = 1;
            wake_up_interruptible(&nai_wq);
            /* we did have a valid interrupt and handled it */
            irqStatus = IRQ_HANDLED;
         }
   }
   return(irqStatus);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
static int __devinit nai_probe(struct pci_dev *pcidev, const struct pci_device_id *pciid)
#else
static int nai_probe(struct pci_dev *pcidev, const struct pci_device_id *pciid)
#endif
{
   struct nai_state *s;
   int res = -1;
   int err = -1;
#ifdef CONFIG_PCI_MSI
   int devCnt = 0;
#endif   
   
   pr_info("%s:probe vendor:%x device:%x\n",DEVICE_NAME,pcidev->vendor, pcidev->device);   
   /*TODO: This temp workaround to ignore 68ARM1 PCIe RC Bridge port*/ 
   if(pcidev->device == 0x6884){
      printk(KERN_INFO "ignore pci device %X \n",pcidev->device);   
      return res;
   }

   if ((res=pci_enable_device(pcidev))){
      printk(KERN_ERR "%s:pci device was already enabled\n",DEVICE_NAME);
      return res;
   }
	
   res = pci_request_regions(pcidev, DEVICE_NAME);
   if (res) {
      printk(KERN_ERR "%s:Cannot obtain PCI resources, aborting.\n",DEVICE_NAME);
      goto err_out_disable_pdev;
   }
	
   pci_set_master(pcidev);

   if (!(s = kmalloc(sizeof(struct nai_state), GFP_KERNEL))) {
      res = -ENOMEM;
      printk(KERN_ERR "%s:out of memory.\n",DEVICE_NAME);
      goto err_out_disable_pdev;
   }

   memset(s, 0, sizeof(struct nai_state));
   /* init_waitqueue_head(&s->open_wait); */
#ifndef init_MUTEX
   sema_init(&s->open_sem, 1);
#else
   init_MUTEX(&s->open_sem);
#endif
   spin_lock_init(&s->lock);

   s->pdev = pcidev;
   
   s->irq = pcidev->irq;
   s->vendorId = pcidev->vendor;
   s->deviceId = pcidev->device;
   s->device_index = g_device_count;
   pci_read_config_byte(pcidev, PCI_REVISION_ID, &s->revId);

   s->bus_number = pcidev->bus->number;
   s->slot_number = pcidev->devfn >> 3;   /* right shift 3 bits */
   s->fn_number = pcidev->devfn & 0x3;    /* take low 3 bits */
	
   s->irqWriteValue = 1; /*set legacy IRQ write ACK value to 1*/
   s->irqUsrFlag = 0; /*default irq usr flag to 0*/
   
   if( (pcidev->revision & 0xf) >= HW_REV_BAR1 ){
		s->base_addr= pci_ioremap_bar(pcidev, 1);
	}else{
		s->base_addr= pci_ioremap_bar(pcidev, 0);
	}
	
	
   if (!s->base_addr) {
		printk(KERN_ERR "%s:Cannot map device registers, aborting.\n",DEVICE_NAME);
		goto err_out_free_res;
	}
	
/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
	config_shrm_bar(pcidev, s);
   
#ifdef CONFIG_PCI_MSI
	if (pci_msi_enabled()){
		for(devCnt=0; devCnt< sizeof(pcieDeviceIdArrayNai)/sizeof(unsigned short); devCnt++)
		{
			if( (pcieDeviceIdArrayNai[devCnt]  == s->deviceId) ){
				err = pci_enable_msi(pcidev);
				if (err){ 
					printk(KERN_ERR "%s:Failed to enable MSI\n",DEVICE_NAME);
					goto err_out_unmap_io;
				}
			break;
			}
		}
	}
#endif

   if (request_irq(pcidev->irq, nai_interrupt,
#if (KERNEL_VERSION(2,6,24) < LINUX_VERSION_CODE)
       IRQF_SHARED
#else
       SA_SHIRQ
#endif
       , DEVICE_NAME, s) != 0) {
      printk(KERN_ERR "%s: unable to request IRQ %d\n",DEVICE_NAME, pcidev->irq);
      res = -EBUSY;
      goto err_out_unmap_io;
   }
	  
   if(s->device_index==0)
   {
      /* register this module as a character device driver.*/
      printk(KERN_INFO "%s:registering character device \n",DEVICE_NAME);

      err = register_chrdev (0, DEVICE_NAME, &fops);
      if( err < 0 )
      {
         printk (KERN_ERR "%s:registering char device failed with err=%d\n",DEVICE_NAME,err);
         goto err_out_free_irq;
      }
	
	  Major = err;	/* In case the user specified `major=0' (dynamic) */
	  nai_class = class_create(THIS_MODULE, DEVICE_NAME);
	  if (IS_ERR(nai_class)) {
		 err = PTR_ERR(nai_class);
		 printk (KERN_ERR "%s:failed to created device class err=%d\n",DEVICE_NAME,err);
		 goto out_chrdev;
	  }
  
   }

	device_create(nai_class, NULL, MKDEV(Major, s->device_index), NULL,
			  "nai%u", s->device_index);

   pci_set_drvdata(pcidev, s);
   g_dev[g_device_count] = s;
   g_device_count++;
   
   return 0;

out_chrdev:
	unregister_chrdev(Major, DEVICE_NAME);
err_out_free_irq:
#ifdef CONFIG_PCI_MSI
if (pci_msi_enabled()){
		for(devCnt=0; devCnt< sizeof(pcieDeviceIdArrayNai)/sizeof(unsigned short); devCnt++)
		{
			if(((pcidev->msi_enabled) && (pcieDeviceIdArrayNai[devCnt] == s->deviceId))){
				pci_disable_msi( pcidev );
				break;	
			}
		}
	}
#endif
   free_irq(s->irq, (struct nai_state *) s);	
err_out_unmap_io:
	iounmap(s->base_addr);
if(	s->shrm_bar_enabled == NAI_GEN5_SHRM_BAR_ENABLE )
	iounmap(s->shrm_base_addr);
err_out_free_res:	   
	kfree(s);
err_out_disable_pdev:
   pci_disable_device(pcidev);
   pci_set_drvdata(pcidev, NULL);
   return res;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
static void __devinit nai_remove(struct pci_dev *dev)
#else
static void nai_remove(struct pci_dev *dev)
#endif
{
#if (KERNEL_VERSION(2,6,24) >= LINUX_VERSION_CODE)
   int ret = -1;
#endif   
   struct nai_state *s = pci_get_drvdata(dev);

#ifdef CONFIG_PCI_MSI
   int 	devCnt=0;
#endif   
   
   printk(KERN_INFO "%s:remove vendor:%x device:%x\n",DEVICE_NAME,dev->vendor,dev->device);   
   
   if (!s){
	printk (KERN_ERR"%s:pci dev was not initialized\n",DEVICE_NAME);
	return;
   } 


   if(dev->irq){      
      free_irq(dev->irq, s);
#ifdef CONFIG_PCI_MSI
if (pci_msi_enabled()){
		for(devCnt=0; devCnt< sizeof(pcieDeviceIdArrayNai)/sizeof(unsigned short); devCnt++)
		{
			if(((dev->msi_enabled) && (pcieDeviceIdArrayNai[devCnt] == s->deviceId))){
				printk (KERN_ERR"%s:disable MSI\n",DEVICE_NAME);
				pci_disable_msi( dev );
			break;	
			}
		}
	}
#endif
   }
   
	device_destroy(nai_class, MKDEV(Major, s->device_index));

    if ( g_device_count <= 1 ){
	  printk (KERN_INFO"%s: class destory \n",DEVICE_NAME);
	  class_destroy(nai_class);
#if (KERNEL_VERSION(2,6,24) < LINUX_VERSION_CODE)
      unregister_chrdev (Major, DEVICE_NAME);
#else
      ret = unregister_chrdev (Major, DEVICE_NAME);
      if (ret < 0) {
       printk (KERN_ERR"%s:error in unregister err=%d\n",DEVICE_NAME,ret);
      }
#endif
    }
	
   if (s->base_addr) {
      iounmap(s->base_addr);
      s->base_addr = 0;
   }
   
   if(s->shrm_base_addr)
	  iounmap(s->shrm_base_addr);

   /* release_mem_region(s->memstart, s->memend);*/
   pci_release_regions(dev);
   pci_disable_device(dev);

   kfree(s);

   pci_set_drvdata(dev, NULL);
   //t.y new remove a device count
   g_device_count--;	
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
static struct pci_device_id id_table[] __devinitdata = {
#else
static struct pci_device_id id_table[] = {
#endif
  { PCI_VENDOR_ID_NAI, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0 },
  { 0, }
};

MODULE_DEVICE_TABLE(pci, id_table);

static struct pci_driver nai_driver = {
  .name     = DEVICE_NAME,
  .id_table = id_table,
  .probe    = nai_probe,
  .remove   = nai_remove
};

static int __init nai_begin(void)
{
   /*printk (KERN_INFO*/pr_info("%s:North Atlantic Industries Linux Driver Version %s\n",DEVICE_NAME, DRIVER_VERSION);
   return pci_register_driver(&nai_driver);
}

static void __exit nai_finish (void)
{
   pci_unregister_driver(&nai_driver);
}

module_init(nai_begin);
module_exit(nai_finish);

static int device_open(struct inode *ip, struct file *fp)
{
   int minor_number = iminor(ip);

   fp->private_data = g_dev[minor_number];

   try_module_get(THIS_MODULE);

   g_dev[minor_number]->device_open = 1;
#ifdef DEBUG_MODULE
   pr_info("file name:\"%s\",minor:%d\n",fp->f_path.dentry->d_iname,iminor(fp->f_path.dentry->d_inode));
#endif
   return (0);
}

static int device_release(struct inode *ip, struct file *fp)
{
   int minor_number = iminor(ip);
   module_put(THIS_MODULE);

   /* printk ("<1>nai:close() called minor=%d\n", minor_number);*/
   g_dev[minor_number]->device_open = 0;
   return (0);
}

/*if failed, it returns -EFAULT*/
static int get_pci_module_revision(NAI_MODULE_PCI_REVISION __user *arg) {
	int ret = 0;
	u8 major = NAI_DRV_PCI_VER_MAJOR;
	u8 minor = NAI_DRV_PCI_VER_MINOR;
	
	if((ret == put_user(major, &arg->pciMajorRev)) == 0)
    {
      ret = put_user(minor, &arg->pciMinorRev);
    }
	return ret;
}
/*Get frame count.  
If is pack, the frame count is in_count / MAX_DWORD_IN_BLK_FRAME *2 since each block transfer is 32 bit and when it is packed there are 2 data entries in each 32 bit block transfer.
If is not pack, the frame count is in_count / MAX_DWORD_IN_BLK_FRAME since each block transfer is 32 bit.
*/
static u32 getBlockFrame(u32 inCount, u32 in_BlkCap)
{
   u32 frameCount=0;
   frameCount = inCount / MAX_DWORD_IN_BLK_FRAME; 
   if( in_BlkCap & PCK_CAPABLE)
   {
      frameCount = inCount / (MAX_DWORD_IN_BLK_FRAME * 2);
   }
   return(frameCount);   
}

/*Get frame count.  
If is pack and if count is odd, it needs one to round up the count.
If is pack and count is not odd, remaining count is mod in_count divided by 2.
If is not pack, remaining count is mod in_count.
*/
static u32 getRemainingCounts(u32 inCount, u32 inBlkCap)
{
   u32 remainingCount=0;

   if( inBlkCap & PCK_CAPABLE)
   {
	  remainingCount = (inCount % (MAX_DWORD_IN_BLK_FRAME *2)) / 2;
	  if( (inCount % 2) == 1)  
	  {  
		remainingCount = (inCount % (MAX_DWORD_IN_BLK_FRAME *2)) / 2 + 1;   /*odd*/
      }
   }
   else
   {
      remainingCount = (inCount % (MAX_DWORD_IN_BLK_FRAME ));
   }
   return(remainingCount);
}

/*configBlockCtrl()  
Configure the block transfer control register.
*/
static void configBlockCtrl(struct nai_state *s, u32 inModuleNum,  u32 inBlkCap, u32 inStride,  u32 blkXferSize)
{
	u32 blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
	u32 blockCtrlStride =0;
	
	if(inBlkCap & PCK_CAPABLE)
		blockCtrl |= NAI_GEN5_BLK_CTRL_PACK;
	
    if(blkXferSize == MAX_WORD_IN_BLK_FRAME)
    {	
		blockCtrl |= NAI_GEN5_BLK_CTRL_AUTO_REARM;  	/*turn on auto re-arm*/
		blockCtrl |= MAX_WORD_IN_BLK_FRAME;             /*set max trasfer word size*/
	}
	else
	{
		blockCtrl |= blkXferSize *2 ;                   /*multiple by 2 since HW expects in words*/
	}
	/*stride*/
	blockCtrlStride = inStride << 8; 		/*stride is defined at bit 15-8 of block ctrl reg*/
   blockCtrl |= blockCtrlStride;               		/*set stride size (4bytes per stride)*/
		
	nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((inModuleNum-1)*REG_WIDTH_32BIT) , blockCtrl );
}

/*rw32Data()  
Perform read or write operations from user to/from Hardware when the user data buffer is 32 bit.
*/
static u32 rw32Data(struct nai_state *s, u32 inRegAddr, u32 inStride, u32 inBlkCap, u32 inCount, void *dataBufferAddr, u32 rwOp, u32 *byteCount)
{
   int32_t status =0;
   u32 ival =0;
   u32 ival2 =0;
   u32 temp =0;
   
	if(rwOp == READ_OP)
	{
		ival = nai_read32( s, inRegAddr + *byteCount * inStride);
		if(inBlkCap & PCK_CAPABLE)
		{
         /*data is packed, the first item is at bit(0-15), the second item is at bit(16-31)
         use 32 bit on a 16Bit user data buffer, so reduced the number of bus access by half*/
         temp = ival & 0xffff;
         status = copy_to_user( (dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 ), &temp, REG_WIDTH_32BIT );
         
         if(inCount/2 != *byteCount)
         {
            temp = ival & 0xffff0000;
            temp >>= 16;
            status = copy_to_user( (dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 + REG_WIDTH_32BIT ), &temp, REG_WIDTH_32BIT );
         }
		}
		else
		{
			status = copy_to_user( (dataBufferAddr + *byteCount * REG_WIDTH_32BIT), &ival, REG_WIDTH_32BIT );
		}
	}
	else  /*write op*/
	{
      if(inBlkCap & PCK_CAPABLE)
		{
			ival = (uint32_t)(*(u64*)(dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 ));
         if(*byteCount != inCount/2)   
         {
            ival2 = (uint32_t)(*(u64*)(dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 + REG_WIDTH_32BIT ));
         }
         else
         {
            if(inCount % 2 == 0)
            {
               ival2 = (uint32_t)(*(u64*)(dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 + REG_WIDTH_32BIT ));
            }
         }
         nai_write32(s, inRegAddr + *byteCount * inStride,  ((ival2 & 0xffff) << 16) | (ival & 0xffff));
		}
		else
		{
		   ival = (uint32_t)(*(u64*)(dataBufferAddr + *byteCount * REG_WIDTH_32BIT));
         nai_write32(s, inRegAddr + *byteCount * inStride, ival );
		}
	}
	(*byteCount)++;
	return(status);
}
/*rw16Data()  
Perform read or write operations from user to/from Hardware when the user data buffer is 16 bit.
*/
static u32 rw16Data(struct nai_state *s, u32 inRegAddr, u32 inStride, u32 inBlkCap, u32 inCount, void *dataBufferAddr, u32 rwOp, u32 *byteCount)
{
   int32_t status = 0;
	u32 ival =0;
	
	if(rwOp == READ_OP)
	{
		ival = nai_read32( s, inRegAddr + *byteCount * inStride );
		if(inBlkCap & PCK_CAPABLE)
		{
			/*data is packed, the first item is at bit(0-15), the second item is at bit(16-31)
			  use 32 bit on a 16Bit user data buffer, so reduced the number of bus access by half*/
			
			if(*byteCount == (inCount/2))
			{
				status = copy_to_user( (dataBufferAddr + *byteCount * REG_WIDTH_32BIT), &ival, REG_WIDTH_16BIT );
			}
			else
			{
				status = copy_to_user( (dataBufferAddr + *byteCount * REG_WIDTH_32BIT), &ival, REG_WIDTH_32BIT );
			}
		}
		else
		{
			status = copy_to_user( (dataBufferAddr + *byteCount * REG_WIDTH_16BIT), &ival, REG_WIDTH_16BIT );
		}
	}
	else  /*write op*/
	{
      if(inBlkCap & PCK_CAPABLE)
		{
			/*data is packed, the first item is at bit(0-15), the second item is at bit(16-31)
			  use 32 bit on a 16Bit user data buffer, so reduced the number of bus access by half
			*/
         ival = (u32)(*((u64*)(dataBufferAddr + *byteCount * REG_WIDTH_32BIT)));  
         if(*byteCount == (inCount/2))
			{
			   if( (inCount % 2) == 1)  /* odd */
            {
               ival = (uint16_t)(*((u64*)(dataBufferAddr + *byteCount * REG_WIDTH_32BIT)));
            }
         }
		}
		else
		{
		   ival = (uint16_t)(*((u64*)(dataBufferAddr + *byteCount * REG_WIDTH_16BIT)));  
      }
      nai_write32(s, inRegAddr + *byteCount * inStride, ival );
	}
	(*byteCount)++;
	
	return(status);
}

#ifdef CONFIG_COMPAT
/*rw32DataCompact()  
Perform read or write operations from user to/from Hardware when the user data buffer is 32 bit.
*/
static u32 rw32DataCompat(struct nai_state *s, u32 inRegAddr, u32 inStride, u32 inBlkCap, u32 inCount, void *dataBufferAddr, u32 rwOp, u32 *byteCount)
{
   int32_t status =0;
   u32 ival =0;
   u32 ival2 = 0;
   u32 temp =0;
   
	if(rwOp == READ_OP)
	{
		ival = nai_read32( s, inRegAddr + *byteCount * inStride );
		if(inBlkCap & PCK_CAPABLE)
		{
			/*data is packed, the first item is at bit(0-15), the second item is at bit(16-31)
			  use 32 bit on a 16Bit user data buffer, so reduced the number of bus access by half*/
         temp = ival & 0xffff;
         status = copy_to_user( compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2), &temp, REG_WIDTH_32BIT );
         if(inCount/2 != *byteCount)
         {
            temp = ival & 0xffff0000;
            temp >>= 16;
            status = copy_to_user( compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 + REG_WIDTH_32BIT), &temp, REG_WIDTH_32BIT );
         }
		}
		else
		{
			status = copy_to_user( compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT), &ival, REG_WIDTH_32BIT );
		}
	}
	else  /*write op*/
	{
      if(inBlkCap & PCK_CAPABLE)
		{
			ival = (uint32_t)(*(u64*)(compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 )));
         if(*byteCount != inCount/2)   
         {
            ival2 = (uint32_t)(*(u64*)(compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 + REG_WIDTH_32BIT )));
         }
         else
         {
            if(inCount % 2 == 0)
            {
               ival2 = (uint32_t)(*(u64*)(compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT * 2 + REG_WIDTH_32BIT )));
            }
         }
         nai_write32(s, inRegAddr + *byteCount * inStride,  ((ival2 & 0xffff) << 16) | (ival & 0xffff));
		}
		else
		{
		   ival = (uint32_t)(*(u64*)(compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT)));
         nai_write32(s, inRegAddr + *byteCount * inStride, ival );
		}
	}
	(*byteCount)++;
	
	return(status);
}


/*rw32DataCompact()  
Perform read or write operations from user to/from Hardware when the user data buffer is 16 bit.
*/
static u32 rw16DataCompat(struct nai_state *s, u32 inRegAddr, u32 inStride, u32 inBlkCap, u32 inCount, void *dataBufferAddr, u32 rwOp, u32 *byteCount)
{
   int32_t status = 0;
	u32 ival =0;
	
	if(rwOp == READ_OP)
	{
		ival = nai_read32( s, inRegAddr + *byteCount * inStride);
		if(inBlkCap & PCK_CAPABLE)
		{
			/*data is packed, the first item is at bit(0-15), the second item is at bit(16-31)
			  use 32 bit on a 16Bit user data buffer, so reduced the number of bus access by half*/
			
			if(*byteCount == (inCount/2))
			{
				status = copy_to_user( compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT), &ival, REG_WIDTH_16BIT );
			}
			else
			{
				status = copy_to_user( compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT), &ival, REG_WIDTH_32BIT );
			}
		}
		else
		{
			status = copy_to_user( compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_16BIT), &ival, REG_WIDTH_16BIT );
		}
	}
	else  /*write op*/
	{
      if(inBlkCap & PCK_CAPABLE)
		{
			/*data is packed, the first item is at bit(0-15), the second item is at bit(16-31)
			  use 32 bit on a 16Bit user data buffer, so reduced the number of bus access by half
			*/
         ival = (u32)(*((u64*)(compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT))));  
         if(*byteCount == (inCount/2))
			{
			   if( (inCount % 2) == 1)  /* odd */
            {
               ival = (uint16_t)(*((u64*)(compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_32BIT))));
            }
         }
		}
		else
		{
		   ival = (uint16_t)(*((u64*)(compat_ptr(dataBufferAddr + *byteCount * REG_WIDTH_16BIT))));  
      }
      nai_write32(s, inRegAddr + *byteCount * inStride, ival );
	}
	(*byteCount)++;
	
	return(status);
}
#endif

#if (KERNEL_VERSION(2,6,36) <= LINUX_VERSION_CODE)
static long device_ioctl(struct file *fp, unsigned int ioctl_num, unsigned long ioctl_param)
#else
static int device_ioctl(struct inode *ip, struct file *fp, unsigned int ioctl_num, unsigned long ioctl_param)
#endif
{
   int status;
   int ret = 0;
   unsigned long flags;
   NAI_CONFIG cfg;
   NAI_INT_CONFIG ic;
   NAI_WRITE_INPUT tmpIOBufferIn;
   NAI_READ_OUTPUT tmpIOBufferOut;
   NAI_INTERRUPT_DATA iData;
   NAI_BLOCK_DATA blockData;
   NAI_BLOCK_DATA_CAP blockDataCap;
   
   u32 ival = 0;
   u32 x;
   u32 ptrx=0;
   u32 temp=0;
   u32 blockFrames=0;
   u32 remainingDataCounts=0;
   u32 blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
   struct nai_state *s = fp->private_data;

#ifdef DEBUG_MODULE
   pr_info("ioctl_num=%x,IOCTL_NAI_READ_SHRM=%lx,IOCTL_NAI_WRITE_SHRM=%lx\n",ioctl_num,IOCTL_NAI_READ_SHRM,IOCTL_NAI_WRITE_SHRM);
#endif

   if (ioctl_num == IOCTL_NAI_READ_PORT_UCHAR)
   {
      status = copy_from_user( &tmpIOBufferOut, ((void*)ioctl_param), sizeof(tmpIOBufferOut) );
      ival = nai_read8( s, tmpIOBufferOut.data.CharData );
      tmpIOBufferOut.data.CharData = ival;
      status = __copy_to_user( ((void*)ioctl_param), &tmpIOBufferOut, sizeof(tmpIOBufferOut) );
      ret = status;
   }
   else
   if (ioctl_num == IOCTL_NAI_WRITE_PORT_UCHAR)
   {
      status = copy_from_user( &tmpIOBufferIn, ((void*)ioctl_param), sizeof(tmpIOBufferIn) );
      nai_write8(s, tmpIOBufferIn.PortNumber, tmpIOBufferIn.data.CharData );
      ret = status;
   }
   else
   if (ioctl_num == IOCTL_NAI_READ_PORT_USHORT)
   {
      status = copy_from_user( &tmpIOBufferOut, ((void*)ioctl_param), sizeof(tmpIOBufferOut) );
      ival = nai_read16( s, tmpIOBufferOut.data.ShortData );
      tmpIOBufferOut.data.ShortData = ival;
      status = __copy_to_user( ((void*)ioctl_param), &tmpIOBufferOut, sizeof(tmpIOBufferOut) );
      ret = status;
   }
   else
   if (ioctl_num == IOCTL_NAI_WRITE_PORT_USHORT)
   {
      status = copy_from_user( &tmpIOBufferIn, ((void*)ioctl_param), sizeof(tmpIOBufferIn) );
      nai_write16(s, tmpIOBufferIn.PortNumber, tmpIOBufferIn.data.ShortData );
      ret = status;
   }
   else
   if (ioctl_num == IOCTL_NAI_READ_PORT_32)
   {
      status = copy_from_user( &tmpIOBufferOut, ((void*)ioctl_param), sizeof(tmpIOBufferOut) );
      ival = nai_read32(s,tmpIOBufferOut.data.LongData);
      tmpIOBufferOut.data.LongData = ival;
      status = __copy_to_user( ((void*)ioctl_param), &tmpIOBufferOut, sizeof(tmpIOBufferOut) );
#ifdef DEBUG_MODULE
      if(tmpIOBufferOut.data.LongData!=0)
      { 
         pr_info("ioctl_num=%x,IOCTL_NAI_READ_PORT_32=%lx,read:0x%08lx\n",ioctl_num,IOCTL_NAI_READ_PORT_32,tmpIOBufferOut.data.LongData);
      }
#endif
     ret = status;
   }
   else 
   if (ioctl_num == IOCTL_NAI_WRITE_PORT_32)
   {
      status = copy_from_user( &tmpIOBufferIn, ((void*)ioctl_param), sizeof(tmpIOBufferIn) );
      nai_write32(s, tmpIOBufferIn.PortNumber, tmpIOBufferIn.data.LongData );
      ret = status;
   }
   else
   if (ioctl_num == IOCTL_NAI_READ_INFO) {
      cfg.BusNum = s->bus_number;
      cfg.PortCount = s->slot_number;   /* PortCount used as PCI Slot# */
      cfg.bPCIDevice = s->device_index; /* bPCIDevice used as device index */
      cfg.DeviceId = s->deviceId;
      cfg.BaseAdr = (ULONG)s->base_addr;

      status= __copy_to_user( ((void*)ioctl_param), &cfg, sizeof(cfg) );
      ret = status;
   }
/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
    else
    if (ioctl_num == IOCTL_NAI_READ_SHRM)
    {
       status = nai_shrm_access(s, (nai_shrm_data*)ioctl_param, 1);
       ret = status;
    } 
/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
   else
   if (ioctl_num == IOCTL_NAI_WRITE_SHRM)
   {
       status = nai_shrm_access(s, (nai_shrm_data*)ioctl_param, 0);
       ret = status;
   }
/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
   else
   if (ioctl_num == IOCTL_NAI_GEN_IRQ_SHRM)
   {
      nai_gen_shrm_interrupt(s);
      ret = 0;
   }
   else
   if (ioctl_num == IOCTL_NAI_INT_CONFIGURE) {
      status= copy_from_user(&ic,((void*)ioctl_param),sizeof(ic));
//IRQ Status and Vector address should NOT be configured from user application
#if 0
      s->irqReadAddr = ic.ReadAddr;
      s->irqWriteAddr = ic.WriteAddr;
#endif      
      s->irqWriteValue = ic.WriteValue;
      s->irqValue = 0;
      ret = status;
   }
   else
   if (ioctl_num == IOCTL_NAI_INT_DISABLE ) {
      s->irqUsrFlag = 2;
      wake_up_interruptible(&nai_wq);
      ret = 0;
   }
   else
   if (ioctl_num == IOCTL_NAI_INT_TRIGGER_ENABLE) {
		local_irq_save(flags);
		local_irq_disable();
		local_irq_restore(flags);
		local_irq_enable();		

		wait_event_interruptible(nai_wq, s->irqUsrFlag != 0);
		s->irqUsrFlag = 0;
		if(s->revId < NAI_HWREV_GEN5)
		{
			iData.data.UshortData = (USHORT)s->irqValue;;
		}
		else
		{
		 	iData.data.UlongData = (ULONG)s->irqValue32;
		}

		status= copy_to_user(((void*)ioctl_param),&iData,sizeof(iData));

		if (ival) {
			s->irqValue = 0;
			s->irqValue32 = 0;
		}
      ret = status;
   }
   else
   if (ioctl_num == IOCTL_NAI_HW_BLOCK_READ) 
   {
      ptrx=0;
      status= copy_from_user(&blockData,((void*)ioctl_param),sizeof(blockData));
      blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
      if(blockData.in_RegWidth == REG_WIDTH_32BIT)  /* only supported for 32 bit access for now*/
      {
		  if(blockData.in_DataWidth == 2)  /*assume pack, newer design should use IOCTL_NAI_HW_BLOCK_READ_CAP*/
        {
            blockFrames = getBlockFrame(blockData.in_Count, PCK_CAPABLE);
		      remainingDataCounts = getRemainingCounts(blockData.in_Count, PCK_CAPABLE);
            if(blockFrames)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/2, MAX_WORD_IN_BLK_FRAME);
               for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
               {
                  status = rw16Data(s, blockData.in_RegAddr, blockData.in_Stride, PCK_CAPABLE, blockData.in_Count, blockData.dataBuffer, READ_OP, &ptrx);
               }
            }
            if(remainingDataCounts)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/2, remainingDataCounts);
               for(x=0; (x< remainingDataCounts) && (status == 0); x++)
               { 			   
                  status = rw16Data(s, blockData.in_RegAddr, blockData.in_Stride, PCK_CAPABLE, blockData.in_Count, blockData.dataBuffer, READ_OP, &ptrx);
               }
            }
         }
         else  /*dataBuffer is 32bit*/
         {
            blockFrames = getBlockFrame(blockData.in_Count, BLK_CAPABLE | BLK_FIFO_CAPABLE);
		      remainingDataCounts = getRemainingCounts(blockData.in_Count, BLK_CAPABLE | BLK_FIFO_CAPABLE);
            if(blockFrames)
            {
               configBlockCtrl(s, blockData.in_Module, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Stride/4, MAX_WORD_IN_BLK_FRAME);
               for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
               {
                  status = rw32Data(s, blockData.in_RegAddr, blockData.in_Stride, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Count, blockData.dataBuffer, READ_OP, &ptrx);
               }
            }
            if(remainingDataCounts)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/4, remainingDataCounts);
               for(x=0; (x< remainingDataCounts) && (status == 0); x++)
               { 			   
                  status = rw32Data(s, blockData.in_RegAddr, blockData.in_Stride, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Count, blockData.dataBuffer, READ_OP, &ptrx);
               }
            }
         }
		blockCtrl = NAI_GEN5_BLK_CTRL_HW_DEFAULT;
      nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((blockData.in_Module-1)*REG_WIDTH_32BIT) , blockCtrl );               
	  }
     ret = status;
   }
   else 
   if (ioctl_num == IOCTL_NAI_HW_BLOCK_WRITE) 
   {
      ptrx=0;
      status= copy_from_user(&blockData,((void*)ioctl_param),sizeof(blockData));
      blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
      if(blockData.in_RegWidth == REG_WIDTH_32BIT)  /* only supported for 32 bit access for now*/
      {
		  if(blockData.in_DataWidth == 2)  /*assume pack, newer design should use IOCTL_NAI_HW_BLOCK_READ_CAP*/
        {
            blockFrames = getBlockFrame(blockData.in_Count, PCK_CAPABLE);
		      remainingDataCounts = getRemainingCounts(blockData.in_Count, PCK_CAPABLE);
            if(blockFrames)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/2, MAX_WORD_IN_BLK_FRAME);
               for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
               {
                  status = rw16Data(s, blockData.in_RegAddr, blockData.in_Stride, PCK_CAPABLE, blockData.in_Count, blockData.dataBuffer, WRITE_OP, &ptrx);
               }
            }
            if(remainingDataCounts)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/2, remainingDataCounts);
               for(x=0; (x< remainingDataCounts) && (status == 0); x++)
               { 			   
                  status = rw16Data(s, blockData.in_RegAddr, blockData.in_Stride, PCK_CAPABLE, blockData.in_Count, blockData.dataBuffer, WRITE_OP, &ptrx);
               }
            }
         }
         else  /*dataBuffer is 32bit*/
         {
            blockFrames = getBlockFrame(blockData.in_Count, BLK_CAPABLE | BLK_FIFO_CAPABLE);
		      remainingDataCounts = getRemainingCounts(blockData.in_Count, BLK_CAPABLE | BLK_FIFO_CAPABLE);
            if(blockFrames)
            {
               configBlockCtrl(s, blockData.in_Module, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Stride/4, MAX_WORD_IN_BLK_FRAME);
               for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
               {
                  status = rw32Data(s, blockData.in_RegAddr, blockData.in_Stride, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Count, blockData.dataBuffer, WRITE_OP, &ptrx);
               }
            }
            if(remainingDataCounts)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/4, remainingDataCounts);
               for(x=0; (x< remainingDataCounts) && (status == 0); x++)
               { 			   
                  status = rw32Data(s, blockData.in_RegAddr, blockData.in_Stride, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Count, blockData.dataBuffer, WRITE_OP, &ptrx);
               }
            }
         }
		blockCtrl = NAI_GEN5_BLK_CTRL_HW_DEFAULT;
      nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((blockData.in_Module-1)*REG_WIDTH_32BIT) , blockCtrl );               
	  }
     ret = status;
   } 
   else
   if (ioctl_num == IOCTL_NAI_HW_BLOCK_READ_CAP)
   {
      ptrx=0;
      status= copy_from_user(&blockDataCap,((void*)ioctl_param),sizeof(blockDataCap));
      blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
      if(blockDataCap.in_RegWidth == REG_WIDTH_32BIT)  /* only supported for 32 bit access for now*/
      {
		   blockFrames = getBlockFrame(blockDataCap.in_Count, blockDataCap.in_Cap);
         remainingDataCounts = getRemainingCounts(blockDataCap.in_Count, blockDataCap.in_Cap);
         if(blockFrames)
         {
	         configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, MAX_WORD_IN_BLK_FRAME);
            for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
            {
				   if(blockDataCap.in_DataWidth == 2)  /* 16bit */
               {
                  status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
	            }
               else                         /* 32bit */
               {
                  status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
	            }
	         }
         }
         if(remainingDataCounts)
         {
            if(blockDataCap.in_Cap & PCK_CAPABLE)
		      {
               temp = blockDataCap.in_Count - ptrx * 2;
			      if((blockDataCap.in_Count % 2) == 0)  /*is even*/
			      {
				      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts); 
				      for(x=0; (x<remainingDataCounts) && (status == 0); x++)
				      {
					      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
					      {
						      status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
					      }
					      else                         /* 32bit */
					      {
						      status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
					      }
				      }
			      }
			      else /*odd*/
			      {
				      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts-1); 
				      for(x=0; (x<remainingDataCounts-1) && (status == 0); x++)
				      {
					      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
					      {
						      status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
					      }
					      else                         /* 32bit */
					      {
						      status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
					      }
				      }
				      configBlockCtrl(s, blockDataCap.in_Module, (blockDataCap.in_Cap & (~PCK_CAPABLE)), blockDataCap.in_Stride/4, 1); 
                  if(blockDataCap.in_DataWidth == 2)  /* 16bit */
				      {
					      ptrx *= 2;
					      status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap ^ PCK_CAPABLE, 1, blockDataCap.dataBuffer, READ_OP, &ptrx);
   			      }
				      else                                /* 32bit */
				      {
					      ptrx *= 2;
                     status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap ^ PCK_CAPABLE, 1, blockDataCap.dataBuffer, READ_OP, &ptrx);
				      }
			      }
            }
            else
            {
               temp = blockDataCap.in_Count - ptrx;
			      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts); 
			      for(x=0; (x<remainingDataCounts) && (status == 0); x++)
			      {
				      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
				      {
					      status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
				      }
				      else                                /* 32bit */
				      {
					      status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
				      }
			      }
            }
         }
      blockCtrl = NAI_GEN5_BLK_CTRL_HW_DEFAULT;
      nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((blockDataCap.in_Module-1)*REG_WIDTH_32BIT) , blockCtrl );
      }
      ret = status;
   }
   else 
   if (ioctl_num == IOCTL_NAI_HW_BLOCK_WRITE_CAP)
   {
      ptrx=0;
      status= copy_from_user(&blockDataCap,((void*)ioctl_param),sizeof(blockDataCap));
      blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
      if(blockDataCap.in_RegWidth == REG_WIDTH_32BIT)  /* only supported for 32 bit access for now*/
      {
         blockFrames = getBlockFrame(blockDataCap.in_Count, blockDataCap.in_Cap);
         remainingDataCounts = getRemainingCounts(blockDataCap.in_Count, blockDataCap.in_Cap);
         if(blockFrames)
         {
            configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, MAX_WORD_IN_BLK_FRAME);
            for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
            {
               if(blockDataCap.in_DataWidth == 2)  /* 16bit */
               {
                  status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
               }
               else                         /* 32bit */
               {
                  status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
               }
            }
         }
	      if(remainingDataCounts)
	      {
		      if(blockDataCap.in_Cap & PCK_CAPABLE)
		      {
			      temp = blockDataCap.in_Count - ptrx * 2;
			      if((blockDataCap.in_Count % 2) == 0)  /*is even*/
			      {
				      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts); 
				      for(x=0; (x<remainingDataCounts) && (status == 0); x++)
				      {
					      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
					      {
						      status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
					      }
					      else                         /* 32bit */
					      {
						      status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
					      }
				      }
			      }
			      else /*odd*/
			      {
				      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts-1); 
				      for(x=0; (x<remainingDataCounts-1) && (status == 0); x++)
				      {
					      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
					      {
						      status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
					      }
					      else                         /* 32bit */
					      {
						      status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
					      }
				      }
				      configBlockCtrl(s, blockDataCap.in_Module, (blockDataCap.in_Cap & (~PCK_CAPABLE)), blockDataCap.in_Stride/4, 1); 
                  if(blockDataCap.in_DataWidth == 2)  /* 16bit */
				      {
					      ptrx *= 2;
					      status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap ^ PCK_CAPABLE, 1, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
   			          }
				      else                                /* 32bit */
				      {
					      ptrx *= 2;
                     status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap ^ PCK_CAPABLE, 1, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
				      }
			      }
		      }
		      else /*non packed*/
		      {
			      temp = blockDataCap.in_Count - ptrx;
			      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts); 
			      for(x=0; (x<remainingDataCounts) && (status == 0); x++)
			      {
				      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
				      {
					      status = rw16Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
				      }
				      else                                /* 32bit */
				      {
					      status = rw32Data(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
				      }
			      }
		      }
         }
      blockCtrl = NAI_GEN5_BLK_CTRL_HW_DEFAULT;
      nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((blockDataCap.in_Module-1)*REG_WIDTH_32BIT) , blockCtrl );
      }
	  ret = status;
   }
   else 
   if (ioctl_num == IOCTL_NAI_GET_PCI_MOD_REV) 
   {
      ret = get_pci_module_revision((NAI_MODULE_PCI_REVISION *)ioctl_param);
   }
   else
   {
      pr_err("not defined ioctl_num:0x%x\n",ioctl_num);
      ret = -EFAULT;
   }
   return ret;
}

#ifdef CONFIG_COMPAT

static int nai_shrm_compat_access(struct nai_state *s, unsigned long arg, u32 rd)
{
   int err = 0;
   u32 oklen = 0;

   nai_shrm_data local_nai_shrm_data;

   err=copy_from_user(&local_nai_shrm_data,(void*)compat_ptr(arg),sizeof(nai_shrm_data));
   if( err != 0 )
   {
       pr_err("copy_from_user(&local_nai_shrm_data,(void*)compat_ptr(arg),sizeof(nai_shrm_data))\n");
       goto exit;
   }

   if(s->shrm_bar_enabled != NAI_GEN5_SHRM_BAR_ENABLE)
   {
      pr_err("device s->deviceId=0x%x not enabled\n",s->deviceId);
      err = -EFAULT;
      goto exit;
   }

   /* Ensure we are starting at a valid location */
   if ((local_nai_shrm_data.offset < 0) || (local_nai_shrm_data.offset > (s->shrm_size - 1)))
   {
      pr_err("invalid ofset\n");
      err = -EFAULT;
      goto exit;
   }

   /* Ensure not reading past end of the image */
   if ((local_nai_shrm_data.offset + local_nai_shrm_data.len) > s->shrm_size)
   {
      oklen =  s->shrm_size - local_nai_shrm_data.offset;
      /*pr_info("local_nai_shrm_data.offset=%u,local_nai_shrm_data.len=%u,oklen=%u\n",local_nai_shrm_data.offset,local_nai_shrm_data.len,oklen);*/
   }
   else
   {
      oklen = local_nai_shrm_data.len;
      /*pr_info("local_nai_shrm_data.offset=%u,local_nai_shrm_data.len=%u,oklen=%u\n",local_nai_shrm_data.offset,local_nai_shrm_data.len,oklen);*/
   }

   if (rd)
   {  /*read op*/
      err = copy_to_user((void*)((compat_ptr)(local_nai_shrm_data.pbuf)), s->shrm_base_addr+local_nai_shrm_data.offset, oklen);
      if(err)
      {
         pr_err("failed copy_to_user length: %d \n",oklen);
         oklen = 0;
      }
#ifdef DEBUG_MODULE
 //too many log info in /var/log/messages
      else
      {
         pr_info("read %d bytes from phy shrm offset 0x%0x, vir addr=%p\n",oklen,local_nai_shrm_data.offset,s->shrm_base_addr);
         printk_hex_info(s->shrm_base_addr+local_nai_shrm_data.offset,oklen<64?oklen:(oklen-1)%64+1);//not necassary to dump all read info
      }
#endif
   }
   else
   {  /*write op*/
      err = copy_from_user(s->shrm_base_addr+local_nai_shrm_data.offset, (void*)((compat_ptr)(local_nai_shrm_data.pbuf)), oklen);   
      if (err)
      {
         pr_err("failed copy_from_user: length %d \n",oklen);
         oklen = 0;
      }
#ifdef DEBUG_MODULE
      else
      {
         pr_info("write %d bytes to phy shrm offset 0x%0x, vir addr=%p\n",oklen,local_nai_shrm_data.offset,s->shrm_base_addr);
         printk_hex_info(s->shrm_base_addr+local_nai_shrm_data.offset,oklen);
      }
#endif
   }

exit:
   return err;
}

static long device_compat_ioctl(struct file *fp, unsigned int ioctl_num, unsigned long ioctl_param)
{
   int status;
   int ret = 0;
   unsigned long flags;
   NAI_READ_OUTPUT rd;
   NAI_WRITE_INPUT wr;
   NAI_DEVICE_CONFIG cfg;
   NAI_INT_CONFIG ic;
   NAI_INTERRUPT_DATA iData;
   NAI_BLOCK_DATA blockData;
   u32 ival = 0;
   u32 x;
   u32 ptrx=0;
   u32 temp=0;
   u32 blockFrames=0;
   u32 remainingDataCounts=0;
   u32 blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
   u32 blockPackDataTwo;
   struct nai_state *s = fp->private_data;
   
   if (ioctl_num == IOCTL_NAI_READ_PORT_UCHAR) {
      status= copy_from_user(&rd,((void*)compat_ptr(ioctl_param)),sizeof(rd));
      ival = nai_read8(s, rd.data.CharData );
      rd.data.CharData = ival;
      status= __copy_to_user(((void*)compat_ptr(ioctl_param)),&rd,sizeof(rd));
   }

   if (ioctl_num == IOCTL_NAI_WRITE_PORT_UCHAR) {
      status= copy_from_user(&wr,((void*)compat_ptr(ioctl_param)),sizeof(wr));
      nai_write8(s, wr.PortNumber, wr.data.CharData );
   }
   if (ioctl_num == IOCTL_NAI_READ_PORT_USHORT) {
      status= copy_from_user(&rd,((void*)compat_ptr(ioctl_param)),sizeof(rd));
      ival = nai_read16(s, rd.data.ShortData );
      rd.data.ShortData = ival;
      status= __copy_to_user(((void*)compat_ptr(ioctl_param)),&rd,sizeof(rd));
   }
   if (ioctl_num == IOCTL_NAI_WRITE_PORT_USHORT) {
      status= copy_from_user(&wr,((void*)compat_ptr(ioctl_param)),sizeof(wr));
      nai_write16(s, wr.PortNumber, wr.data.ShortData );
   }
   if (ioctl_num == IOCTL_NAI_READ_PORT_32) {
      status= copy_from_user(&rd,((void*)compat_ptr(ioctl_param)),sizeof(rd));
      ival = nai_read32(s, rd.data.LongData );
      rd.data.LongData = ival;
      status= __copy_to_user(((void*)compat_ptr(ioctl_param)),&rd,sizeof(rd));
   }
   if (ioctl_num == IOCTL_NAI_WRITE_PORT_32) {
      status= copy_from_user(&wr,((void*)compat_ptr(ioctl_param)),sizeof(wr));
      nai_write32(s, wr.PortNumber, wr.data.LongData );
   }
/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
   if (ioctl_num == IOCTL_NAI_READ_SHRM)
   {
      nai_shrm_compat_access(s, (nai_shrm_data*)compat_ptr(ioctl_param), 1);
   }
   
/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
   if (ioctl_num == IOCTL_NAI_WRITE_SHRM)
   {
      nai_shrm_compat_access(s, (nai_shrm_data*)compat_ptr(ioctl_param), 0);
   }
   
/*TODO: 
* This is a temporary hack to support download via
* bar1(cPCI dev)/bar2(PCIe dev) on Gen5 PCI card.
* We have to create another PCI device driver 
* for Gen5 device to handle normal I/O operation and 
* download operation
*/
   if (ioctl_num == IOCTL_NAI_GEN_IRQ_SHRM)
   {
      nai_gen_shrm_interrupt(s);
   }
      
   if (ioctl_num == IOCTL_NAI_READ_INFO)
   {
      cfg.BusNum = s->bus_number;
      cfg.PortCount = s->slot_number;   /* PortCount used as PCI Slot# */
      cfg.bPCIDevice = s->device_index; /* bPCIDevice used as device index */
      cfg.DeviceId = s->deviceId;
      status= __copy_to_user(((void*)compat_ptr(ioctl_param)),&cfg,sizeof(cfg));
   }
   
   if (ioctl_num == IOCTL_NAI_GEN_IRQ_SHRM)
      nai_gen_shrm_interrupt(s);
   
   if (ioctl_num == IOCTL_NAI_INT_CONFIGURE)
   {
      status= copy_from_user(&ic,((void*)compat_ptr(ioctl_param)),sizeof(ic));
//IRQ Status and Vector address should NOT be configured from user application
#if 0
      s->irqReadAddr = ic.ReadAddr;
      s->irqWriteAddr = ic.WriteAddr;
#endif      
      s->irqWriteValue = ic.WriteValue;
      s->irqValue = 0;
   }

   if (ioctl_num == IOCTL_NAI_INT_TRIGGER_ENABLE)
   {
      local_irq_save(flags);
      local_irq_disable();
      local_irq_restore(flags);
      local_irq_enable();

      wait_event_interruptible(nai_wq, s->irqUsrFlag != 0);
      s->irqUsrFlag = 0;
      if(s->revId < NAI_HWREV_GEN5)
      {
         iData.data.UshortData = (USHORT)s->irqValue;
      }
      else
      {   
         iData.data.UlongData = (ULONG)s->irqValue32;
      }

      status = __copy_to_user(((void*)compat_ptr(ioctl_param)),&iData,sizeof(iData));
 
      if (ival)
      {
         s->irqValue = 0;
         s->irqValue32 = 0; 
      }
   }
   if (ioctl_num == IOCTL_NAI_HW_BLOCK_READ) 
   {
      ptrx=0;
      status= copy_from_user(&blockData,((void*)compat_ptr(ioctl_param)),sizeof(blockData));
      blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
      if(blockData.in_RegWidth == REG_WIDTH_32BIT)  /* only supported for 32 bit access for now*/
      {
        if(blockData.in_DataWidth == 2)  /*assume pack, newer design should use IOCTL_NAI_HW_BLOCK_READ_CAP*/
        {
            blockFrames = getBlockFrame(blockData.in_Count, PCK_CAPABLE);
            remainingDataCounts = getRemainingCounts(blockData.in_Count, PCK_CAPABLE);
            if(blockFrames)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/2, MAX_WORD_IN_BLK_FRAME);
               for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
               {
                  status = rw16DataCompat(s, blockData.in_RegAddr, blockData.in_Stride, PCK_CAPABLE, blockData.in_Count, blockData.dataBuffer, READ_OP, &ptrx);
               }
            }
            if(remainingDataCounts)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/2, remainingDataCounts);
               for(x=0; (x< remainingDataCounts) && (status == 0); x++)
               { 			   
                  status = rw16DataCompat(s, blockData.in_RegAddr, blockData.in_Stride, PCK_CAPABLE, blockData.in_Count, blockData.dataBuffer, READ_OP, &ptrx);
               }
            }
        }
        else  /*dataBuffer is 32bit*/
        {
           blockFrames = getBlockFrame(blockData.in_Count, BLK_CAPABLE | BLK_FIFO_CAPABLE);
           remainingDataCounts = getRemainingCounts(blockData.in_Count, BLK_CAPABLE | BLK_FIFO_CAPABLE);
           if(blockFrames)
           {
               configBlockCtrl(s, blockData.in_Module, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Stride/4, MAX_WORD_IN_BLK_FRAME);
               for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
               {
                  status = rw32DataCompat(s, blockData.in_RegAddr, blockData.in_Stride, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Count, blockData.dataBuffer, READ_OP, &ptrx);
               }
            }
            if(remainingDataCounts)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/4, remainingDataCounts);
               for(x=0; (x< remainingDataCounts) && (status == 0); x++)
               { 			   
                  status = rw32DataCompat(s, blockData.in_RegAddr, blockData.in_Stride, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Count, blockData.dataBuffer, READ_OP, &ptrx);
               }
            }
         }
         blockCtrl = NAI_GEN5_BLK_CTRL_HW_DEFAULT;
         nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((blockData.in_Module-1)*REG_WIDTH_32BIT) , blockCtrl );               
       }
       ret = status;
   }
   
   if (ioctl_num == IOCTL_NAI_HW_BLOCK_WRITE) 
   {
      ptrx=0;
      status= copy_from_user(&blockData,((void*)compat_ptr(ioctl_param)),sizeof(blockData));
      blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
      if(blockData.in_RegWidth == REG_WIDTH_32BIT)  /* only supported for 32 bit access for now*/
      {
         if(blockData.in_DataWidth == 2)  /*assume pack, newer design should use IOCTL_NAI_HW_BLOCK_READ_CAP*/
         {
            blockFrames = getBlockFrame(blockData.in_Count, PCK_CAPABLE);
            remainingDataCounts = getRemainingCounts(blockData.in_Count, PCK_CAPABLE);
            if(blockFrames)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/2, MAX_WORD_IN_BLK_FRAME);
               for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
               {
                  status = rw16DataCompat(s, blockData.in_RegAddr, blockData.in_Stride, PCK_CAPABLE, blockData.in_Count, blockData.dataBuffer, WRITE_OP, &ptrx);
               }
            }
            if(remainingDataCounts)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/2, remainingDataCounts);
               for(x=0; (x< remainingDataCounts) && (status == 0); x++)
               { 			   
                  status = rw16DataCompat(s, blockData.in_RegAddr, blockData.in_Stride, PCK_CAPABLE, blockData.in_Count, blockData.dataBuffer, WRITE_OP, &ptrx);
               }
            }
         }
         else  /*dataBuffer is 32bit*/
         {
            blockFrames = getBlockFrame(blockData.in_Count, BLK_CAPABLE | BLK_FIFO_CAPABLE);
            remainingDataCounts = getRemainingCounts(blockData.in_Count, BLK_CAPABLE | BLK_FIFO_CAPABLE);
            if(blockFrames)
            {
               configBlockCtrl(s, blockData.in_Module, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Stride/4, MAX_WORD_IN_BLK_FRAME);
               for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
               {
                  status = rw32DataCompat(s, blockData.in_RegAddr, blockData.in_Stride, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Count, blockData.dataBuffer, WRITE_OP, &ptrx);
               }
            }
            if(remainingDataCounts)
            {
               configBlockCtrl(s, blockData.in_Module, PCK_CAPABLE, blockData.in_Stride/4, remainingDataCounts);
               for(x=0; (x< remainingDataCounts) && (status == 0); x++)
               { 			   
                  status = rw32DataCompat(s, blockData.in_RegAddr, blockData.in_Stride, BLK_CAPABLE | BLK_FIFO_CAPABLE, blockData.in_Count, blockData.dataBuffer, WRITE_OP, &ptrx);
               }
            }
         }
         blockCtrl = NAI_GEN5_BLK_CTRL_HW_DEFAULT;
         nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((blockData.in_Module-1)*REG_WIDTH_32BIT) , blockCtrl );               
     }
     ret = status;
   } 

   if (ioctl_num == IOCTL_NAI_HW_BLOCK_READ_CAP)
   {
      ptrx=0;
      status= copy_from_user(&blockDataCap,((void*)compat_ptr(ioctl_param)),sizeof(blockDataCap));
      blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
      if(blockDataCap.in_RegWidth == REG_WIDTH_32BIT)  /* only supported for 32 bit access for now*/
      {
		   blockFrames = getBlockFrame(blockDataCap.in_Count, blockDataCap.in_Cap);
         remainingDataCounts = getRemainingCounts(blockDataCap.in_Count, blockDataCap.in_Cap);
         if(blockFrames)
         {
	         configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, MAX_WORD_IN_BLK_FRAME);
            for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
            {
				   if(blockDataCap.in_DataWidth == 2)  /* 16bit */
               {
					status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
	            }
               else                         /* 32bit */
               {
					status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
	            }
	         }
         }
         if(remainingDataCounts)
         {
            if(blockDataCap.in_Cap & PCK_CAPABLE)
		      {
               temp = blockDataCap.in_Count - ptrx * 2;
			      if((blockDataCap.in_Count % 2) == 0)  /*is even*/
			      {
				      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts); 
				      for(x=0; (x<remainingDataCounts) && (status == 0); x++)
				      {
					      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
					      {
					status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
					      }
					      else                         /* 32bit */
					      {
					status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
					      }
				      }
			      }
			      else /*odd*/
			      {
				      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts-1); 
				      for(x=0; (x<remainingDataCounts-1) && (status == 0); x++)
				      {
					      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
					      {
						      status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
					      }
					      else                         /* 32bit */
					      {
						      status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
					      }
				      }
				      configBlockCtrl(s, blockDataCap.in_Module, (blockDataCap.in_Cap & (~PCK_CAPABLE)), blockDataCap.in_Stride/4, 1); 
                  if(blockDataCap.in_DataWidth == 2)  /* 16bit */
				      {
					      ptrx *= 2;
					      status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap ^ PCK_CAPABLE, 1, blockDataCap.dataBuffer, READ_OP, &ptrx);
   			      }
				      else                                /* 32bit */
				      {
					      ptrx *= 2;
                     status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap ^ PCK_CAPABLE, 1, blockDataCap.dataBuffer, READ_OP, &ptrx);
				      }
			      }
            }
            else
            {
               temp = blockDataCap.in_Count - ptrx;
			      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts); 
			      for(x=0; (x<remainingDataCounts) && (status == 0); x++)
			      {
				      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
				      {
					      status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
				      }
				      else                                /* 32bit */
				      {
					      status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, READ_OP, &ptrx);
				      }
			      }
            }
         }
      blockCtrl = NAI_GEN5_BLK_CTRL_HW_DEFAULT;
      nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((blockDataCap.in_Module-1)*REG_WIDTH_32BIT) , blockCtrl );
      }
      ret = status;
   }
   
   if (ioctl_num == IOCTL_NAI_HW_BLOCK_WRITE_CAP)
   {
      ptrx=0;
      status= copy_from_user(&blockDataCap,((void*)compat_ptr(ioctl_param)),sizeof(blockDataCap));
      blockCtrl = NAI_GEN5_BLK_CTRL_NULL;
      if(blockDataCap.in_RegWidth == REG_WIDTH_32BIT)  /* only supported for 32 bit access for now*/
      {
         blockFrames = getBlockFrame(blockDataCap.in_Count, blockDataCap.in_Cap);
         remainingDataCounts = getRemainingCounts(blockDataCap.in_Count, blockDataCap.in_Cap);
         if(blockFrames)
         {
            configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, MAX_WORD_IN_BLK_FRAME);
            for(x=0; (x < blockFrames *  MAX_DWORD_IN_BLK_FRAME) && (status == 0); x++)
            {
               if(blockDataCap.in_DataWidth == 2)  /* 16bit */
               {
                  status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
               }
               else                         /* 32bit */
               {
                  status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
               }
            }
         }
	      if(remainingDataCounts)
	      {
		      if(blockDataCap.in_Cap & PCK_CAPABLE)
		      {
			      temp = blockDataCap.in_Count - ptrx * 2;
			      if((blockDataCap.in_Count % 2) == 0)  /*is even*/
			      {
				      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts); 
				      for(x=0; (x<remainingDataCounts) && (status == 0); x++)
				      {
					      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
					      {
						      status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
					      }
					      else                         /* 32bit */
					      {
						      status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
					      }
				      }
			      }
			      else /*odd*/
			      {
				      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts-1); 
				      for(x=0; (x<remainingDataCounts-1) && (status == 0); x++)
				      {
					      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
					      {
						      status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
					      }
					      else                         /* 32bit */
					      {
						      status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
					      }
				      }
				      configBlockCtrl(s, blockDataCap.in_Module, (blockDataCap.in_Cap & (~PCK_CAPABLE)), blockDataCap.in_Stride/4, 1); 
                  if(blockDataCap.in_DataWidth == 2)  /* 16bit */
				      {
					      ptrx *= 2;
					      status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap ^ PCK_CAPABLE, 1, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
   			          }
				      else                                /* 32bit */
				      {
					      ptrx *= 2;
                     status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap ^ PCK_CAPABLE, 1, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
				      }
			      }
		      }
		      else /*non packed*/
		      {
			      temp = blockDataCap.in_Count - ptrx;
			      configBlockCtrl(s, blockDataCap.in_Module, blockDataCap.in_Cap, blockDataCap.in_Stride/4, remainingDataCounts); 
			      for(x=0; (x<remainingDataCounts) && (status == 0); x++)
			      {
				      if(blockDataCap.in_DataWidth == 2)  /* 16bit */
				      {
					      status = rw16DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
				      }
				      else                                /* 32bit */
				      {
					      status = rw32DataCompat(s, blockDataCap.in_RegAddr, blockDataCap.in_Stride, blockDataCap.in_Cap, blockDataCap.in_Count, blockDataCap.dataBuffer, WRITE_OP, &ptrx);
				      }
			      }
		      }
         }
		blockCtrl = NAI_GEN5_BLK_CTRL_HW_DEFAULT;
		nai_write32(s, NAI_GEN5_BLK_XFER_OFFSET_ADD +  ((blockDataCap.in_Module-1)*REG_WIDTH_32BIT) , blockCtrl );
      }
      ret = status;
   }
   
   if (ioctl_num == IOCTL_NAI_GET_PCI_MOD_REV) 
      ret = get_pci_module_revision((NAI_MODULE_PCI_REVISION *)compat_ptr(ioctl_param));

   return ret;
}

#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

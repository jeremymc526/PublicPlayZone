/*
 * Support for the Inicore VME Sysytem Controller
 *
 * Author: NAII
 * Copyright 2014 North Atlantic Industries
 *
 * Based on work by Martyn Welch <martyn.welch@ge.com>
 * Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc. 
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/byteorder/generic.h>
#include <linux/vme.h>

#include "../vme_bridge.h"
#include "vme_inicore.h"


#undef DEBUG
//#define DEBUG
#ifdef DEBUG
#define DEBUGF(x...) printk(x)
#else
#define DEBUGF(x...)
#endif /* DEBUG */

/* Module parameter */
static bool err_chk;
static int geoid = 1;

static const char driver_name[] = "vme_inicore";

/*
 * Wake up IACK queue.
 */
/*TODO: Not implemented yet*/
#if 0
static u32 inicore_IACK_irqhandler(struct inicore_driver *bridge)
{
	wake_up(&bridge->iack_queue);

	return INICORE_LCSR_INTC_IACKC;
}
#endif

/*Clear all pending IRQ*/
static void inicore_clr_pending_irq(struct vme_bridge *inicore_bridge, u32 clrIrq){
	
	struct inicore_driver *bridge;

	bridge = inicore_bridge->driver_priv;

	/*enable local csr access*/
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);
	
	DEBUGF("%s:Clear INICORE_LCSR_INT_STATUS:%x \n",__func__,clrIrq);	
	/*clear pending irq*/
	iowrite32be(clrIrq,bridge->port + INICORE_LCSR_INT_STATUS);
	
	/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);
	
	return;
}

/*
 * Calling VME bus interrupt callback if provided.
 */
static u32 inicore_VIRQ_irqhandler(struct vme_bridge *inicore_bridge,
	u32 stat)
{
   unsigned int vector, level, serviced = 0;
   struct inicore_driver *bridge;

   bridge = inicore_bridge->driver_priv;

   for (level = 7; level > 0; level--)
   {
      if (stat & INICORE_LCSR_IS_IRQn[level])
      {
   
         /*TODO: Should we check for VME_IRN1_ERR?*/
         vector = ioread32be(bridge->port + INICORE_LCSR_VME_IRQn_STAT[level]);

         if((vector&INICORE_LCSR_VME_IRQn_STAT_D08_VINTHn_ERR)!=0)
         {
            pr_err("level: %d IACK cycle bus err.\n ",level);
         }
         else
         {

            /* Clear serviced interrupt */
            iowrite32be(INICORE_LCSR_IS_IRQn[level], bridge->port + INICORE_LCSR_INT_STATUS);


            if(vector < VME_NUM_STATUSID)
            {
               vme_irq_handler(inicore_bridge, level, vector);
            }

            serviced |= INICORE_LCSR_IS_IRQn[level];
         }
      }
   }

	return serviced;
}

/*
 * Top level interrupt handler.  Clears appropriate interrupt status bits and
 * then calls appropriate sub handler(s).
 */
static irqreturn_t inicore_irqhandler(int irq, void *ptr)
{
	u32 stat = 0, enable = 0, savedCfgReg = 0, serviced = 0; 
	
	struct vme_bridge *inicore_bridge;
	struct inicore_driver *bridge;

	inicore_bridge = ptr;
	bridge = inicore_bridge->driver_priv;

	/* Enable Local CSR acces */
	savedCfgReg = ioread32(bridge->core_cfg_base);
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);
	
	/* Determine which interrupts are unmasked and set */
	enable = ioread32be(bridge->port + INICORE_LCSR_INT_EBL);
	stat = ioread32be(bridge->port + INICORE_LCSR_INT_STATUS);

	/* Only look at unmasked interrupts */
	stat &= enable;

	if (unlikely(!stat))
		return IRQ_NONE;
	
	DEBUGF("%s: stat:0x%x \n",__func__,stat);
	/* Call subhandlers as appropriate */
	/* VME bus irqs */
	if (stat & (INICORE_LCSR_IS_IRQ7 | INICORE_LCSR_IS_IRQ6 |
			INICORE_LCSR_IS_IRQ5 | INICORE_LCSR_IS_IRQ4 |
			INICORE_LCSR_IS_IRQ3 | INICORE_LCSR_IS_IRQ2 |
			INICORE_LCSR_IS_IRQ1)){
		serviced |= inicore_VIRQ_irqhandler(inicore_bridge, stat);
	}
	
	DEBUGF("%s: serviced:0x%x \n",__func__,serviced);
	
	/* Restore core cfg register */
	iowrite32be(savedCfgReg,bridge->core_cfg_base);
	
	return IRQ_HANDLED;
}

static int inicore_irq_init(struct vme_bridge *inicore_bridge)
{
	int result;
	u32 tmp;
	struct inicore_driver *bridge;

	bridge = inicore_bridge->driver_priv;
	
	/*TODO: Enable VME SW and USER IRQ */
	result = request_irq(bridge->irq,
			     inicore_irqhandler,
			     IRQF_SHARED,
			     driver_name, inicore_bridge);			 
			     
	if (result) {
		dev_err(inicore_bridge->parent, "Can't get assigned irq "
			"vector %02X\n", bridge->irq);
		return result;
	}
	
	/*TODO: Clear all power up pending IRQ*/
	tmp = INICORE_LCSR_IS_MBOX3 | INICORE_LCSR_IS_MBOX2 | 
			INICORE_LCSR_IS_MBOX1 | INICORE_LCSR_IS_MBOX0 |
			INICORE_LCSR_IS_VTIERR | INICORE_LCSR_IS_VBERR |
			INICORE_LCSR_IS_DMAERR | INICORE_LCSR_IS_DMADONE |
			INICORE_LCSR_IS_SWIACK | INICORE_LCSR_IS_IRQ1 |
			INICORE_LCSR_IS_IRQ2 | INICORE_LCSR_IS_IRQ3 |
			INICORE_LCSR_IS_IRQ4 | INICORE_LCSR_IS_IRQ5 |
			INICORE_LCSR_IS_IRQ6 | INICORE_LCSR_IS_IRQ7 |
			INICORE_LCSR_IS_SYSFAL | INICORE_LCSR_IS_ACFAL;
			
	inicore_clr_pending_irq(inicore_bridge, tmp);
	
	return 0;
}

static void inicore_irq_exit(struct vme_bridge *inicore_bridge)
{
	struct inicore_driver *bridge = inicore_bridge->driver_priv;
	/*TODO: We need semphore*/
	
	/*enable local csr access*/
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);
	
	/* Turn off interrupts */
	iowrite32be(0x0, bridge->port + INICORE_LCSR_INT_EBL);
	
	
	/* Clear all interrupts */
	iowrite32be(0xFFFFFFFF, bridge->port + INICORE_LCSR_INT_STATUS);
	
		/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);
	
	/* Detach interrupt handler */
	free_irq(bridge->irq, bridge);
}

/*
 * Check to see if an IACk has been received, return true (1) or false (0).
 */
static int inicore_iack_received(struct inicore_driver *bridge)
{
	u32 tmp;

	tmp = ioread32be(bridge->port + INICORE_LCSR_VINT_STATUS);
	if (tmp & INICORE_LCSR_VINT_STATUS_SWIRQ)
		return 0;
	else
		return 1;
}

/*
 * Configure VME interrupt
 */
static void inicore_irq_set(struct vme_bridge *inicore_bridge, int level,
	int state, int sync)
{
	u32 tmp;
	struct inicore_driver *bridge;

	bridge = inicore_bridge->driver_priv;

	/*enable local csr access*/
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);
	
	tmp = ioread32be(bridge->port + INICORE_LCSR_INT_EBL);
	DEBUGF("%s:get INT_EBL_REG:0x%x lvl:%d\n",__func__,tmp,level);	
	if (state == 0) {
		
		tmp &= ~INICORE_LCSR_IE_IRQn[level];
		DEBUGF("%s: clear INT_EBL_REG:0x%x lvl:%d\n",__func__,tmp,level);	
		iowrite32be(tmp, bridge->port + INICORE_LCSR_INT_EBL);
		
		if (sync != 0) {
			synchronize_irq(bridge->irq);
		}
	} else {
		
		tmp |= INICORE_LCSR_IE_IRQn[level];
		DEBUGF("%s: set INT_EBL_REG:0x%x lvl:%d\n",__func__,tmp,level);
		iowrite32be(tmp, bridge->port + INICORE_LCSR_INT_EBL);
		
		/*TODO: as of now we are locking iack cycle to 8bit*/
		tmp = ioread32be(bridge->port + INICORE_LCSR_VINT_IRQH_CMD);
		DEBUGF("%s: get VINT_IRQH_CMD:0x%x \n",__func__,tmp);
		tmp |= INICORE_LCSR_VINT_IRQH_INTx_TYP_D8;
		DEBUGF("%s: set VINT_IRQH_CMD:0x%x \n",__func__,tmp);
		iowrite32be(tmp, bridge->port + INICORE_LCSR_VINT_IRQH_CMD);
		
	}
	
	/*release semapore*/
	/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);


	return;
}

/*
 * Generate a VME bus interrupt at the requested level & vector. Wait for
 * interrupt to be acked.
 */
static int inicore_irq_generate(struct vme_bridge *inicore_bridge, int level,
	int statid)
{
	u32 tmp;
	struct inicore_driver *bridge;

	bridge = inicore_bridge->driver_priv;
	
	mutex_lock(&bridge->vme_int);


	
	/*enable local csr access*/
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);
	
	/* Set VME INT MAP */
	tmp = INICORE_LCSR_VINT_MAP_D32 | INICORE_LCSR_VINT_MAP_SWIRQL[level];
	iowrite32be(tmp, bridge->port + INICORE_LCSR_VINT_MAP);
		
	/* Set Status/ID */
	iowrite32be(statid, bridge->port + INICORE_LCSR_VINT_STAT_SW);
	
	/*check iack*/
	wait_event_interruptible_timeout(bridge->iack_queue,
			inicore_iack_received(bridge),
			msecs_to_jiffies(50));
			
	/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);

	mutex_unlock(&bridge->vme_int);

	return 0;
}

/*
 * Initialize a slave window with the requested attributes.
 */
static int inicore_slave_set(struct vme_slave_resource *image, int enabled,
	unsigned long long vme_base, unsigned long long size,
	dma_addr_t pci_base, u32 aspace, u32 cycle)
{
	unsigned int i, addr = 0, granularity = 0;
	unsigned int temp_ctl = 0;
	struct vme_bridge *inicore_bridge;
	struct inicore_driver *bridge;

	inicore_bridge = image->parent;
	bridge = inicore_bridge->driver_priv;

	i = image->number;
	
	/*TODO: Handle multiple window*/
	switch (aspace) {
	case VME_A24:
		granularity = 0x1000;
		addr |= INICORE_CSRADERn_AM_A24;
		break;
	case VME_A32:
		granularity = 0x10000;
		addr |= INICORE_CSRADERn_AM_A32;
		break;
	case VME_A16:
	case VME_CRCSR:
	case VME_USER1:
	case VME_USER2:
	case VME_USER3:
	case VME_USER4:
	default:
		dev_err(inicore_bridge->parent, "Invalid address space\n");
		return -EINVAL;
		break;
	}
	
	if (cycle & (VME_MBLT| VME_2eVME | VME_2eSST |VME_2eSSTB)){
			dev_err(inicore_bridge->parent, "Invalid VME cycle\n");
			return -EINVAL;
	}
	
	/*enable local csr access*/
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);
		
	/*disable slave window */
	temp_ctl = ioread32be(bridge->port + INICORE_LCSR_SLV_ACC_DEC1);
	temp_ctl &= ~INICORE_LCSR_SLVW_EBL;
	iowrite32be(temp_ctl, bridge->port + INICORE_LCSR_SLV_ACC_DEC1);
	
	/* Setup address space */
	temp_ctl = addr;
	
	/* Setup Cycle and Access */
	if (cycle & VME_SUPER)
		temp_ctl |= INICORE_CSRADERn_AM_SUPR ;
	if (cycle & VME_USER)
		temp_ctl |= INICORE_CSRADERn_AM_NPRIV;
	if (cycle & VME_BLT)
		temp_ctl |= INICORE_CSRADERn_AM_BLT;
	if (cycle & VME_PROG)
		temp_ctl |= INICORE_CSRADERn_AM_PGM;
	if (cycle & VME_DATA)
		temp_ctl |= INICORE_CSRADERn_AM_DAT;
	
	/*setup AM*/
	iowrite32be(temp_ctl, bridge->port + (INICORE_CSRADER0 + INICORE_CSRADERn_C_OFFSET));
	
	/*setup vme base address*/
	temp_ctl = (vme_base & 0xFF000000);
	iowrite32be(temp_ctl, bridge->port + (INICORE_CSRADER0 + INICORE_CSRADERn_0_OFFSET));
	temp_ctl = ((vme_base & 0x00FF0000) << 8);
	iowrite32be(temp_ctl, bridge->port + (INICORE_CSRADER0 + INICORE_CSRADERn_4_OFFSET));
	temp_ctl = ((vme_base & 0x0000FF00) << 16);
	iowrite32be(temp_ctl, bridge->port + (INICORE_CSRADER0 + INICORE_CSRADERn_8_OFFSET));
	
	/*setup vme base decoder mask*/
	temp_ctl = (vme_base & INICORE_LCSR_SLVW_ADEM_M);
	iowrite32be(temp_ctl, bridge->port + INICORE_LCSR_SLV_ACC_MSK1);
	
	if (enabled){
		temp_ctl = ioread32be(bridge->port + INICORE_LCSR_SLV_ACC_DEC1);
		temp_ctl |= INICORE_LCSR_SLVW_EBL;
		iowrite32be(temp_ctl, bridge->port + INICORE_LCSR_SLV_ACC_DEC1);
	}
		
	/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);

	return 0;
}

/*
 * Get slave window configuration.
 */
static int inicore_slave_get(struct vme_slave_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size,
	dma_addr_t *pci_base, u32 *aspace, u32 *cycle)
{
	unsigned int i, granularity = 0, ctl = 0;
	struct inicore_driver *bridge;

	bridge = image->parent->driver_priv;

	i = image->number;

	/*enable local csr access*/
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);	
	
	/* Get VME Address */
	*vme_base = 0;
	/*read csr address decoder register*/
	ctl = ioread32be(bridge->port + (INICORE_CSRADER0 + INICORE_CSRADERn_0_OFFSET));
	*vme_base |= (ctl & INICORE_CSRADERn_ADDRCB_M);
	/*read csr address decoder register*/
	ctl = ioread32be(bridge->port + (INICORE_CSRADER0 + INICORE_CSRADERn_4_OFFSET));
	*vme_base |= ((ctl & INICORE_CSRADERn_ADDRCB_M) >> 8);
	/*read csr address decoder register*/
	ctl = ioread32be(bridge->port + (INICORE_CSRADER0 + INICORE_CSRADERn_8_OFFSET));
	*vme_base |= ((ctl & INICORE_CSRADERn_ADDRCB_M) >> 16);
		
	/*read slave access decoding register*/
	ctl = ioread32be(bridge->port + INICORE_LCSR_SLV_ACC_DEC1);
		
	*enabled = 0;
	*aspace = 0;
	*cycle = 0;
	
	/*check slave window enabled*/
	if (ctl & INICORE_LCSR_SLVW_EBL)
		*enabled = 1;
	
	/*read csr address decoder register*/
	ctl = ioread32be(bridge->port + (INICORE_CSRADER0 + INICORE_CSRADERn_C_OFFSET));
	
	/*get address space*/
	if ((ctl & INICORE_CSRADERn_AM_AS_M) == INICORE_CSRADERn_AM_A24) {
		granularity = 0x10;
		*aspace |= VME_A24;
	}
	if ((ctl & INICORE_CSRADERn_AM_AS_M) == INICORE_CSRADERn_AM_A32) {
		granularity = 0x10000;
		*aspace |= VME_A32;
	}
	
	/*get cycle*/
	if ((ctl & INICORE_CSRADERn_AM_AC_M) == INICORE_CSRADERn_AM_SUPR) {
		*cycle |= VME_SUPER;
	}
	if ((ctl & INICORE_CSRADERn_AM_AC_M) == INICORE_CSRADERn_AM_BLT) {
		*cycle |= VME_BLT;
	}
	if ((ctl & INICORE_CSRADERn_AM_AC_M) == INICORE_CSRADERn_AM_PGM) {
		*cycle |= VME_PROG;
	}
	if ((ctl & INICORE_CSRADERn_AM_AC_M) == INICORE_CSRADERn_AM_DAT) {
		*cycle |= VME_DATA;
	}
	if ((ctl & INICORE_CSRADERn_AM_AC_M) == INICORE_CSRADERn_AM_NPRIV) {
		*cycle |= VME_USER;
	}

	/*read slave address decoder compare bits*/
	ctl = ioread32be(bridge->port + INICORE_LCSR_SLV_ACC_MSK1);
	
	/*get size*/
	*size = 0;
	/*TODO There is no register in VME IP stores the slave size*/
	/*
	 * *size = ((ctl & INICORE_LCSR_SLVW_ADEM_M) - *vme_base);
	 * 
	 */ 
	
	/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);

	return 0;
}

/*
 * Allocate and MAP VME Resource
 */
static int inicore_alloc_resource(struct vme_master_resource *image, unsigned long long vme_base,
	unsigned long long size)
{
	unsigned long long existing_size = 0, existing_start = 0, new_start = 0;
	int retval = 0;
	struct resource *root;
	struct vme_bridge *inicore_bridge;
	struct inicore_driver *bridge;

	inicore_bridge = image->parent;
	bridge = inicore_bridge->driver_priv;
	root = bridge->win_resource;
	
	existing_size = (unsigned long long)(image->bus_resource.end -
		image->bus_resource.start);
	
	new_start = (unsigned long long)(root->start + vme_base);
	existing_start = (unsigned long long)(image->bus_resource.start);
		
	/* check if it's already map */
	if ((size != 0) 
			&& (existing_size == (size - 1))
			&& (new_start == existing_start)){
           return -EEXIST;
	}
	
	if (existing_size != 0) {
		iounmap(image->kern_base);
		image->kern_base = NULL;
		kfree(image->bus_resource.name);
		release_resource(&image->bus_resource);
		memset(&image->bus_resource, 0, sizeof(struct resource));
	}

	if (image->bus_resource.name == NULL) {
		image->bus_resource.name = kmalloc(VMENAMSIZ+3, GFP_ATOMIC);
		if (image->bus_resource.name == NULL) {
			dev_err(inicore_bridge->parent, "Unable to allocate "
				"memory for resource name\n");
			retval = -ENOMEM;
			goto err_name;
		}
	}

	sprintf((char *)image->bus_resource.name, "%s.%d", inicore_bridge->name,
		image->number);

	image->bus_resource.start = (root->start + vme_base);
	image->bus_resource.end = ((root->start + vme_base + size) - 1);
	image->bus_resource.flags = IORESOURCE_MEM;
		
	retval = request_resource(root,&image->bus_resource);
	
	if (retval != 0) {
		dev_err(inicore_bridge->parent, "Failed to allocate "
		"resource for window %d size 0x%lx start 0x%lx end 0x%lx err:%d\n",
			image->number, (unsigned long)size,
			(unsigned long)image->bus_resource.start,
			(unsigned long)image->bus_resource.end,
				retval);
		/*clear image resource*/		
		image->bus_resource.start = 0;
		image->bus_resource.end = 0;
		image->bus_resource.flags = IORESOURCE_MEM;
		goto err_resource;
	}
		
	image->kern_base = ioremap_nocache(
		image->bus_resource.start, size);
		
	if (image->kern_base == NULL) {
		dev_err(inicore_bridge->parent, "Failed to remap resource\n");
		retval = -ENOMEM;
		goto err_remap;
	}
		
	return 0;
	
err_remap:
	release_resource(&image->bus_resource);
err_resource:
	kfree(image->bus_resource.name);
	memset(&image->bus_resource, 0, sizeof(struct resource));
err_name:
	return retval;
}

/*
 * Free and Resource
 */
static void inicore_free_resource(struct vme_master_resource *image)
{
   iounmap(image->kern_base);
   image->kern_base = NULL;
   release_resource(&image->bus_resource);
   kfree(image->bus_resource.name);
   memset(&image->bus_resource, 0, sizeof(struct resource));
}

/*
 * Free Window Resource
 */
static int inicore_master_rst_win(struct vme_master_resource *image)
{
   struct vme_bridge *inicore_bridge;
   struct inicore_driver *bridge;

   if(image->bus_resource.parent==NULL)
   {
#ifdef VME_DEBUG
      pr_info("slot %d was not allocated\n",image->number);
#endif
      return 1;
   }

   inicore_bridge = image->parent;
   bridge = inicore_bridge->driver_priv;
   

   
   inicore_free_resource(image);

   /*Clear master window register*/
   iowrite32(0,bridge->master_win_base + VME_MS_WIN_ST[image->number]);
   iowrite32(0,bridge->master_win_base + VME_MS_WIN_ED[image->number]);

   
   return 0;
}

/*
 * Set the attributes of an outbound window.
 */
static int inicore_master_set(struct vme_master_resource *image, int enabled,
	unsigned long long vme_base, unsigned long long size, u32 aspace,
	u32 cycle, u32 dwidth)
{
  int retval = 0;
  unsigned int i = 0, temp_ctl = 0;
  unsigned int am = 0, dw = 0;
  struct vme_bridge *inicore_bridge;
  struct inicore_driver *bridge;

  inicore_bridge = image->parent;
  bridge = inicore_bridge->driver_priv;
	
  /* Verify vme base addr */
  if (vme_base & 0xFF || vme_base & 0xFFFFFFFFE0000000ULL ) 
  {
    dev_err(inicore_bridge->parent, "Invalid VME Window alignment\n");
    retval = -EINVAL;
    goto err_window;
  }

  /* Verify size is not less than 256B*/
  if((size < 0x100) && (enabled != 0)) 
  {
    dev_err(inicore_bridge->parent, "Size must be non-zero for enabled windows\n");
    retval = -EINVAL;
    goto err_window;
  }

  i = image->number;

  
  retval = inicore_alloc_resource(image, vme_base, size);

  if(retval) 
  {
    dev_err(inicore_bridge->parent, "Unable to allocate memory for resource\n");
    goto err_res;
  }

  /*Clear master window register*/
  iowrite32(0,bridge->master_win_base + VME_MS_WIN_ST[i]);
  iowrite32(0,bridge->master_win_base + VME_MS_WIN_ED[i]);

  /* Setup data width */
  switch (dwidth) 
  {
    case VME_D8:
     image->width_sel = VME_D8;
     dw = INICORE_DW08;
     break;
    case VME_D16:
     image->width_sel = VME_D16;
     dw = INICORE_DW16;
     break;
    case VME_D32:
     image->width_sel = VME_D32;
     dw = INICORE_DW32;
     break;
    default:
     dev_err(inicore_bridge->parent, "Invalid data width\n");
     retval = -EINVAL;
     goto err_dwidth;
  }

  /* Setup address space */
  switch (aspace) 
  {
    case VME_A16:
     image->address_sel = VME_A16;
     am = INICORE_AM_A16;
     break;
    case VME_A24:
     image->address_sel = VME_A24;
     am = INICORE_AM_A24;
     break;
    case VME_A32:
     image->address_sel = VME_A32;
     am = INICORE_AM_A32;
     break;
    case VME_CRCSR:
     image->address_sel = VME_CRCSR;
     am = INICORE_AM_CRCSR;
     break;
    case VME_A64:
    case VME_USER1:
    case VME_USER2:
    case VME_USER3:
    case VME_USER4:
    default:
     dev_err(inicore_bridge->parent, "Invalid address space\n");
     retval = -EINVAL;
     goto err_aspace;
     break;
  }
  
  DEBUGF("%s:am:%x\n",__func__,am);
  
  /* Setup Cycle  */
  if(aspace != VME_CRCSR)
  {
    if(cycle & VME_SUPER)
    {
      image->cycle_sel = VME_SUPER;
      am |= INICORE_AM_SURP;
    }
    if(cycle & VME_USER)
    {
      image->cycle_sel = VME_USER;
      am |= INICORE_AM_NPRIV;
    }
    if(cycle & VME_PROG)
    {
      image->cycle_sel = VME_PROG;
      am |= INICORE_AM_PGM;
    }
    if(cycle & VME_DATA)
    {
      image->cycle_sel = VME_DATA;
      am |= INICORE_AM_DAT;
    }
  }

  DEBUGF("%s:am:%x\n",__func__,am);
  temp_ctl =  VME_MS_WINn_ST_DW_S(dw) | VME_MS_WINn_ST_AM_S(am);
  temp_ctl |= (vme_base & VME_MS_WINn_XX_ADDR_M);
  /* Setup Master Window start address, AM, and data width */
  iowrite32(temp_ctl,bridge->master_win_base + VME_MS_WIN_ST[i]);
  /* Setup Master Window end address*/
  temp_ctl = (vme_base + size) - 1;
  iowrite32(temp_ctl,bridge->master_win_base + VME_MS_WIN_ED[i]);
  /*TODO: There is no MST Win enable/disable in VME Controller
  * should we track enabled state in SW?
  */
  

  return 0;

err_res:
err_aspace:
err_dwidth:
err_window:

  return retval;
}

/*
 * Set Bus Error Timeout
 *
 * XXX Not parsing prefetch information.
 */
static void __set_vme_bus_err_timeout(struct vme_bridge *inicore_bridge){

	struct inicore_driver *bridge;
	u32 tmp;
	u8 timeout;
	
	bridge = inicore_bridge->driver_priv;
	timeout = bridge->berr_time;
	
	/*enable local csr access*/
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);
	
	/*setup timeout BERR Timer*/
	DEBUGF("%s:timeout:%d us\n",__func__,timeout);	
	tmp = ioread32be(bridge->port + INICORE_LCSR_SYS_CTRL);
	tmp |= INICORE_LCSR_SYSC_BERRTIMER(timeout);
	
	DEBUGF("%s:tmp:0x%08x \n",__func__,tmp);
	iowrite32be(tmp, bridge->port + INICORE_LCSR_SYS_CTRL);
	
	/*enable local csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);
}


/*set user side bus to little endian*/
static void __set_vme_local_bus_lendian(struct vme_bridge *inicore_bridge){

	struct inicore_driver *bridge;
	u32 tmp;
	
	bridge = inicore_bridge->driver_priv;
	
	/*enable local csr access*/
	iowrite32(CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT, bridge->core_cfg_base);
	
	/*enable little endian*/
	/*
	 * NOTE: After enabel little endian mode then 
	 * all local IP CR/CSR configuration read/write required 
	 * BE swap
	 */
	tmp = ioread32(bridge->port + INICORE_LCSR_DEV_CTRL);
	/*If LENDIAN mode is not enable then enable little endian mode*/
	if(tmp != 0x1000000){
		tmp = INICORE_LCSR_DEV_CTRL_LENDIAN;
		iowrite32(tmp, bridge->port + INICORE_LCSR_DEV_CTRL);
	}

	/*enable local csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);
}

/*
 * Set the attributes of an outbound window.
 *
 * XXX Not parsing prefetch information.
 */
static int __inicore_master_get(struct vme_master_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size, u32 *aspace,
	u32 *cycle, u32 *dwidth)
{
	unsigned int i = 0, ctlSt = 0, ctlEd = 0, tmp = 0;
	struct inicore_driver *bridge;

	bridge = image->parent->driver_priv;

	i = image->number;
	
	ctlSt = ioread32(bridge->master_win_base + VME_MS_WIN_ST[i]);
	*vme_base = (ctlSt & VME_MS_WINn_XX_ADDR_M);
	ctlEd = ioread32(bridge->master_win_base + VME_MS_WIN_ED[i]);
	
	if(ctlEd != 0){
		/*padded size with 256Byte*/
		*size = (unsigned long long)(((ctlEd  | 0xFF) - *vme_base)  + 1); 
	}else{
		*size = 0;
	}
	
	*enabled = 0;
	*aspace = 0;
	*cycle = 0;
	*dwidth = 0;

	/*TODO: how should we track master win enable in SW*/
	//if (ctl & inicore_LCSR_OTAT_EN)
	*enabled = 1;

	/* Setup address space */
	tmp = (ctlSt & VME_MS_WINn_ST_AM_M);
	
	if ((tmp & INICORE_AM_A16) == INICORE_AM_A16){
		if(tmp == INICORE_AM_CRCSR)
			*aspace = VME_CRCSR;
		else
			*aspace = VME_A16;
	} else if ((tmp & INICORE_AM_A24) == INICORE_AM_A24){
		*aspace = VME_A24;
	} else if ((tmp & INICORE_AM_A32) == INICORE_AM_A32){
		*aspace = VME_A32;
	}
	
	
	if(*aspace != VME_CRCSR){
		if ((tmp & INICORE_AM_SURP) == INICORE_AM_SURP)
			*cycle |= VME_SUPER;
		if ((tmp & INICORE_AM_NPRIV) == INICORE_AM_NPRIV)
			*cycle |= VME_USER;
		if ((tmp & INICORE_AM_PGM) == INICORE_AM_PGM)
			*cycle |= VME_PROG;
		if ((tmp & INICORE_AM_DAT) == INICORE_AM_DAT)
			*cycle |= VME_DATA;
	}
	
	/* Setup data width */
	tmp = ctlSt & VME_MS_WINn_ST_DW_M;
	
	if (tmp == VME_MS_WINn_ST_DW_S(INICORE_DW08))
		*dwidth = VME_D8;
	if (tmp == VME_MS_WINn_ST_DW_S(INICORE_DW16))
		*dwidth = VME_D16;
	if (tmp == VME_MS_WINn_ST_DW_S(INICORE_DW32))
		*dwidth = VME_D32;
	
	return 0;
}


static int inicore_master_get(struct vme_master_resource *image, int *enabled,
	unsigned long long *vme_base, unsigned long long *size, u32 *aspace,
	u32 *cycle, u32 *dwidth)
{
	int retval;


	retval = __inicore_master_get(image, enabled, vme_base, size, aspace,
		cycle, dwidth);
	

	return retval;
}

static ssize_t inicore_master_read(struct vme_master_resource *image, void *buf,
	size_t count, loff_t offset)
{
	int retval, enabled;
	unsigned long long vme_base, size;
	u32 aspace, cycle, dwidth, tmp;
	struct vme_error_handler *handler = NULL;
	struct vme_bridge *inicore_bridge;
	struct inicore_driver *bridge;
	void __iomem *addr = image->kern_base + offset;
	unsigned int done = 0;
	unsigned long flags;
	
	dwidth = image->width_sel;
	aspace = image->address_sel;
	
	inicore_bridge = image->parent;
	bridge = inicore_bridge->driver_priv;
	
	if(count == 0){
		return -EINVAL;
	}
	
	spin_lock_irqsave(&image->lock, flags);

	if (err_chk) {
		inicore_master_get(image, &enabled, &vme_base, &size, &aspace,
				   &cycle, &dwidth);
		handler = vme_register_error_handler(inicore_bridge, aspace,
						     vme_base + offset, count);
		if (!handler) {
			spin_unlock_irqrestore(&image->lock, flags);
			return -ENOMEM;
		}
	}
	
	if(aspace == VME_CRCSR){	
		switch (dwidth) {
			case VME_D8:
				tmp = CORE_CFG_SET_DSIZE_8BIT;
				break;
			case VME_D16:
				tmp = CORE_CFG_SET_DSIZE_16BIT;
				break;
			case VME_D32:
				tmp = CORE_CFG_SET_DSIZE_32BIT;
				break;
			default:
				tmp = CORE_CFG_SET_DSIZE_8BIT;
		}
		/*set fpga to ext csr access*/
		iowrite32(tmp|CORE_CFG_EN_EXT_CSR, bridge->core_cfg_base);
		
	}else{	
		/*set fpga to vme bus access*/
		iowrite32(CORE_CFG_EN_VME_BUS, bridge->core_cfg_base);
	}
		
	
	if ((uintptr_t)addr & 0x1) {
		if(dwidth == VME_D8){
			DEBUGF("%s:addr:0x1:D8\n",__func__);
			do{
				*(u8 *)(buf + done) = ioread8(addr+done);
				done++;
			}while(--count);
			count = done;
		}
		
		if(count != done)
			count = 0;
			
		goto out;
	}
	
	if ((uintptr_t)addr & 0x2) {
	
		if(dwidth == VME_D8){
			DEBUGF("%s:addr:0x2:D8\n",__func__);
			do{
				*(u8 *)(buf + done) = ioread8(addr+done);
				done++;
			}while(--count);
			count = done;
		}else if(dwidth == VME_D16){
			DEBUGF("%s:addr:0x2:D16\n",__func__);
			if(count % 2 == 0) {
				count = count/2;
				do{
					*(u16 *)(buf + done) = ioread16(addr + done);
					done += 2;
				}while(--count);		
				count = done;
			}
		}
		
		if(count != done)
			count = 0;
		
		goto out;
	}

	if(dwidth == VME_D32){				
		DEBUGF("%s:addr:0x0:D32\n",__func__);
		if(count % 4 == 0) {
			count = count/4;
			do{
				*(u32 *)(buf + done) = ioread32(addr + done);
				done += 4;
			}while(--count);
			count = done;
		}
	}else if(dwidth == VME_D16){
		DEBUGF("%s:addr:0x0:D16\n",__func__);
		if(count % 2 == 0) {
			count = count/2;
			do{
				*(u16 *)(buf + done) = ioread16(addr + done);
				done += 2;
			}while(--count);
			count = done;
		}
	}else if(dwidth == VME_D8){
		DEBUGF("%s:addr:0x0:D8\n",__func__);
		do{
			*(u8 *)(buf + done) = ioread8(addr+done);
			done++;
		}while(--count);
		count = done;
	}
	
	if(count != done)
		count = 0;
out:		
	/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);
	
	retval = count;
	
	if (err_chk) {
		if (handler->num_errors) {
			dev_err(image->parent->parent,
				"First VME read error detected an at address 0x%llx\n",
				handler->first_error);
			retval = handler->first_error - (vme_base + offset);
		}
		vme_unregister_error_handler(handler);
	}

	spin_unlock_irqrestore(&image->lock, flags);
	return retval;
}


static ssize_t inicore_master_write(struct vme_master_resource *image, void *buf,
	size_t count, loff_t offset)
{
	int retval = 0, enabled;
	unsigned long long vme_base, size;
	u32 aspace, cycle, dwidth, tmp;
	void __iomem *addr = image->kern_base + offset;
	unsigned int done = 0;
	struct vme_error_handler *handler = NULL;
	struct vme_bridge *inicore_bridge;
	struct inicore_driver *bridge;
	unsigned long flags;

	dwidth = image->width_sel;
	aspace = image->address_sel;
	
	inicore_bridge = image->parent;
	bridge = inicore_bridge->driver_priv;
	
	if(count == 0){
		return -EINVAL;
	}


	spin_lock_irqsave(&image->lock, flags);

	if (err_chk) {
		inicore_master_get(image, &enabled, &vme_base, &size, &aspace,
				   &cycle, &dwidth);
		handler = vme_register_error_handler(inicore_bridge, aspace,
						     vme_base + offset, count);
		if (!handler) {
			spin_unlock_irqrestore(&image->lock, flags);
			return -ENOMEM;
		}
	}

	if(aspace == VME_CRCSR){
		
		switch (dwidth) {
			case VME_D8:
				tmp = CORE_CFG_SET_DSIZE_8BIT;
				break;
			case VME_D16:
				tmp = CORE_CFG_SET_DSIZE_16BIT;
				break;
			case VME_D32:
				tmp = CORE_CFG_SET_DSIZE_32BIT;
				break;
			default:
				tmp = CORE_CFG_SET_DSIZE_8BIT;
		}
		/*set fpga to ext csr access*/
		iowrite32(tmp|CORE_CFG_EN_EXT_CSR, bridge->core_cfg_base);
		
	}else{
		/*set fpga to vme bus access*/
		iowrite32(CORE_CFG_EN_VME_BUS, bridge->core_cfg_base);
	}

	if ((uintptr_t)addr & 0x1) {
		if(dwidth == VME_D8){
			DEBUGF("%s:addr:0x1:D8\n",__func__);
			do{
				iowrite8(*(u8 *)(buf + done),addr+done);
				done++;
			}while(--count);
			count = done;
		}
		
		if(count != done)
			count = 0;
			
		goto out;
	}
	
	if ((uintptr_t)addr & 0x2) {
	
		if(dwidth == VME_D8){
			DEBUGF("%s:addr:0x2:D8\n",__func__);
			do{
				iowrite8(*(u8 *)(buf + done),addr+done);
				done++;
			}while(--count);
			count = done;
		}else if(dwidth == VME_D16){
			DEBUGF("%s:addr:0x2:D16\n",__func__);
			if(count % 2 == 0) {
				count = count/2;
				do{
					iowrite16(*(u16 *)(buf + done),addr+done);
					done += 2;
				}while(--count);		
				count = done;
			}
		}
		
		if(count != done)
			count = 0;
		
		goto out;
	}

	if(dwidth == VME_D32){				
		DEBUGF("%s:addr:0x0:D32\n",__func__);
		if(count % 4 == 0) {
			count = count/4;
			do{
				iowrite32(*(u32 *)(buf + done),addr+done);
				done += 4;
			}while(--count);
			count = done;
		}
	}else if(dwidth == VME_D16){
		DEBUGF("%s:addr:0x0:D16\n",__func__);
		if(count % 2 == 0) {
			count = count/2;
			do{
				iowrite16(*(u16 *)(buf + done),addr+done);
				done += 2;
			}while(--count);
			count = done;
		}
	}else if(dwidth == VME_D8){
		DEBUGF("%s:addr:0x0:D8\n",__func__);
		do{
			iowrite8(*(u8 *)(buf + done),addr+done);
			done++;
		}while(--count);
		count = done;
	}
	
	if(count != done)
		count = 0;

out:
			
	/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);	
	
	retval = done;
	
	/*
	 * Writes are posted. We need to do a read on the VME bus to flush out
	 * all of the writes before we check for errors. We can't guarantee
	 * that reading the data we have just written is safe. It is believed
	 * that there isn't any read, write re-ordering, so we can read any
	 * location in VME space, so lets read the Device ID from the inicore's
	 * own registers as mapped into CR/CSR space.
	 *
	 * We check for saved errors in the written address range/space.
	 */

	if (err_chk) {
		/*TODO: Not sure why we need to read from 7F00*/
		ioread16(bridge->flush_image->kern_base + 0x7F000);

		if (handler->num_errors) {
			dev_warn(inicore_bridge->parent,
				 "First VME write error detected an at address 0x%llx\n",
				 handler->first_error);
			retval = handler->first_error - (vme_base + offset);
		}
		vme_unregister_error_handler(handler);
	}

	spin_unlock_irqrestore(&image->lock, flags);
	return retval;
}

/*
 * Determine Geographical Addressing
 */
static int inicore_slot_get(struct vme_bridge *inicore_bridge)
{
	u32 slot = 0;
	u32 tmp = 0;
	struct inicore_driver *bridge;

	bridge = inicore_bridge->driver_priv;

	tmp = CORE_CFG_EN_LOCAL_CSR|CORE_CFG_SET_DSIZE_32BIT;
	
	/*enable local csr access*/
	iowrite32(tmp, bridge->core_cfg_base);
	
	if (!geoid) {
		slot = ioread32be(bridge->port + INICORE_LCSR_SYS_CTRL);
		slot = slot & INICORE_LCSR_SYSC_GA_M;
	} else {
		slot = geoid;
	}
	
	/*clear fpga to csr access*/
	iowrite32(CORE_CFG_CLEAR, bridge->core_cfg_base);


	return (int)slot;
}

static int inicore_dtb(struct vme_bridge *bridge, struct device *dev){
	
	int retval = 0;
	int irq = 0;
	u8 time;
	struct device_node *parentnode, *childnode;
	int childnodecount = 0;
	struct inicore_driver *inicore_device = bridge->driver_priv; 
	void __iomem *tmp;
	const char *desc;
	
	parentnode = of_find_compatible_node(NULL, NULL, "nai,inicore");
	if (!parentnode) {
		pr_err("%s:Unable to find nai,inicore in dtb\n",__func__);
		retval = -EAGAIN;
		goto err;
	}
		
	//read master slot from dtb property
	retval = of_property_read_u8(parentnode, "berr_time", &time);
	if ( retval < 0 ){
		pr_err("%s: unable to find berr_time in dtb\n",__func__);
		/*default to 0*/
		time = 0;
	}
	
	inicore_device->berr_time = time;
	DEBUGF("%s:timeout:%d us\n",__func__,inicore_device->berr_time);
	
	/*get IRQ*/
	irq = irq_of_parse_and_map(parentnode,0);
	
	if (irq == 0){
		pr_err("%s: Unable to find IRQ in dtb\n",__func__);
		retval = -EAGAIN;
		goto err;
	}
	inicore_device->irq = irq;
	
	childnodecount = of_get_child_count(parentnode);
	
	/* map fpga vme core register */
	childnode = of_get_child_by_name(parentnode,"port");
	desc = of_get_property(childnode, "label", NULL);
	
	tmp = of_iomap(childnode,0);
	if (!tmp) {
		pr_err("%s port iomap failed\n",__func__);
		retval = -ENOMEM;
		goto err_port;
	}
	
	inicore_device->port = tmp;
	
	/* map fpga core config register */
	childnode = of_get_child_by_name(parentnode,"core_cfg_base");
	desc = of_get_property(childnode, "label", NULL);
	
	tmp = of_iomap(childnode,0);
	if (!tmp){
		pr_err("%s: core_cfg_base ioremap failed\n",__func__);
		retval = -ENOMEM;
		goto err_core_cfg;
	}
	
	inicore_device->core_cfg_base = tmp;
	
	/* map fpga master window register */			 
	childnode = of_get_child_by_name(parentnode,"master_win_base");
	desc = of_get_property(childnode, "label", NULL);
	
	tmp = of_iomap(childnode,0);
	if (!tmp) {
		pr_err("%s: master_win_base ioremap failed\n",__func__);
		retval = -ENOMEM;
		goto err_master_win;
	}
	
	inicore_device->master_win_base = tmp;
		
	return 0;

err_master_win:
	iounmap(inicore_device->core_cfg_base);	
err_core_cfg:
	iounmap(inicore_device->port);
err_port:	
err:	
	return retval;
}

static int inicore_probe(struct platform_device *pdev)
{
	int retval, i, master_num;
	struct list_head *pos = NULL, *n;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct vme_bridge *inicore_bridge = NULL;
	struct inicore_driver *inicore_device = NULL;
	struct vme_master_resource *master_image = NULL;
	struct vme_slave_resource *slave_image = NULL;

	inicore_bridge = kzalloc(sizeof(struct vme_bridge), GFP_KERNEL);
	if (inicore_bridge == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for device "
			"structure\n");
		retval = -ENOMEM;
		goto err_struct;
	}
	vme_init_bridge(inicore_bridge);

	inicore_device = kzalloc(sizeof(struct inicore_driver), GFP_KERNEL);
	if (inicore_device == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for device "
			"structure\n");
		retval = -ENOMEM;
		goto err_driver;
	}

	inicore_bridge->parent = dev;
	inicore_bridge->driver_priv = inicore_device;

	
	/*get DTB*/
	retval = inicore_dtb(inicore_bridge, dev);
	
	if(retval < 0){
		retval = -ENOMEM;
		goto  err_dtb;
	}
	
		
	/*TODO Check to see if the mapping worked out */
	/* Initialize wait queues & mutual exclusion flags */
	mutex_init(&inicore_device->vme_int);
	mutex_init(&inicore_device->vme_rmw);

	strcpy(inicore_bridge->name, driver_name);
	
	__set_vme_local_bus_lendian(inicore_bridge);
	
	__set_vme_bus_err_timeout(inicore_bridge);
	
	/* Setup IRQ */
	retval = inicore_irq_init(inicore_bridge);
	if (retval != 0) {
		dev_err(&pdev->dev, "Chip Initialization failed.\n");
		goto err_irq;
	}

	/* If we are going to flush writes, we need to read from the VME bus.
	 * We need to do this safely, thus we read the devices own CR/CSR
	 * register. To do this we must set up a window in CR/CSR space and
	 * hence have one less master window resource available.
	 */
	master_num = INICORE_MAX_MASTER;
	if (err_chk) {
		master_num--;

		inicore_device->flush_image =
			kmalloc(sizeof(struct vme_master_resource), GFP_KERNEL);
		if (inicore_device->flush_image == NULL) {
			dev_err(&pdev->dev, "Failed to allocate memory for "
			"flush resource structure\n");
			retval = -ENOMEM;
			goto err_master;
		}
		inicore_device->flush_image->parent = inicore_bridge;
		spin_lock_init(&inicore_device->flush_image->lock);
		inicore_device->flush_image->locked = 1;
		inicore_device->flush_image->number = master_num;
		memset(&inicore_device->flush_image->bus_resource, 0,
			sizeof(struct resource));
		inicore_device->flush_image->kern_base  = NULL;
	}

	/* Add master windows to list */
	for (i = 0; i < master_num; i++) {
		master_image = kmalloc(sizeof(struct vme_master_resource),
			GFP_KERNEL);
		if (master_image == NULL) {
			dev_err(&pdev->dev, "Failed to allocate memory for "
			"master resource structure\n");
			retval = -ENOMEM;
			goto err_master;
		}
		master_image->parent = inicore_bridge;
		spin_lock_init(&master_image->lock);
		master_image->locked = 0;
		master_image->number = i;
		master_image->address_attr = VME_A16 | VME_A24 | VME_A32 | VME_CRCSR;
		master_image->cycle_attr = VME_SUPER | VME_USER |
			VME_PROG | VME_DATA | VME_CRCSR;
		master_image->width_attr = VME_D8 | VME_D16 | VME_D32;
		memset(&master_image->bus_resource, 0,
			sizeof(struct resource));
		master_image->kern_base  = NULL;
		list_add_tail(&master_image->list,
			&inicore_bridge->master_resources);
	}

	/* Add slave windows to list */
	for (i = 0; i < INICORE_MAX_SLAVE; i++) {
		slave_image = kmalloc(sizeof(struct vme_slave_resource),
			GFP_KERNEL);
		if (slave_image == NULL) {
			dev_err(&pdev->dev, "Failed to allocate memory for "
			"slave resource structure\n");
			retval = -ENOMEM;
			goto err_slave;
		}
		slave_image->parent = inicore_bridge;
		mutex_init(&slave_image->mtx);
		slave_image->locked = 0;
		slave_image->number = i;
		slave_image->address_attr = VME_A16 | VME_A24 | VME_A32;
		slave_image->cycle_attr = VME_BLT | VME_SUPER | VME_USER |
			VME_PROG | VME_DATA;
		list_add_tail(&slave_image->list,
			&inicore_bridge->slave_resources);
	}

	inicore_bridge->slave_get = inicore_slave_get;
	inicore_bridge->slave_set = inicore_slave_set;
	inicore_bridge->master_get = inicore_master_get;
	inicore_bridge->master_set = inicore_master_set;
	inicore_bridge->master_read = inicore_master_read;
	inicore_bridge->master_write = inicore_master_write;
	inicore_bridge->master_reset_window = inicore_master_rst_win;

	inicore_bridge->master_rmw = NULL;
	inicore_bridge->dma_list_add = NULL;
	inicore_bridge->dma_list_exec = NULL;
	inicore_bridge->dma_list_empty = NULL;
	inicore_bridge->irq_set = inicore_irq_set;
	inicore_bridge->irq_generate = inicore_irq_generate;
	inicore_bridge->lm_set = NULL;
	inicore_bridge->lm_get = NULL;
	inicore_bridge->lm_attach = NULL;
	inicore_bridge->lm_detach = NULL;
	inicore_bridge->slot_get = inicore_slot_get;
	inicore_bridge->alloc_consistent = NULL;
	inicore_bridge->free_consistent = NULL;

	/*allocated device resource*/
	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res) {
		dev_err(&pdev->dev, "failed to allocated memory for device\n");
		retval = -ENOMEM;
		goto err_res_alloc;
	}
	
	res->name  = inicore_bridge->name;
	res->start = (resource_size_t)VME_BUS_BASE_ADDR;
	res->end   = (resource_size_t)(VME_BUS_BASE_ADDR +
				       VNME_BUS_SIZE - 1);
	res->flags = IORESOURCE_MEM;
	
	retval = request_resource(&iomem_resource, res);
	
	if (retval) {
		dev_err(&pdev->dev, "failed to allocated resource\n");
		goto err_req_res;
	}
	
	inicore_device->win_resource = res;
	
	retval = vme_register_bridge(inicore_bridge);
	if (retval != 0) {
		dev_err(&pdev->dev, "Chip Registration failed.\n");
		goto err_reg;
	}
	
	platform_set_drvdata(pdev, inicore_bridge);
	
	return 0;

err_reg:
	release_resource(res);
err_req_res:
	kfree(res);
err_res_alloc:
err_slave:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &inicore_bridge->slave_resources) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}
err_master:
	/* resources are stored in link list */
	list_for_each_safe(pos, n, &inicore_bridge->master_resources) {
		master_image = list_entry(pos, struct vme_master_resource,
			list);
		list_del(pos);
		kfree(master_image);
	}
	inicore_irq_exit(inicore_bridge);
err_irq:
err_dtb:
	kfree(inicore_device);
err_driver:
	kfree(inicore_bridge);
err_struct:
	return retval;

}

static int inicore_remove(struct platform_device *pdev)
{
	struct list_head *pos = NULL;
	struct list_head *tmplist;
	struct vme_master_resource *master_image;
	struct vme_slave_resource *slave_image;
	struct inicore_driver *bridge;
	struct vme_bridge *inicore_bridge = platform_get_drvdata(pdev);

	bridge = inicore_bridge->driver_priv;

	dev_dbg(&pdev->dev, "Driver is being unloaded.\n");

	/*
	 *  TODO: clear slave and master windows
	 */

	inicore_irq_exit(inicore_bridge);

	release_resource(bridge->win_resource);
	
	kfree(bridge->win_resource);
	
	/* resources are stored in link list */
	list_for_each_safe(pos, tmplist, &inicore_bridge->slave_resources) {
		slave_image = list_entry(pos, struct vme_slave_resource, list);
		list_del(pos);
		kfree(slave_image);
	}

	/* resources are stored in link list */
	list_for_each_safe(pos, tmplist, &inicore_bridge->master_resources) {
		master_image = list_entry(pos, struct vme_master_resource,
			list);
		list_del(pos);
		kfree(master_image);
	}
	
	
	iounmap(bridge->master_win_base);
	iounmap(bridge->core_cfg_base);	
	iounmap(bridge->port);
	
	kfree(bridge);

	vme_unregister_bridge(inicore_bridge);
	
	kfree(inicore_bridge);
	
	return 0;
	
}

static struct of_device_id inicore_of_match[] = {
	{ .compatible = "nai,inicore", },
	{ },
};
MODULE_DEVICE_TABLE(of, inicore_of_match);

static struct platform_driver inicore_drv = {
	.probe    = inicore_probe,
	.remove   = inicore_remove,
	.driver   = {
		.name  = "inicore",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(inicore_of_match),
	},
};

static int __init inicore_init(void) {
	return platform_driver_register(&inicore_drv);
}

static void __exit inicore_exit(void) {
	platform_driver_unregister(&inicore_drv);
}

subsys_initcall(inicore_init);
module_exit(inicore_exit);

MODULE_PARM_DESC(err_chk, "Check for VME errors on reads and writes");
module_param(err_chk, bool, 0);

MODULE_PARM_DESC(geoid, "Override geographical addressing");
module_param(geoid, int, 1);

MODULE_DESCRIPTION("Inicore VME system control driver");
MODULE_LICENSE("GPL");

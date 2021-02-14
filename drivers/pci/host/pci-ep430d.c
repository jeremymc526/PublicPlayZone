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
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>


#define EP430_CFG_SLAVE_REG_OFFSET              0x00000004
#define EP430_CFG_MASTER_REG_OFFSET             0x00000008

#define EP430_CFG_SLAVE_UW_MASK                 0xFF000000
#define EP430_CFG_SLAVE_UW_SHIFT                24
#define EP430_CFG_SLAVE_READ_BYTE_EN_MASK       0x00F00000
#define EP430_CFG_SLAVE_READ_BYTE_EN_SHIFT      20
#define EP430_CFG_SLAVE_FAST                    0x00000040
#define EP430_CFG_SLAVE_CS2                     0x00000020
#define EP430_CFG_SLAVE_CS1                     0x00000010
#define EP430_CFG_SLAVE_CS0                     0x00000008
#define EP430_CFG_SLAVE_CMD1                    0x00000004
#define EP430_CFG_SLAVE_CMD0                    0x00000002
#define EP430_CFG_SLAVE_MIO                     0x00000001

/* TODO what are ready and read_err? */
#define EP430_CFG_STATUS_DETECTED_PARITY_ERR    0x00000080
#define EP430_CFG_STATUS_SIGNALED_SYSTEM_ERR    0x00000040
#define EP430_CFG_STATUS_RECEIVED_MASTER_ABORT  0x00000020
#define EP430_CFG_STATUS_RECEIVED_TARGET_ABORT  0x00000010
#define EP430_CFG_STATUS_SIGNALED_TARGET_ABORT  0x00000008
#define EP430_CFG_STATUS_MASTER_DATA_PARITY_ERR 0x00000004
#define EP430_CFG_STATUS_READY                  0x00000002
#define EP430_CFG_STATUS_READ_ERR               0x00000001


#define EP430_CORE_BASE_SIZE                    0x04
#define EP430_CORE_DATA_RW_EN                   0x80000000

#define EP430_MEM_BASE_ADDR                     0x60000000
#define EP430_MEM_BASE_SIZE                     0x04000000

#define NAI_75G5_PCI_DEVICE_ID					0x758115AC

#define NAI_IS_PCI_MASTER_COMMON_AREA_OFFSET	0x160	

/* TODO do we have a separate IO space?
 *      toggling a chip select may not work, investigate!
 */
#define PCI_IO_BUS              0x00000000
#define PCI_IO_PHY              0x00000000
#define PCI_IO_SIZE             0x00010000

static void __iomem *mb_common_addr = NULL;
static void __iomem *cfg_base_addr = NULL;
static void __iomem *core_base_addr = NULL;
static int pci_irq = 0;
static u8 ignoreSlot = 0;

static u32 ep430_config_addr(u8 busnum, int devfn, int where) {
	u32 addr;

	addr = busnum << 16;
	addr |= PCI_SLOT(devfn) << 11;
	addr |= PCI_FUNC(devfn) << 8;
	addr |= (where & ~0x03);

        return addr;
}

static int ep430_get_byte_en(int size) {
	int byte_en;

	/* TODO Verify the rationale (1/2/4 byte access) and
	 *      endianness with Frank.
	 */
	switch (size) {
	case 1:
		byte_en = 0x08;
		break;

	case 2:
		byte_en = 0x0C;
		break;

	default: /* 4 */
		byte_en = 0x0F;
		break;
	}

	return byte_en;
}

/*
 * EP430 Core Register Configure
 * 
 * */
static void ep430_csr_config(u32 config_addr, u32 size) {
	
	u8 byte_en;
	
	config_addr |= EP430_CORE_DATA_RW_EN;

	byte_en = ep430_get_byte_en(size) <<
		EP430_CFG_SLAVE_READ_BYTE_EN_SHIFT;

	/* Enable slave byte read and CS1 */
	/* 0x00f00010 */
	iowrite32(EP430_CFG_SLAVE_READ_BYTE_EN_MASK |
		  EP430_CFG_SLAVE_CS1,
		  cfg_base_addr + EP430_CFG_SLAVE_REG_OFFSET);

	/* Set device addr to config_addr */
	iowrite32(config_addr, core_base_addr);

	/* Change to config_data */
	/* 0x00f00020 */
	iowrite32(byte_en | EP430_CFG_SLAVE_CS2,
		  cfg_base_addr + EP430_CFG_SLAVE_REG_OFFSET);
		  
}

/*
 * EP430 enable 
 * 
 * */
static void ep430_en_memory_mode(void) {
	
	/* Change chip select to cs0 and set mio to 1 for memory mode */
	/* bit 3 ~ 0
	 * 3 = cs0
	 * 2 = cmd1
	 * 1 = cmd0
		 * 0 = mio
	 */
	iowrite32((EP430_MEM_BASE_ADDR & EP430_CFG_SLAVE_UW_MASK) |
		  EP430_CFG_SLAVE_READ_BYTE_EN_MASK |
		  EP430_CFG_SLAVE_CS0 |
		  EP430_CFG_SLAVE_MIO,
		  cfg_base_addr + EP430_CFG_SLAVE_REG_OFFSET);
		  
}

static int ep430_read_config(struct pci_bus *bus, unsigned int devfn,
			     int where,	int size, u32 *value) {
	u32 config_addr;
	//u8 byte_en;
	u32 retval;
	u8 slot = PCI_SLOT(devfn);
	
	if ( slot == ignoreSlot) {
		//printk("NAI SLOT_ID %x \n", slot);
		switch (size) {
		case 1:
			retval = 0xff;
			break;

		case 2:
			retval = 0xffff;
			break;

		default: /* 4*/
			retval = 0xffffffff;
			break;
		}
		goto ignore_slot;
	} 

	config_addr = ep430_config_addr(bus->number, devfn, where);
	ep430_csr_config(config_addr,size);
	/* Read from config_data */
	switch (size) {
	case 1:
		//*value = ioread8(core_base_addr);
		retval = ioread32(core_base_addr);
		if (where & 2) retval >>= 16;
		if (where & 1) retval >>= 8;
		retval &= 0xff;
		break;

	case 2:
		//*value = ioread16(core_base_addr);
		retval = ioread32(core_base_addr);
		if (where & 2) retval >>= 16;
		retval &= 0xffff;
		break;

	default: /* 4*/
		retval = ioread32(core_base_addr);
		break;
	}

	//enable memory mode
	ep430_en_memory_mode();
	
ignore_slot:	
	*value = retval;	
	return PCIBIOS_SUCCESSFUL;
}

static int ep430_write_config(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 value) {
	u32 config_addr;
	//u8 byte_en;
	u8 slot = PCI_SLOT(devfn);
	
	if ( slot == ignoreSlot ) {	
		goto ignore_slot;
	}

	
	config_addr = ep430_config_addr(bus->number, devfn, where);
	ep430_csr_config(config_addr,size);
	
	/* Write to config_data register */
	switch (size) {
	case 1:
		iowrite8((u8)value, core_base_addr);
		break;

	case 2:
		iowrite16((u16)value, core_base_addr);
		break;

	default: /* 4 */
		iowrite32(value, core_base_addr);
		break;
	}


	
	//enable memory mode
	ep430_en_memory_mode();
	
ignore_slot:	
	return PCIBIOS_SUCCESSFUL;
}

/*
 * Host Master Setup
 * */
static void ep430_host_bus_master_setup(void) {

	//enable 4 bytes config access
	u32 byte_en_size = 4;
	u32 config_addr = 0;
	u32 retval = 0;
	u8 	devfn = 0;
	u8 	bus = 0;
	u8 slot = 0;
	u32 i;
	/* Search all 32 slots on PCI bus 
	 * and look for 75G5 in Slot 2
	 * TODO: Verify with HW if there is going to be a 75XX 
	 * in slot 1
	 */	  
	 for (i=0; i<32; i++){		
		
		bus = 0; //bus number
		devfn = i << 3; //dev
		devfn |= 0 << 0; //func 	
		config_addr = ep430_config_addr(0,devfn, PCI_VENDOR_ID);
		slot = PCI_SLOT(devfn);
		
		//printk("ep430: config 0x: %x is 0x=%x\n", config_addr, ignore_slot);
		
		ep430_csr_config(config_addr, byte_en_size);
		retval = ioread32(core_base_addr);
		
		if ( (retval == NAI_75G5_PCI_DEVICE_ID ) && (slot == ignoreSlot) ) {
			printk("ep430: pci core device: 0x%x slot: 0x%x\n", retval, slot);
			//enable bus master 
			config_addr = ep430_config_addr(0,devfn, PCI_COMMAND);
			ep430_csr_config(config_addr, byte_en_size);
			
			retval = ioread32(core_base_addr);
			retval |=  PCI_COMMAND_MASTER;
			
			iowrite32(retval, core_base_addr);
			break;
		}
	 }
	 
}

static struct pci_ops ep430_ops = {
	.read	= ep430_read_config,
	.write	= ep430_write_config,
};

static int __init ep430_map_irq(const struct pci_dev *dev, u8 slot, u8 pin) {
	return pci_irq;
}

static void __init ep430_preinit(void) {

}

static int ep430_setup(int nr, struct pci_sys_data *sys) {
	int ret;
	struct device_node *parentnode, *childnode;
	struct resource *res;
	int childnodecount = 0;
	const char *desc;
	u32 isPCIFlag = 0;
	
	if (nr != 0) {
		pr_err("EP430: unexpected nr = %d\n", nr);
		ret = -EAGAIN;
		goto err;
	}
	
	parentnode = of_find_compatible_node(NULL, NULL, "nai,ep430d");
	if (!parentnode) {
		pr_err("ep430_setup: Unable to find nai,ep430d in dtb\n");
		ret = -EAGAIN;
		goto err;
	}
	
	//read master slot from dtb property
	ret = of_property_read_u8(parentnode, "ignore_slot", &ignoreSlot);
	if ( ret < 0 ){
		pr_err("ep430_setup: Unable to find ignore_slot property in dtb\n");
		goto err;
	}
	printk("EP430: Ignore Slot 0x%x \n", ignoreSlot);
	
	pci_irq = irq_of_parse_and_map(parentnode,0);
	printk("EP430: PCI_IRQ %d\n", pci_irq);
	if (pci_irq == 0)
	{
		pr_err("ep430_setup: Unable to find IRQ in dtb\n");
		ret = -EAGAIN;
		goto err;
	}
	
	childnodecount = of_get_child_count(parentnode);
	printk("EP430: child count %d\n", childnodecount);

	childnode = of_get_child_by_name(parentnode,"mb_common_addr");
	desc = of_get_property(childnode, "label", NULL);
	printk("EP430: mb_common_addr %s\n", desc);
	mb_common_addr = of_iomap(childnode,0);
	if (!mb_common_addr) {
		pr_err("EP430: mb_common_addr ioremap failed\n");
		ret = -ENOMEM;
		goto err;
	}

	childnode = of_get_child_by_name(parentnode,"cfg_base_addr");
	desc = of_get_property(childnode, "label", NULL);
	printk("EP430: base addr desc %s\n", desc);
	cfg_base_addr = of_iomap(childnode,0);
	if (!cfg_base_addr) {
		pr_err("EP430: cfg ioremap failed\n");
		ret = -ENOMEM;
		goto err_cfg_mem;
	}
				 
	childnode = of_get_child_by_name(parentnode,"core_base_addr");
	desc = of_get_property(childnode, "label", NULL);
	printk("EP430: base addr desc %s\n", desc);
	core_base_addr = of_iomap(childnode,0);
	if (!core_base_addr) {
		pr_err("EP430: core ioremap failed\n");
		ret = -ENOMEM;
		goto err_core_mem;
	}

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res) {
		pr_err("EP430: res alloc failed\n");
		ret = -ENOMEM;
		goto err_res_alloc;
	}


	res->name  = "EP430 PCI Memory Space";
	res->start = (resource_size_t)EP430_MEM_BASE_ADDR;
	res->end   = (resource_size_t)(EP430_MEM_BASE_ADDR +
				       EP430_MEM_BASE_SIZE - 1);
	res->flags = IORESOURCE_MEM;

	ret = request_resource(&iomem_resource, res);
	if (ret) {
		pr_err("EP430: failed to request resource\n");
		goto err_req_res;
	}

	//enable host bus master
	ep430_host_bus_master_setup();
	
	pci_add_resource_offset(&sys->resources, res, sys->mem_offset);
	
	//set PCI Master Flag in MB Common Area 
	isPCIFlag = ioread32(mb_common_addr+NAI_IS_PCI_MASTER_COMMON_AREA_OFFSET);
	isPCIFlag |= 0x1;
	iowrite32(isPCIFlag, mb_common_addr+NAI_IS_PCI_MASTER_COMMON_AREA_OFFSET);
	printk("EP430: Set PCI Master Flag %x \n", isPCIFlag);
	
	
	/* TODO do we need platform_notify and platform_notify_remove? */

	return 1;
err_req_res:
	kfree(res);
err_res_alloc:
	iounmap(core_base_addr);
err_core_mem:
	iounmap(cfg_base_addr);
err_cfg_mem:
	iounmap(mb_common_addr);
err:
	return ret;
}

static struct hw_pci ep430_hw_pci __initdata = {
	.setup		= ep430_setup,
	.map_irq	= ep430_map_irq,
	.nr_controllers = 1,
	.ops		= &ep430_ops,
	.preinit	= ep430_preinit,
};

int __init ep430_init(void) {
	pci_common_init(&ep430_hw_pci);

	return 0;
}

subsys_initcall(ep430_init);

MODULE_DESCRIPTION("Eureka Technology EP430 PCI host driver");
MODULE_AUTHOR("Obi Okafor <ookafor@naii.com>");
MODULE_LICENSE("GPLv2");

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
#include <linux/string.h>
#include <linux/semaphore.h>
#include <asm/io.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/printk.h>

#include "nai_serdes.h"
#include "nai_serdes_prv.h"
#include "nai_serdes_utils.h"

static void* g_LWHPS2FPGA_VirtualBaseHW = NULL; /*Hardware SERDES*/
static void* g_LWHPS2FPGA_VirtualBaseSW = NULL; /*Software SERDES*/
static void* g_MODULEPKTCFG_VirtualBase = NULL; /*Module Pkt Config*/
 
#define NAI_ACTUAL_MAX_SLOTS		 6

static u16 g_usMaxSlotCount        = NAI_THEORETICAL_MAX_SLOTS;
static u8  g_ucMsgUtilsInitialized = 0;
static u8  ucHardCodedModuleSlot   = 0;
static u8  g_ucSlotID              = NAI_INVALID_SLOT;
static u32 g_unModuleStartAddresses[NAI_THEORETICAL_MAX_SLOTS];

static u32 nai_is_mb(u8 ucSlotID);

static u32 nai_is_mb(u8 ucSlotID) {
	u32 is_mb = 0;

	if ((ucSlotID == NAI_MB_SLOT) || (ucSlotID == NAI_PPC_MB_SLOT))
		is_mb = 1;

	return is_mb;
}

void nai_set_virtual_base_hw(void *addr) {
	g_LWHPS2FPGA_VirtualBaseHW = addr;
	if (g_LWHPS2FPGA_VirtualBaseHW != NULL)
		pr_debug("SERDES HW: base addr=%p\n", g_LWHPS2FPGA_VirtualBaseHW);
	else
		pr_debug("SERDES HW IS NOT SUPPORTED\n");
}

void nai_set_virtual_base_sw(void *addr) {	
	g_LWHPS2FPGA_VirtualBaseSW = addr;
	if (g_LWHPS2FPGA_VirtualBaseSW != NULL)
		pr_debug("SERDES SW: base addr=%p\n", g_LWHPS2FPGA_VirtualBaseSW);
	else
		pr_debug("SERDES SW IS NOT SUPPORTED\n");
}

void nai_set_virtual_base_module_pkt_config(void *addr) {
	g_MODULEPKTCFG_VirtualBase = addr;
	if (g_MODULEPKTCFG_VirtualBase != NULL)
		pr_debug("MOUDLE PKT CONFIG: base addr=%p\n", g_MODULEPKTCFG_VirtualBase);
	else
		pr_debug("SERDES HW BLOCK IS NOT SUPPORTED\n");
}

void nai_assign_hard_coded_module_slot(uint8_t ucSlotID) {
	ucHardCodedModuleSlot = ucSlotID;
}

s32 nai_perform_init_slot_addressing(u32 *puModAddresses, u8 ucSize) {
	s32 nStatus = NAI__SUCCESS;
    
	if (ucHardCodedModuleSlot == 0) {
		u32 i = 0;

		if (g_ucMsgUtilsInitialized == 1) {
			goto exit;
		}

		if (ucSize < NAI_ACTUAL_MAX_SLOTS)
		{
			nStatus = NAI_INVALID_PARAMETER_VALUE;
			goto exit;
		}
			
		for (i = 0; i < NAI_THEORETICAL_MAX_SLOTS; ++i) {
			/* First initialize the entry to all F's */
			g_unModuleStartAddresses[i] = 0xFFFFFFFF;

			if (i < NAI_ACTUAL_MAX_SLOTS) {
				g_unModuleStartAddresses[i] = puModAddresses[i];
			}
		}
	}
	
	if (nStatus == NAI__SUCCESS)
		g_ucMsgUtilsInitialized = 1;	
exit:
	return nStatus;
}

s32 nai_retreive_module_slots_status_request(u16 *pusSlotIDs) {
	s32 nStatus = NAI__SUCCESS;
	u32 unModuleDetectedStatus = 0x00000100;
	FIFOValue tFIFOVal;
	u32 ulTimer;

	ulTimer = jiffies;
	while (1) {
		tFIFOVal.unValue = nai_read32(unModuleDetectedStatus);
		if ((tFIFOVal.unValue & 0x3F00) == 0x3F00)
			break;

		if (((s32)jiffies - (s32)ulTimer) > COMPLETION_TIMEOUT) {
			nStatus = NAI_DETECT_MODULES_TIMEOUT;
			break;
		}
		schedule();
	}

	if (nStatus == NAI__SUCCESS) {
		/* Now that detection process is complete..we can get the module status */
		*pusSlotIDs = (u16)(tFIFOVal.unValue & 0x003F);
	}

	return nStatus;
}

s32 nai_get_start_address_for_slot(u8 ucSlotID, u32 *punModuleStartAddress) {
	s32 nStatus = NAI__SUCCESS;
	
	if (ucSlotID == 0 || ucSlotID >= NAI_THEORETICAL_MAX_SLOTS)
		nStatus = NAI_ERROR_WRONG_SLOT_NUM;
		
	*punModuleStartAddress = g_unModuleStartAddresses[(ucSlotID-1)];
	
	return nStatus;
}

s32 nai_get_module_id_and_address(u32 unAddress, u8 *pucModuleID,
				  u32 *punModuleAddress) {
	s32 nStatus = NAI__SUCCESS;

	if (!ucHardCodedModuleSlot)	{
		u8 i = 0;
		u8 k = 0;
		u32 bFound = 0;

		for (i = 0; i < NAI_THEORETICAL_MAX_SLOTS; ++i) {
			/* Skip over any unused module slots */
			if (g_unModuleStartAddresses[i] == 0xFFFFFFFF)
				continue;

			if (unAddress == g_unModuleStartAddresses[i]) {
				*pucModuleID = (i + 1);
				*punModuleAddress = unAddress -
					g_unModuleStartAddresses[i];
				bFound = 1;
				break;
			}

			k = i + 1;
			while ((k < NAI_THEORETICAL_MAX_SLOTS) &&
			       (g_unModuleStartAddresses[k] == 0xFFFFFFFF))
				k++;

			/* If no more slots to check...break */
			if (k == NAI_THEORETICAL_MAX_SLOTS) {
				if (unAddress > g_unModuleStartAddresses[i]) {
					*pucModuleID = (i + 1);
					*punModuleAddress = unAddress -
						g_unModuleStartAddresses[i];
					bFound = 1;
				}
				break;
			}

			/* Now let's check to see if we found the slot the given address belongs to */
			if ((unAddress > g_unModuleStartAddresses[i]) &&
			    (unAddress < g_unModuleStartAddresses[k])) {
				*pucModuleID = (i + 1);
				*punModuleAddress = unAddress -
					g_unModuleStartAddresses[i];
				bFound = 1;
				break;
			}

			i = k - 1;
		}

		if (!bFound)
			nStatus = NAI_MODULE_NOT_FOUND;
	}
	else {
		*pucModuleID = ucHardCodedModuleSlot;
		*punModuleAddress = unAddress;
	}

	return nStatus;
}

s32 nai_get_module_packet_config_address_offset(u32 unAddress, u8 ucCompleterID, u32 *punPacketConfigAddressOffset) {
	s32 nStatus = NAI__SUCCESS;

	switch (ucCompleterID) {
		case 1 :
			*punPacketConfigAddressOffset = 0x00000000; /*0x43C100A0*/
			break;
		case 2 :
			*punPacketConfigAddressOffset = 0x00000004; /*0x43C100A4*/
			break;
		case 3 :
			*punPacketConfigAddressOffset = 0x00000008; /*0x43C100A8*/
			break;
		case 4 :
			*punPacketConfigAddressOffset = 0x0000000C; /*0x43C100AC*/
			break;
		case 5 :
			*punPacketConfigAddressOffset = 0x00000010; /*0x43C100B0*/
			break;
		case 6 :
			*punPacketConfigAddressOffset = 0x00000014; /*0x43C100B4*/
			break;
		default :
			nStatus = NAI_INVALID_ADDRESS;
			break;		
	}
    
	return nStatus;
}

void nai_write16(u32 unAddress, u16 usValue) {
	if (g_LWHPS2FPGA_VirtualBaseSW != NULL) {
		pr_debug("SERDES SW: WR16 addr=%p val=0x%04X\n",
			 (void *)(g_LWHPS2FPGA_VirtualBaseSW + unAddress), usValue);

		iowrite16(usValue, g_LWHPS2FPGA_VirtualBaseSW + unAddress);
	}
	else
		pr_debug("SERDES SW IS NOT SUPPORTED\n");
}

u16 nai_read16(u32 unAddress) {
	u16 val = 0;
	if (g_LWHPS2FPGA_VirtualBaseSW != NULL) {
		val = ioread16(g_LWHPS2FPGA_VirtualBaseSW + unAddress);

		pr_debug("SERDES SW: RD16 addr=%p val=0x%04X\n",
			 (void *)(g_LWHPS2FPGA_VirtualBaseSW + unAddress), val);
	}
	else
		pr_debug("SERDES SW IS NOT SUPPORTED\n");
		
	return val;
}

void nai_write32(u32 unAddress, u32 unValue) {
	if (g_LWHPS2FPGA_VirtualBaseHW != NULL) {	
		pr_debug("SERDES HW: WR32 addr=%p val=0x%08X\n",
			 (void *)(g_LWHPS2FPGA_VirtualBaseHW + unAddress), unValue);
		
		iowrite32(unValue, g_LWHPS2FPGA_VirtualBaseHW + unAddress);
	}
	else
		pr_debug("SERDES HW IS NOT SUPPORTED\n");
}

u32 nai_read32(u32 unAddress) {
	u32 val = 0;
	if (g_LWHPS2FPGA_VirtualBaseHW != NULL) {
		val = ioread32(g_LWHPS2FPGA_VirtualBaseHW + unAddress);

		pr_debug("SERDES HW: RD32 addr=%p val=0x%08X\n",
			 (void *)(g_LWHPS2FPGA_VirtualBaseHW + unAddress), val);
	}
	else
		pr_debug("SERDES HW IS NOT SUPPORTED\n");
			 
	return val;
}

void nai_write32_SW(u32 unAddress, u32 unValue) {
	if (g_LWHPS2FPGA_VirtualBaseSW != NULL) {
		pr_debug("SERDES SW: WR32 addr=%p val=0x%08X\n",
			 (void *)(g_LWHPS2FPGA_VirtualBaseSW + unAddress), unValue);
		
		iowrite32(unValue, g_LWHPS2FPGA_VirtualBaseSW + unAddress);
	}
	else
		pr_debug("SERDES SW IS NOT SUPPORTED\n");
}

u32 nai_read32_SW(u32 unAddress) {
	u32 val = 0;
	if (g_LWHPS2FPGA_VirtualBaseSW != NULL) {
		val = ioread32(g_LWHPS2FPGA_VirtualBaseSW + unAddress);

		pr_debug("SERDES SW: RD32 addr=%p val=0x%08X\n",
			 (void *)(g_LWHPS2FPGA_VirtualBaseSW + unAddress), val);
	}
	else
		pr_debug("SERDES SW IS NOT SUPPORTED\n");
			 
	return val;
}

void nai_write_32_ModPktCfg(u32 unAddressOffset, u32 unValue) {
	if (g_MODULEPKTCFG_VirtualBase != NULL) {
		pr_debug("MOUDLE PKT CONFIG: WR32 addr=%p val=0x%08X\n",
			 (void *)(g_MODULEPKTCFG_VirtualBase + unAddressOffset), unValue);
		
		iowrite32(unValue, g_MODULEPKTCFG_VirtualBase + unAddressOffset);	
	}
	else
		pr_debug("SERDES HW BLOCK IS NOT SUPPORTED\n");
}

u32 nai_read32_ModPktCfg(u32 unAddressOffset) {
	u32 val = 0;
	if (g_MODULEPKTCFG_VirtualBase != NULL) {
		val = ioread32(g_MODULEPKTCFG_VirtualBase + unAddressOffset);

		pr_debug("MOUDLE PKT CONFIG: RD32 addr=%p val=0x%08X\n",
			 (void *)(g_MODULEPKTCFG_VirtualBase + unAddressOffset), val);
	}
			 
	return val;
}

u32 nai_get_tx_fifo_address(u8 ucRequesterID, u8 ucCompleterID) {
	u32 unTxAddr = 0;

	if (nai_is_mb(ucRequesterID)) {
		switch (ucCompleterID) {
		case NAI_MODULE_1_SLOT:
			unTxAddr = 0x00000000;
			break;

		case NAI_MODULE_2_SLOT:
			unTxAddr = 0x00000004;
			break;

		case NAI_MODULE_3_SLOT:
			unTxAddr = 0x00000008;
			break;

		case NAI_MODULE_4_SLOT:
			unTxAddr = 0x0000000C;
			break;

		case NAI_MODULE_5_SLOT:
			unTxAddr = 0x00000010;
			break;

		case NAI_MODULE_6_SLOT:
			unTxAddr = 0x00000014;
			break;
		}
	}

	return unTxAddr;
}

u32 nai_get_tx_fifo_pkt_ready_address(u8 ucRequesterID, u8 ucCompleterID) {
	u32 unTxPktReadyAddr = 0x00000020;

	if (nai_is_mb(ucRequesterID)) {
		switch (ucCompleterID) {
		case NAI_MODULE_1_SLOT:
			unTxPktReadyAddr = 0x00000020;
			break;

		case NAI_MODULE_2_SLOT:
			unTxPktReadyAddr = 0x00000024;
			break;

		case NAI_MODULE_3_SLOT:
			unTxPktReadyAddr = 0x00000028;
			break;

		case NAI_MODULE_4_SLOT:
			unTxPktReadyAddr = 0x0000002C;
			break;

		case NAI_MODULE_5_SLOT:
			unTxPktReadyAddr = 0x00000030;
			break;

		case NAI_MODULE_6_SLOT:
			unTxPktReadyAddr = 0x00000034;
			break;
		}
	}

	return unTxPktReadyAddr;
}

u32 nai_get_rx_fifo_address(u8 ucCompleterID) {
	u32 unRxAddr = 0x00000080;

	if (nai_is_mb(g_ucSlotID)) {
		switch (ucCompleterID) {
		case NAI_MODULE_1_SLOT:
			unRxAddr = 0x00000080;
			break;

		case NAI_MODULE_2_SLOT:
			unRxAddr = 0x00000084;
			break;

		case NAI_MODULE_3_SLOT:
			unRxAddr = 0x00000088;
			break;

		case NAI_MODULE_4_SLOT:
			unRxAddr = 0x0000008C;
			break;

		case NAI_MODULE_5_SLOT:
			unRxAddr = 0x00000090;
			break;

		case NAI_MODULE_6_SLOT:
			unRxAddr = 0x00000094;
			break;
		}
	}

	return unRxAddr;
}

u32 nai_get_rx_fifo_num_words_address(u8 ucRequesterID, u8 ucCompleterID) {
	u32 unRxNumWordsAddr = 0x000000A0;

	if (nai_is_mb(ucRequesterID)) {
		switch (ucCompleterID) {
		case NAI_MODULE_1_SLOT:
			unRxNumWordsAddr = 0x000000A0;
			break;

		case NAI_MODULE_2_SLOT:
			unRxNumWordsAddr = 0x000000A4;
			break;

		case NAI_MODULE_3_SLOT:
			unRxNumWordsAddr = 0x000000A8;
			break;

		case NAI_MODULE_4_SLOT:
			unRxNumWordsAddr = 0x000000AC;
			break;

		case NAI_MODULE_5_SLOT:
			unRxNumWordsAddr = 0x000000B0;
			break;

		case NAI_MODULE_6_SLOT:
			unRxNumWordsAddr = 0x000000B4;
			break;
		}
	}

	return unRxNumWordsAddr;
}

u32 nai_tx_fifo_empty(u8 ucRequesterID, u8 ucCompleterID) {
	u32 bFIFOEmpty = 1;
	u32 unFIFOsEmptyStatusRegister = 0x00000040;
	u8 ucTemp;

	if (ucRequesterID == NAI_ASSIGNED_SLOT)
		ucRequesterID = g_ucSlotID;

	if (nai_is_mb(ucRequesterID)) {
		if (ucCompleterID > g_usMaxSlotCount)
			return 0;

		unFIFOsEmptyStatusRegister = 0x00000040;

		ucTemp = (u8)(nai_read16(unFIFOsEmptyStatusRegister));
		bFIFOEmpty = (((ucTemp >> (ucCompleterID-1)) & 0x01) == 0x01);
	}
	else {
		ucTemp = (u8)(nai_read16(unFIFOsEmptyStatusRegister));
		bFIFOEmpty = (ucTemp == 0x01);
	}

	return bFIFOEmpty;
}

u32 nai_rx_fifo_empty(u8 ucRequesterID, u8 ucCompleterID) {
	u32 bFIFOEmpty = 1;
	u32 unFIFOsEmptyStatusRegister = 0x000000C0;
	u8 ucTemp;

	if (ucRequesterID == NAI_ASSIGNED_SLOT)
		ucRequesterID = g_ucSlotID;

	if (nai_is_mb(ucRequesterID)) {
		if (ucCompleterID > g_usMaxSlotCount)
			return 0;

		unFIFOsEmptyStatusRegister = 0x000000C0;

		ucTemp = (u8)(nai_read16(unFIFOsEmptyStatusRegister));
		bFIFOEmpty = (((ucTemp >> (ucCompleterID-1)) & 0x01) == 0x01);
	}
	else {
		u8 ucTemp = (u8)(nai_read16(unFIFOsEmptyStatusRegister));
		bFIFOEmpty = (ucTemp == 0x01);
	}

	return bFIFOEmpty;
}

u32 nai_rx_fifo_pkt_ready(u8 ucRequesterID, u8 ucCompleterID) {
	u32 bPktReady = 0;
	u32 unFIFOsPktReadyStatusRegister = 0x000000E4;
	u8 ucTemp;

	if (ucRequesterID == NAI_ASSIGNED_SLOT)
		ucRequesterID = g_ucSlotID;

	if (nai_is_mb(ucRequesterID)) {
		if (ucCompleterID > g_usMaxSlotCount)
			return 0;

		unFIFOsPktReadyStatusRegister = 0x000000E4;

		ucTemp = (u8)(nai_read16(unFIFOsPktReadyStatusRegister));
		bPktReady = (((ucTemp >> (ucCompleterID-1)) & 0x01) == 0x01);
	}
	else {
		ucTemp = (u8)(nai_read16(unFIFOsPktReadyStatusRegister));
		bPktReady = (ucTemp == 0x01);
	}

	return bPktReady;
}

void nai_rx_fifo_clear_pkt_ready(u8 ucRequesterID, u8 ucCompleterID) {
	u32 unFIFOsClearPktReadyStatusRegister = 0x000000E4;

	if (ucRequesterID == NAI_ASSIGNED_SLOT)
		ucRequesterID = g_ucSlotID;

	if (nai_is_mb(ucRequesterID)) {
		if (ucCompleterID > g_usMaxSlotCount)
			return;

		unFIFOsClearPktReadyStatusRegister = 0x000000E4;

		/* Write a 1 to clear pkt Ready Status register */
		nai_write16(unFIFOsClearPktReadyStatusRegister,
			    (u16)(0x01 << (ucCompleterID-1)));
	}
	else {
		nai_write16(unFIFOsClearPktReadyStatusRegister, (u16)0x0001);
	}
}

s32 nai_init_as_slot(u8 ucSlotID) {
	if (ucSlotID > g_usMaxSlotCount)
		return NAI_INVALID_SLOT_ID;
		
	g_ucSlotID = ucSlotID;

	return NAI__SUCCESS;
}

u8 nai_get_global_slot_id(void) {
	return g_ucSlotID;
}

u16 nai_get_max_slot_count(void) {
	return g_usMaxSlotCount;
}

u32 convert_bytes_to_words(u32 unBytes) {
	u32 unWordCount = 0;

	if (unBytes & 0x01)
		unWordCount = 1;

	unWordCount += unBytes / 2;

	return unWordCount;
}

u8 nai_is_hw_serdes_capable(void) {
	return ((g_LWHPS2FPGA_VirtualBaseHW != NULL)?1:0);
}

u8 nai_is_sw_serdes_capable(void) {
	return ((g_LWHPS2FPGA_VirtualBaseSW != NULL)?1:0);
}

u8 nai_is_hw_block_serdes_capable(void) {
	return (((g_MODULEPKTCFG_VirtualBase != NULL) && (g_LWHPS2FPGA_VirtualBaseHW != NULL))?1:0);
}

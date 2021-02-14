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

#ifndef __NAI_SERDES_UTILS_H__
#define __NAI_SERDES_UTILS_H__

#include <linux/kernel.h>
#include <linux/types.h>

#define NAI_THEORETICAL_MAX_SLOTS   10
/* MessageUtils */
extern u32 convert_bytes_to_words(u32 ulBytes);

/* MessageProcessing */
extern s32 nai_perform_init_slot_addressing(u32 *puModAddresses, u8 ucSize);
extern s32 nai_get_start_address_for_slot(u8 ucSlotID, u32 *punModuleStartAddress);
extern s32 nai_get_module_id_and_address(u32 unAddress, u8 *pucModuleID, u32 *punModuleAddress);
extern s32 nai_get_module_packet_config_address_offset(u32 unAddress, u8 ucCompleterID, u32 *punPacketConfigAddressOffset);
					 
extern void nai_write16(u32 unAddress, u16 usValue);
extern u16 nai_read16(u32 unAddress);
extern void nai_write32(u32 unAddress, u32 unValue);		/* Uses hardware serdes */
extern u32 nai_read32(u32 unAddress);						/* Uses hardware serdes */
extern void nai_write32_SW(u32 unAddress, u32 unValue);		/* Uses software serdes */
extern u32 nai_read32_SW(u32 unAddress);  					/* Uses software serdes */
extern void nai_write_32_ModPktCfg(u32 unAddressOffset, u32 unValue);
extern u32 nai_read32_ModPktCfg(u32 unAddressOffset);

extern u32 nai_get_tx_fifo_address(u8 ucRequesterID, u8 ucCompleterID);
extern u32 nai_get_tx_fifo_pkt_ready_address(u8 ucRequesterID,
					     u8 ucCompleterID);
extern u32 nai_get_rx_fifo_address(u8 ucCompleterID);
extern u32 nai_get_rx_fifo_num_words_address(u8 ucRequesterID,
					     u8 ucCompleterID);
extern u32 nai_tx_fifo_empty(u8 ucRequesterID, u8 ucCompleterID);
extern u32 nai_rx_fifo_empty(u8 ucRequesterID, u8 ucCompleterID);
extern u32 nai_rx_fifo_pkt_ready(u8 ucRequesterID, u8 ucCompleterID);
extern void nai_rx_fifo_clear_pkt_ready(u8 ucRequesterID, u8 ucCompleterID);

/* HighLevelAPI */
extern void nai_assign_hard_coded_module_slot(u8 ucSlotID);
extern s32 nai_retreive_module_slots_status_request(u16 *pusSlotIDs);
extern s32 nai_init_as_slot(u8 ucSlotID);
extern u16 nai_get_max_slot_count(void);
extern u8 nai_get_global_slot_id(void);
extern void nai_set_virtual_base_hw(void *addr);
extern void nai_set_virtual_base_sw(void *addr);
extern void nai_set_virtual_base_module_pkt_config(void *addr);
extern u8 nai_is_hw_serdes_capable(void);
extern u8 nai_is_sw_serdes_capable(void);
extern u8 nai_is_hw_block_serdes_capable(void);
#endif /* __NAI_SERDES_UTILS_H__ */

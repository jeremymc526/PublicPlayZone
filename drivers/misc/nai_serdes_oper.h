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

#ifndef __NAI_SERDES_OPER_H__
#define __NAI_SERDES_OPER_H__

#include <linux/kernel.h>
#include <linux/types.h>

/* Operational Message Utils */
extern s32 nai_init_msg_utils(u8 ucID);

/* Addressing Scheme */
extern s32 nai_read_reg16_request(u32 unAddress, u16 *pusValue);
extern s32 nai_write_reg16_request(u32 unAddress, u16 usValue);
extern s32 nai_read_reg32_request(u32 unAddress, u32 *punValue);
extern s32 nai_write_reg32_request(u32 unAddress, u32 unValue);
extern s32 nai_read_block16_request(u32 unStartAddress, u32 unCount,
				    u8 ucStride, u16 *pusDataBuf);
extern s32 nai_write_block16_request(u32 unStartAddress, u32 unCount,
				     u8 ucStride, u16 *pusDataBuf);
extern s32 nai_read_block32_request(u32 unStartAddress, u32 unCount,
				    u8 ucStride, u32 *punDataBuf);
extern s32 nai_write_block32_request(u32 unStartAddress, u32 unCount,
				     u8 ucStride, u32 *punDataBuf);

/* Module Slot Scheme */
extern s32 nai_read_reg16_by_slot_request(u8 ucSlotID, u32 unModuleOffset,
					  u16 *pusValue);
extern s32 nai_write_reg16_by_slot_request(u8 ucSlotID, u32 unModuleOffset,
					   u16 usValue);
extern s32 nai_read_reg32_by_slot_request(u8 ucSlotID, u32 unModuleOffset,
					   u32 *punValue);
extern s32 nai_write_reg32_by_slot_request(u8 ucSlotID, u32 unModuleOffset,
					   u32 unValue);
extern s32 nai_read_block16_by_slot_request(u8 ucSlotID,
					    u32 unModuleOffsetStart,
					    u32 unCount, u8 ucStride,
					    u16 *pusDataBuf);
extern s32 nai_write_block16_by_slot_request(u8 ucSlotID,
					     u32 unModuleOffsetStart,
					     u32 unCount, u8 ucStride,
					     u16 *pusDataBuf);
extern s32 nai_read_block32_by_slot_request(u8 ucSlotID,
					    u32 unModuleOffsetStart,
					    u32 unCount, u8 ucStride,
					    u32 *punDataBuf);
extern s32 nai_write_block32_by_slot_request(u8 ucSlotID,
					     u32 unModuleOffsetStart,
					     u32 unCount, u8 ucStride,
					     u32 *punDataBuf);

#endif /* __NAI_SERDES_OPER_H__ */

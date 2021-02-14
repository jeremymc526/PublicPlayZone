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
#ifndef __NAI_SERDES_CONFIG_H__
#define __NAI_SERDES_CONFIG_H__

/* HighLevelAPI */
extern s32 nai_read_module_eeprom_request(u16 usChipID, u8 ucRequesterID, u8 ucCompleterID, u32 unEepromOffset, u8 *pucBuf, s32 nLen);
extern s32 nai_write_module_eeprom_request(u16 usChipID, u8 ucRequesterID,u8 ucCompleterID, u32 unEepromOffset, u8 *pucBuf, s32 nLen);
extern s32 nai_erase_flash_request(u8 ucRequesterID, u8 ucCompleterID, u32 unFlashOffset, u8 ucNumPages);
extern s32 nai_read_module_flash_request(u8 ucRequesterID, u8 ucCompleterID, u32 unFlashOffset, u8 *pucBuf, s32 nLen);
extern s32 nai_write_module_flash_request(u8 ucRequesterID, u8 ucCompleterID, u32 unFlashOffset, u8 *pucBuf, s32 nLen);

/* Microcontroller */
extern s32 nai_write_micro_request(u8 ucRequesterID, u8 ucCompleterID, u8 ucChannel, u32 unOffset, u8 *pucBuf, s32 nLen);
extern s32 nai_get_micro_request(u8 ucRequesterID, u8 ucCompleterID, u8 ucChannel, u8 *pucBuf, s32 nLen);
extern s32 nai_erase_micro_request(u8 ucRequesterID, u8 ucCompleterID, u8 ucChannel);

#endif /* __NAI_SERDES_CONFIG_H__ */

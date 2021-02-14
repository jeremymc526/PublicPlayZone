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

#ifndef __NAI_SERDES_H__
#define __NAI_SERDES_H__

#include <linux/types.h>
#include <linux/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error Codes */
#define NAI__SUCCESS 				       		 0
#define NAI_ERROR_WRONG_SLOT_NUM 		       	-8201
#define NAI_INVALID_SLOT_ID			       		-8202
#define NAI_SERDES_UNEXPECTED_PAYLOAD_COUNT     -8203
#define NAI_MODULE_NOT_FOUND			       	-8204
#define NAI_MIS_ALIGNED_BYTE_ENABLE             -8205
#define NAI_INVALID_PARAMETER_VALUE		       	-8206
#define NAI_SYSTEM_NOT_READY			       	-8207
#define NAI_MODULE_NOT_READY			       	-8208
#define NAI_UNABLE_TO_ALLOCATE_MEMORY		    -8209
#define NAI_COMMAND_NOT_RECOGNIZED		       	-8210
#define NAI_TX_FIFO_NOT_EMPTY_TIMEOUT	        -8211
#define NAI_RX_FIFO_PKT_NOT_READY_TIMEOUT       -8212
#define NAI_DETECT_MODULES_TIMEOUT		       	-8213
#define NAI_I2C_DEVICE_NOT_FOUND		       	-8214
#define NAI_UNABLE_TO_LOCK_MUTEX   	   	       	-8215
#define NAI_UNABLE_TO_UNLOCK_MUTEX		       	-8216
#define NAI_STRIDE_CAUSES_MISALIGNMENT			-8217
#define NAI_USER_COPY_FAILED					-8218
#define NAI_MODULE_DETECT_READY_TIMEOUT			-8219
#define NAI_MODULE_LINK_DETECT_TIMEOUT			-8220
#define NAI_ENTER_CONFIG_MODE_TIMEOUT       	-8221
#define NAI_STM_TX_TIMEOUT						-8222
#define NAI_STM_RX_TIMEOUT						-8223
#define NAI_ACK_NOT_RECEIVED					-8224
#define NAI_POTENTIAL_BUFFER_OVERRUN			-8225
#define NAI_CPLD_PROGRAMMING_ERROR				-8226
#define NAI_INVALID_PAYLOAD_LENGTH				-8227
#define NAI_INVALID_PACKET_TYPE					-8228
#define NAI_RESPONSE_COMMAND_MISMATCH			-8229
#define NAI_COMMAND_FAILED				    	-8230
#define NAI_NOT_SUPPORTED						-8231
#define NAI_INVALID_ADDRESS						-8232

/* Misc */
#define NAI_MB_SLOT				       	   0x00
#define NAI_MODULE_1_SLOT			       0x01
#define NAI_MODULE_2_SLOT			       0x02
#define NAI_MODULE_3_SLOT			       0x03
#define NAI_MODULE_4_SLOT			       0x04
#define NAI_MODULE_5_SLOT			       0x05
#define NAI_MODULE_6_SLOT			       0x06
#define NAI_PPC_MB_SLOT				       0x0A
#define NAI_ASSIGNED_SLOT			       0xFE
#define NAI_INVALID_SLOT			       0xFF

#define NAI_MODULE_FLASH_READ			   0x01
#define NAI_MODULE_FLASH_WRITE			   0x02
#define NAI_MODULE_FLASH_ERASE			   0x03
#define NAI_MODULE_MICRO_GET			   0x04
#define NAI_MODULE_MICRO_WRITE			   0x05
#define NAI_MODULE_MICRO_ERASE			   0x06

/* Block Write*/
#define NAI_MAX_COUNT_PER_WRITE			   400000


typedef struct _nai_serdes_slot {
	__s32 slot;
	__s32 rc;
} nai_serdes_slot;

typedef struct _nai_serdes_addr {
       __u32 addr[8];
       __s32 rc;
} nai_serdes_addr;

typedef struct _nai_serdes_16 {
	__s32 slot;
        __u32 addr;
	__u16 val;
	__s32 rc;
} nai_serdes_16;

typedef struct _nai_serdes_32 {
	__s32 slot;
	__u32 addr;
	__u32 val;
	__s32 rc;
} nai_serdes_32;

typedef struct _nai_serdes_blk_16 {
	__s32 slot;
	__u32 addr;
	__u16 count;
	__u8  stride;
	__u16 *val;
	__s32 rc;
} nai_serdes_blk_16;

typedef struct _nai_serdes_blk_32 {
	__s32 slot;
	__u32 addr;
	__u16 count;
	__u8  stride;
	__u32 *val;
	__s32 rc;
} nai_serdes_blk_32;

typedef struct _nai_serdes_blk_16_large {
	__s32 slot;
	__u32 addr;
	__u32 count;
	__u8  stride;
	__u16 *val;
	__s32 rc;
} nai_serdes_blk_16_large;

typedef struct _nai_serdes_blk_32_large {
	__s32 slot;
	__u32 addr;
	__u32 count;
	__u8  stride;
	__u32 *val;
	__s32 rc;
} nai_serdes_blk_32_large;

typedef struct _nai_serdes_module_eeprom {
	__u16 chipId;
	__u8 reqId;
	__u8 compId;
	__u32 eepromOffset;
	__u8 *buff;
	__s32 len;
	__s32 rc;
} nai_serdes_module_eeprom;

typedef struct _nai_serdes_module_flash {
	__u8 reqId;
	__u8 compId;
	__u32 flashOffset;
	__u8 numPage;
	__u8 *buff;
	__s32 len;
	__s32 rc;
} nai_serdes_module_flash;

typedef struct _nai_serdes_module_micro {
	__u8 reqId;
	__u8 compId;
	__u8 channel;	
	__u32 flashOffset;	
	__u8 *buff;
	__s32 len;
	__s32 rc;
} nai_serdes_module_micro;

typedef struct _nai_module_op {
	__u32 slot;
	__u32 enable;
	__s32 rc;
} nai_module_op;

typedef struct _nai_module_serdes_revision {
	__u8 serdesMajorRev;
	__u8 serdesMinorRev;
} nai_module_serdes_revision;

#define NAI_SERDES_MAGIC            0xDD

#define NAI_IOC_SERDES_SET_SLOTID _IOWR(NAI_SERDES_MAGIC, 1,  nai_serdes_slot)
#define NAI_IOC_SERDES_INIT_ADDR  _IOW (NAI_SERDES_MAGIC, 2,  nai_serdes_addr)
#define NAI_IOC_SERDES_RD_REG16   _IOWR(NAI_SERDES_MAGIC, 3,  nai_serdes_16)
#define NAI_IOC_SERDES_WR_REG16   _IOWR(NAI_SERDES_MAGIC, 4,  nai_serdes_16)
#define NAI_IOC_SERDES_RD_REG32   _IOWR(NAI_SERDES_MAGIC, 5,  nai_serdes_32)
#define NAI_IOC_SERDES_WR_REG32   _IOWR(NAI_SERDES_MAGIC, 6,  nai_serdes_32)
#define NAI_IOC_SERDES_RD_BLK16   _IOWR(NAI_SERDES_MAGIC, 7,  nai_serdes_blk_16)
#define NAI_IOC_SERDES_WR_BLK16   _IOWR(NAI_SERDES_MAGIC, 8,  nai_serdes_blk_16)
#define NAI_IOC_SERDES_RD_BLK32   _IOWR(NAI_SERDES_MAGIC, 9,  nai_serdes_blk_32)
#define NAI_IOC_SERDES_WR_BLK32   _IOWR(NAI_SERDES_MAGIC, 10, nai_serdes_blk_32)

#define NAI_IOC_SERDES_RD_MODULE_EEPROM   			_IOWR(NAI_SERDES_MAGIC, 11, nai_serdes_module_eeprom)
#define NAI_IOC_SERDES_WR_MODULE_EEPROM   			_IOWR(NAI_SERDES_MAGIC, 12, nai_serdes_module_eeprom)
#define NAI_IOC_SERDES_ERASE_MODULE_FLASH   		_IOWR(NAI_SERDES_MAGIC, 13, nai_serdes_module_flash)
#define NAI_IOC_SERDES_RD_MODULE_FLASH   			_IOWR(NAI_SERDES_MAGIC, 14, nai_serdes_module_flash)
#define NAI_IOC_SERDES_WR_MODULE_FLASH   			_IOWR(NAI_SERDES_MAGIC, 15, nai_serdes_module_flash)
#define NAI_IOC_GET_MODULE_DETECTED  				_IOWR(NAI_SERDES_MAGIC, 16, nai_module_op)
#define NAI_IOC_GET_MODULE_LINK_INIT 				_IOWR(NAI_SERDES_MAGIC, 17, nai_module_op)
#define NAI_IOC_SET_MODULE_RESET     				_IOW (NAI_SERDES_MAGIC, 18, nai_module_op)
#define NAI_IOC_SET_MODULE_CONFIG_MODE    			_IOWR(NAI_SERDES_MAGIC, 19, nai_module_op)
#define NAI_IOC_CLR_MODULE_CONFIG_MODE    			_IOWR(NAI_SERDES_MAGIC, 20, nai_module_op)
#define NAI_IOC_GET_MODULE_CONFIG_STATE_RQ_ACK		_IOWR(NAI_SERDES_MAGIC, 21, nai_module_op)
#define NAI_IOC_GET_MODULE_CONFIG_STATE_RDY   		_IOWR(NAI_SERDES_MAGIC, 22, nai_module_op)
#define NAI_IOC_GET_MODULE_OPER_BM_ENTERED    		_IOWR(NAI_SERDES_MAGIC, 23, nai_module_op)
#define NAI_IOC_GET_MODULE_OPER_BM_COMMONPOPULATED  _IOWR(NAI_SERDES_MAGIC, 24, nai_module_op)
#define NAI_IOC_GET_MODULE_OPER_BM_PARAM_LOADED  	_IOWR(NAI_SERDES_MAGIC, 25, nai_module_op)
#define NAI_IOC_GET_MODULE_OPER_BM_CALIB_LOADED    	_IOWR(NAI_SERDES_MAGIC, 26, nai_module_op)
#define NAI_IOC_GET_MODULE_OPER_BM_RDY    			_IOWR(NAI_SERDES_MAGIC, 27, nai_module_op)
#define NAI_IOC_SERDES_WR_MICRO					    _IOWR(NAI_SERDES_MAGIC, 28, nai_serdes_module_micro)
#define NAI_IOC_SERDES_GET_MICRO				   	_IOWR(NAI_SERDES_MAGIC, 29, nai_serdes_module_micro)
#define NAI_IOC_SERDES_ERASE_MICRO				   	_IOWR(NAI_SERDES_MAGIC, 30, nai_serdes_module_micro)
#define NAI_IOC_KILL_MODULE_SERDES				   	_IOWR(NAI_SERDES_MAGIC, 31, nai_module_op)
#define NAI_IOC_GET_MODULE_SERDES_REVISION		   	_IOR(NAI_SERDES_MAGIC, 32,  nai_module_serdes_revision)
#define NAI_IOC_SERDES_RD_BLK16_LARGE              	_IOWR(NAI_SERDES_MAGIC, 33, nai_serdes_blk_16_large)
#define NAI_IOC_SERDES_WR_BLK16_LARGE              	_IOWR(NAI_SERDES_MAGIC, 34, nai_serdes_blk_16_large)
#define NAI_IOC_SERDES_RD_BLK32_LARGE              	_IOWR(NAI_SERDES_MAGIC, 35, nai_serdes_blk_32_large)
#define NAI_IOC_SERDES_WR_BLK32_LARGE              	_IOWR(NAI_SERDES_MAGIC, 36, nai_serdes_blk_32_large)
#ifdef __cplusplus
}
#endif

#endif /* __NAI_SERDES_H__ */

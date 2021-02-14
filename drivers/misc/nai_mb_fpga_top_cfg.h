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

#ifndef __NAI_MB_FPGA_TOP_CFG_
#define __NAI_MB_FPGA_TOP_CFG_

#include <linux/types.h>
#include <linux/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAI_MISC_DEV_NAME	            "nai_mb_fgpa_cfg"
#define NAI_COMMON_TYPE_8_BIT       0x01
#define NAI_COMMON_TYPE_16_BIT      0x02
#define NAI_COMMON_TYPE_32_BIT      0x04

#define NAI_COMMON_MODE_COMMON      0x01
#define NAI_COMMON_MODE_HANDSHAKE   0x02

#define NAI_COMMON_NUM_FW_REVS      9

#define NAI_COMMON_MODULE_1         0
#define NAI_COMMON_MODULE_2         1
#define NAI_COMMON_MODULE_3         2
#define NAI_COMMON_MODULE_4         3
#define NAI_COMMON_MODULE_5         4
#define NAI_COMMON_MODULE_6         5
#define NAI_COMMON_MODULE_7         6

typedef struct _nai_common_reg {
	__u32 offset;
	__u32 type;
	__u32 mode;
	union {
		__u32 val32;
		__u16 val16;
		__u8  val8;
	};
} nai_common_reg;

typedef struct _nai_common_blk {
	__u32 offset;
	__u32 type;
	__u32 count;
	__u32 mode;
	void *val;
} nai_common_blk;

typedef struct _nai_firmware_revs {
	__u32 revisions[NAI_COMMON_NUM_FW_REVS];
} nai_firmware_revs;

typedef struct _nai_module_addr {
	__u32 module_id;
	__u16 start;
	__u16 end;
	__u32 mask;
} nai_module_addr;



#define NAI_COMMON_MAGIC            0xDE

#define NAI_IOC_COMMON_RD_REG               _IOWR(NAI_COMMON_MAGIC,  1, nai_common_reg)
#define NAI_IOC_COMMON_WR_REG               _IOWR(NAI_COMMON_MAGIC,  2, nai_common_reg)
#define NAI_IOC_COMMON_RD_BLK               _IOWR(NAI_COMMON_MAGIC,  3, nai_common_blk)
#define NAI_IOC_COMMON_WR_BLK               _IOWR(NAI_COMMON_MAGIC,  4, nai_common_blk)
#define NAI_IOC_COMMON_GET_FW_REVS          _IOR (NAI_COMMON_MAGIC,  5, nai_firmware_revs)
#define NAI_IOC_COMMON_GET_HPS_READY        _IOR (NAI_COMMON_MAGIC,  6, __u32)
#define NAI_IOC_COMMON_SET_HPS_READY        _IOW (NAI_COMMON_MAGIC,  7, __u32)
#define NAI_IOC_COMMON_GET_MODULE_ADDR      _IOWR(NAI_COMMON_MAGIC,  8, nai_module_addr)
#define NAI_IOC_COMMON_SET_MODULE_ADDR      _IOWR(NAI_COMMON_MAGIC,  9, nai_module_addr)
#define NAI_IOC_COMMON_GET_ALL_MODULES_MASK _IOR (NAI_COMMON_MAGIC, 10, __u32)
#define NAI_IOC_COMMON_SET_ALL_MODULES_MASK _IOW (NAI_COMMON_MAGIC, 11, __u32)


#ifdef __cplusplus
}
#endif

#endif /* __NAI_MB_FPGA_TOP_CFG_ */

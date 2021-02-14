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
#include <linux/string.h>
#define pr_fmt(fmt) "%s:%s:%d::" fmt, strrchr(__FILE__,'/'), __func__, __LINE__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/rwsem.h>
#include <asm/device.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/miscdevice.h>
#include <linux/mod_devicetable.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/of_address.h>

#include "nai_serdes_prv.h"
#include "nai_serdes_utils.h"
#include "nai_serdes_oper.h"
#include "nai_serdes_config.h"
#include "nai_serdes.h"
#include "nai_KerModRev.h"

//#define DEBUG_IT

#define NAI_MISC_DEV_NAME	            "nai_serdes"
#define NAI_DRV_NAME_VER                    "NAI,mb-serdes-1.0"

#define NAI_MODULE_TX_FIFO_EMPTY_OFFSET     0x0040
#define NAI_MODULE_RX_FIFO_EMPTY_OFFSET     0x00C0


#define NAI_MODULE_DETECTED_OFFSET      	0x0100
#define NAI_MODULE_DETECTED_DONE_SHIFT  	8

#define NAI_MODULE_LINK_INIT_OFFSET     	0x0104
#define NAI_MODULE_RESET_OFFSET         	0x0108
#define NAI_MODULE_POWER_OFFSET        		0x010C
#define NAI_MODULE_CLK_OFFSET        		0x0114
#define NAI_MODULE_HSS_OFFSET        		0x011C
#define NAI_MODULE_DLL_OFFSET        		0x0120
#define NAI_MODULE_CONFIG_MODE_OFFSET       0x0124

//Module common module mode ready state offset
#define NAI_MODULE_COMMON_READY_OFFSET      				0x0000025C

//Module Configuration Mode States
#define NAI_MODULE_COMMON_CONFIGSTATE_REQUEST_ACK_BIT       0x00000001
#define NAI_MODULE_COMMON_CONFIGSTATE_READY_BIT             0x00008000
 
//Module Operation Mode States
#define NAI_MODULE_COMMON_OPERSTATE_BM_ENTERED_BIT              0x00010000
#define NAI_MODULE_COMMON_OPERSTATE_BM_COMMONPOPULATED_BIT  	0x00020000
#define NAI_MODULE_COMMON_OPERSTATE_BM_PARAM_LOADED_BIT         0x00040000
#define NAI_MODULE_COMMON_OPERSTATE_BM_CALIB_LOADED_BIT         0x00080000
#define NAI_MODULE_COMMON_OPERSTATE_BM_READY_BIT                0x80000000

/* Module Configuration  */
#define NAI_CLR_MODULE_CONFIG     	0
#define NAI_SET_MODULE_CONFIG     	1
#define NAI_MODULE_RESET_DELAY_MS     	100
#define NAI_MODULE_RESET_DELAY_US		1000
#define NAI_MAX_MODULE_BIT_MASK	0x3f	//6 slots
//module link and module detect timout
//1 seconds
#define NAI_MODULE_DETECT_COMPLETION_TIMEOUT                         (1 * HZ)

struct nai_serdes_dev {
	struct miscdevice   miscdev;
	struct rw_semaphore rwsem;
	s32                 initialized;
	s32                 addressed;
	void __iomem        *swserdes_common_base;
	void __iomem        *hwserdes_common_base;
	void __iomem		*modpktconfig_base;
};

int nai_serdes_open(struct inode *inode, struct file *filp)
{
  /* Open must be implemented to force the miscdevice open
   * function to store dev pointer in the file private data.
   */
#ifdef DEBUG_IT
   pr_info("file name:\"%s\",minor:%d\n",filp->f_path.dentry->d_iname,iminor(filp->f_path.dentry->d_inode));
#endif
   return 0;
}

int nai_serdes_release(struct inode *inode, struct file *filp)
{
#ifdef DEBUG_IT
   pr_info("file name:\"%s\",minor:%d\n",filp->f_path.dentry->d_iname,iminor(filp->f_path.dentry->d_inode));
#endif
   return 0;
}

static long set_slot(struct nai_serdes_dev *dev, nai_serdes_slot *arg) {
	long ret = 0;
	s32 rc = NAI__SUCCESS;
	s32 slot;

	down_write(&dev->rwsem);

	(void)get_user(slot, &arg->slot);

	if(((slot >= NAI_MB_SLOT) && (slot <= NAI_MODULE_6_SLOT)) ||
	   (slot == NAI_PPC_MB_SLOT)) {
		rc = nai_init_msg_utils((u8)slot);

		if (rc == NAI__SUCCESS)
			dev->initialized = 1;
		else {
			dev->initialized = 0;
			ret = -EIO;
		}
	} else {
		rc = NAI_INVALID_SLOT_ID;
		ret = -EINVAL;
	}	

	(void)put_user(rc, &arg->rc);

	up_write(&dev->rwsem);

	dev_dbg(dev->miscdev.this_device, "%s: slot=%d ret=%ld rc=%d\n",
		__func__, slot, ret, rc);

	return ret;
}

static long init_addr(struct nai_serdes_dev *dev, nai_serdes_addr *arg) {
	long ret = 0;	
	s32 rc = NAI__SUCCESS;
	const u8 MODULE_ADDR_ARRAY_COUNT = 8;
	u32 uModAddresses[MODULE_ADDR_ARRAY_COUNT];
	
	down_write(&dev->rwsem);

	if (!dev->initialized) {
		ret = -EACCES;
		goto exit;
	}

	ret = copy_from_user(&uModAddresses, (void __user*)&arg->addr, sizeof(uModAddresses));
	if (ret != 0)
	{
		rc = NAI_USER_COPY_FAILED;
		goto exit;
	}
	
	rc = nai_perform_init_slot_addressing(uModAddresses, MODULE_ADDR_ARRAY_COUNT);

	if (rc == NAI__SUCCESS)
		dev->addressed = 1;
	else {
		dev->addressed = 0;
		ret = -EIO;
	}
exit:
	(void)put_user(rc, &arg->rc);

	up_write(&dev->rwsem);

	dev_dbg(dev->miscdev.this_device, "%s: ret=%ld rc=%d\n",
		__func__, ret, rc);

	return ret;
}

static long rdwr_16(struct nai_serdes_dev *dev, nai_serdes_16 *arg, u32 rd) {
	long ret;
	s32 rc;
	u32 addr;
	s32 slot;
	u16 val;

	(void)get_user(slot, &arg->slot);
	(void)get_user(addr, &arg->addr);

	if (!dev->initialized || ((slot == -1) && !dev->addressed)) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

	if (rd) {
		if (slot == -1)
			rc = nai_read_reg16_request(addr, &val);
		else
			rc = nai_read_reg16_by_slot_request(slot, addr, &val);

		if (rc == NAI__SUCCESS)
			(void)put_user(val, &arg->val);
	}
	else {
		(void)(get_user(val, &arg->val));

		if (slot == -1)
			rc = nai_write_reg16_request(addr, val);
		else
			rc = nai_write_reg16_by_slot_request(slot, addr, val);
	}

	ret = (rc == NAI__SUCCESS) ? 0 : -EIO;
exit:
	(void)put_user(rc, &arg->rc);

	dev_dbg(dev->miscdev.this_device,
		"%s: slot=%d rd=%d addr=0x%08X val=0x%04X rc=%d\n",
		__func__, slot, rd, addr, val, rc);

	return ret;
}

static long rdwr_32(struct nai_serdes_dev *dev, nai_serdes_32 *arg, u32 rd) {
	long ret = 0;
	s32 rc;
	u32 addr;
	s32 slot;
	u32 val;

	(void)get_user(slot, &arg->slot);
	(void)get_user(addr, &arg->addr);

	if (!dev->initialized || ((slot == -1) && !dev->addressed)) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

	if (rd) { //rd op
		if (slot == -1)
			rc = nai_read_reg32_request(addr, &val);
		else
			rc = nai_read_reg32_by_slot_request(slot, addr, &val);
#ifdef DEBUG_IT
        pr_info("rd:%d,slot:%d,addr:0x%x,val:0x%x\n",rd,slot,addr,val);
#endif
		if (rc == NAI__SUCCESS)
			(void)put_user(val, &arg->val);
	}
	else { //wr op
		(void)(get_user(val, &arg->val));

		if (slot == -1)
			rc = nai_write_reg32_request(addr, val);
		else
			rc = nai_write_reg32_by_slot_request(slot, addr, val);
	}

	ret = (rc == NAI__SUCCESS) ? 0 : -EIO;
exit:
	(void)put_user(rc, &arg->rc);

	dev_dbg(dev->miscdev.this_device,
		"%s: slot=%d rd=%d addr=0x%08X val=0x%08X rc=%d\n",
		__func__, slot, rd, addr, val, rc);

	return ret;
}

static long rdwr_blk_16(struct nai_serdes_dev *dev, nai_serdes_blk_16 *arg,
			u32 rd) {
	long ret = 0;
	s32 rc;
	u32 addr;
	s32 slot;
	u16 count;
	u8 stride;
	u32 len;
	void __user *user_val;
	u16 *val = NULL;

	(void)get_user(slot, &arg->slot);
	(void)get_user(addr, &arg->addr);
	(void)get_user(count, &arg->count);
	(void)get_user(stride, &arg->stride);
	(void)get_user(user_val, &arg->val);

	if (!dev->initialized || ((slot == -1) && !dev->addressed)) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

	len = sizeof(*val) * count;
	if (rd)
		ret = !access_ok(VERIFY_WRITE, user_val, len);
	else
		ret = !access_ok(VERIFY_READ, user_val, len);
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	val = kmalloc(len, GFP_KERNEL);
	if (!val) {
		rc = NAI__SUCCESS;
		ret = -ENOMEM;
		goto exit;
	}

	if (rd) {
		if (slot == -1)
			rc = nai_read_block16_request(addr, (u32)count,
						      stride, val);
		else
			rc = nai_read_block16_by_slot_request(slot, addr, (u32)count,
							      stride, val);

		if (rc == NAI__SUCCESS)
			ret = copy_to_user(user_val, val, len);
	}
	else {
		ret = copy_from_user(val, user_val, len);
		
		if (ret == 0) {
			if (slot == -1)
				rc = nai_write_block16_request(addr, (u32)count,
								   stride, val);
			else
				rc = nai_write_block16_by_slot_request(slot, addr, (u32)count,
									   stride, val);
		}
	}

	if (ret)
		rc = NAI_USER_COPY_FAILED;
		
exit:
	
	(void)put_user(rc, &arg->rc);

	dev_dbg(dev->miscdev.this_device,
		"%s: slot=%d rd=%d addr=0x%08X count=%d stride=%d rc=%d\n",
		__func__, slot, rd, addr, count, stride, rc);
#ifdef DEBUG
	if (val) {
		for (len = 0; len < count; ++len)
			dev_dbg(dev->miscdev.this_device, "0x%04X\n", val[len]);
	}
#endif
	if (val)
		kfree(val);

	return ret;
}



static long rdwr_blk_16_large(struct nai_serdes_dev *dev, nai_serdes_blk_16_large *arg,
			u32 rd) {
	long ret = 0;
	s32 rc;
	u32 addr;
	s32 slot;
	u32 count;
	u8 stride;
	u32 len;
	void __user *user_val;
	u16 *val = NULL;

	(void)get_user(slot, &arg->slot);
	(void)get_user(addr, &arg->addr);
	(void)get_user(count, &arg->count);
	(void)get_user(stride, &arg->stride);
	(void)get_user(user_val, &arg->val);

   if (!dev->initialized || ((slot == -1) && !dev->addressed)) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}
   
   
       len = sizeof(*val) * count;
       if (rd)
		 ret = !access_ok(VERIFY_WRITE, user_val, len);
         else
		 ret = !access_ok(VERIFY_READ, user_val, len);
       if (ret) {
		   ret = -EFAULT;
		 goto exit;
	    }
       val = kmalloc(len, GFP_KERNEL);
       if (!val) {
		   rc = NAI__SUCCESS;
		 ret = -ENOMEM;
		   goto exit;
	    }
       
       if (rd) {
		 if (slot == -1)
			rc = nai_read_block16_request(addr, count,
						      stride, val);
		 else
			rc = nai_read_block16_by_slot_request(slot, addr, count,
							      stride, val);

		if (rc == NAI__SUCCESS)
			ret = copy_to_user(user_val, val, len);
	   }
      else
      {
         ret = copy_from_user(val, user_val, len);
         if (ret == 0) {
            if (slot == -1) {
               rc = nai_write_block16_request(addr, count,
                              stride, val);
            } else {
              rc = nai_write_block16_by_slot_request(slot, addr, count,
                                 stride, val);
           }
        }   
      }
   

	if (ret)
		rc = NAI_USER_COPY_FAILED;
		
exit:
	
	(void)put_user(rc, &arg->rc);
    
	dev_dbg(dev->miscdev.this_device,
		"%s: slot=%d rd=%d addr=0x%08X count=%d stride=%d rc=%d\n",
		__func__, slot, rd, addr, count, stride, rc);
#ifdef DEBUG
	if (val) {
		for (len = 0; len < count; ++len)
			dev_dbg(dev->miscdev.this_device, "0x%04X\n", val[len]);
	}
#endif
	if (val)
		kfree(val);

	return ret;
}

static long rdwr_blk_32(struct nai_serdes_dev *dev, nai_serdes_blk_32 *arg,
			u32 rd) {
	long ret = 0;
	s32 rc;
	u32 addr;
	s32 slot;
	u16 count;
	u8 stride;
	u32 len;
	void __user *user_val;
	u32 *val = NULL;

	(void)get_user(slot, &arg->slot);
	(void)get_user(addr, &arg->addr);
	(void)get_user(count, &arg->count);
	(void)get_user(stride, &arg->stride);
	(void)get_user(user_val, &arg->val);

	if (!dev->initialized || ((slot == -1) && !dev->addressed)) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

	len = sizeof(*val) * count;
	if (rd)
		ret = !access_ok(VERIFY_WRITE, user_val, len);
	else
		ret = !access_ok(VERIFY_READ, user_val, len);
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	val = kmalloc(len, GFP_KERNEL);
	if (!val) {
		rc = NAI__SUCCESS;
		ret = -ENOMEM;
		goto exit;
	}

	if (rd) {
		if (slot == -1)
			rc = nai_read_block32_request(addr, (u32)count,
						      stride, val);
		else
			rc = nai_read_block32_by_slot_request(slot, addr, (u32)count,
							      stride, val);

		if (rc == NAI__SUCCESS)
			ret = copy_to_user(user_val, val, len);
	} else {
		ret = copy_from_user(val, user_val, len);
		
		if (ret == 0) {
			if (slot == -1) {
				rc = nai_write_block32_request(addr, (u32)count,
								   stride, val);
			} else {
				rc = nai_write_block32_by_slot_request(slot, addr, (u32)count,
									   stride, val);
			}
		}
	}

	if (ret)
		rc = NAI_USER_COPY_FAILED;
		
exit:
        (void)put_user(rc, &arg->rc);

	dev_dbg(dev->miscdev.this_device,
		"%s: slot=%d rd=%d addr=0x%08X count=%d stride=%d rc=%d\n",
		__func__, slot, rd, addr, count, stride, rc);
#ifdef DEBUG
	if (val) {
		for (len = 0; len < count; ++len)
			dev_dbg(dev->miscdev.this_device, "0x%08X\n", val[len]);
	}
#endif
        if (val)
	        kfree(val);

        return ret;
}

static long rdwr_blk_32_large(struct nai_serdes_dev *dev, nai_serdes_blk_32_large *arg,
			u32 rd) {
	long ret = 0;
	s32 rc;
	u32 addr;
	s32 slot;
	u32 count;
	u8 stride;
	u32 len;
   u32 remainLen;
	void __user *user_val;
	u32 *val = NULL;
   u32 idx=0;
   u32 maxWrSegCnt=0;
   u32 remainingWrCnt=0;

	(void)get_user(slot, &arg->slot);
	(void)get_user(addr, &arg->addr);
	(void)get_user(count, &arg->count);
	(void)get_user(stride, &arg->stride);
	(void)get_user(user_val, &arg->val);

	if (!dev->initialized || ((slot == -1) && !dev->addressed)) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

   if( count > NAI_MAX_COUNT_PER_WRITE)
   {
       maxWrSegCnt=count/NAI_MAX_COUNT_PER_WRITE;
       remainingWrCnt=count%NAI_MAX_COUNT_PER_WRITE; 
       len = sizeof(*val) * NAI_MAX_COUNT_PER_WRITE;
       
       val = kmalloc(len, GFP_KERNEL);
       if (!val) {
		   rc = NAI__SUCCESS;
		   ret = -ENOMEM;
		   goto exit;
	    }
       
       for(idx=0; idx < maxWrSegCnt; idx++)
       {
          if (rd)
          ret = !access_ok(VERIFY_WRITE, user_val + idx * len, len);
            else
          ret = !access_ok(VERIFY_READ, user_val + idx * len, len);
          if (ret) {
            ret = -EFAULT;
            goto exit;
          }
          
          if (rd) {
            if (slot == -1)
			       rc = nai_read_block32_request(addr, NAI_MAX_COUNT_PER_WRITE,
						         stride, val);
		      else
			       rc = nai_read_block32_by_slot_request(slot, addr, NAI_MAX_COUNT_PER_WRITE,
							      stride, val);

          if (rc == NAI__SUCCESS)
			   ret = copy_to_user(user_val + idx * len, val, len);
	       } else {
	       ret = copy_from_user(val, user_val + idx * len, len);
          if (ret == 0) {
             if (slot == -1) {
                rc = nai_write_block32_request(addr, NAI_MAX_COUNT_PER_WRITE,
                                 stride, val);
                } else {
                  rc = nai_write_block32_by_slot_request(slot, addr, NAI_MAX_COUNT_PER_WRITE,
                                 stride, val);
               }
            }
          }
       }
       if(remainingWrCnt)
       {
          remainLen = sizeof(*val) * remainingWrCnt;
          if (rd)  
          ret = !access_ok(VERIFY_WRITE, user_val + maxWrSegCnt * len, remainLen);
            else
          ret = !access_ok(VERIFY_READ, user_val + maxWrSegCnt * len, remainLen);
          if (ret) {
            ret = -EFAULT;
            goto exit;
          }
          
          if (rd) {
            if (slot == -1)
			       rc = nai_read_block32_request(addr, remainingWrCnt,
						         stride, val);
		      else
			       rc = nai_read_block32_by_slot_request(slot, addr, remainingWrCnt,
							      stride, val);

          if (rc == NAI__SUCCESS)
			   ret = copy_to_user(user_val + maxWrSegCnt * len, val, remainLen);
	       } else {
	       ret = copy_from_user(val, user_val + maxWrSegCnt * len, remainLen);
          if (ret == 0) {
             if (slot == -1) {
                rc = nai_write_block32_request(addr, remainingWrCnt,
                                 stride, val);
                } else {
                  rc = nai_write_block32_by_slot_request(slot, addr, remainingWrCnt,
                                    stride, val);
               }
            }
          }
       }     
   }
   else
   {
       len = sizeof(*val) * count;
       if (rd)
		 ret = !access_ok(VERIFY_WRITE, user_val, len);
         else
		 ret = !access_ok(VERIFY_READ, user_val, len);
       if (ret) {
		   ret = -EFAULT;
		 goto exit;
	    }
       val = kmalloc(len, GFP_KERNEL);
       if (!val) {
		   rc = NAI__SUCCESS;
		 ret = -ENOMEM;
		   goto exit;
	    }
       
       if (rd) {
		 if (slot == -1)
			rc = nai_read_block32_request(addr, count,
						      stride, val);
		else
			rc = nai_read_block32_by_slot_request(slot, addr, count,
							      stride, val);

		if (rc == NAI__SUCCESS)
			ret = copy_to_user(user_val, val, len);
	   }
      else
      {
         ret = copy_from_user(val, user_val, len);
         if (ret == 0) {
            if (slot == -1) {
               rc = nai_write_block32_request(addr, count,
                              stride, val);
            } else {
              rc = nai_write_block32_by_slot_request(slot, addr, count,
                                 stride, val);
           }
        }   
      }
   }
	if (ret)
		rc = NAI_USER_COPY_FAILED;
		
exit:
        (void)put_user(rc, &arg->rc);

	dev_dbg(dev->miscdev.this_device,
		"%s: slot=%d rd=%d addr=0x%08X count=%d stride=%d rc=%d\n",
		__func__, slot, rd, addr, count, stride, rc);
#ifdef DEBUG
	if (val) {
		for (len = 0; len < count; ++len)
			dev_dbg(dev->miscdev.this_device, "0x%08X\n", val[len]);
	}
#endif
        if (val)
	        kfree(val);

        return ret;
}

static long rdwr_module_eeprom(struct nai_serdes_dev *dev, nai_serdes_module_eeprom *arg, u32 rd) {
	
	long ret = 0;
	s32 rc;
	u16 chipId;
	u8 reqId;
	u8 compId;
	u32 eepromOffset;
	s32 len;
	s32 buffSize;
	
	void __user *user_buff;
	u8 *buf = NULL;
	
#ifdef DEBUG	
	u32 index = 0;
#endif

	(void)get_user(chipId, &arg->chipId); //function:0x81,interface:0x80,
	(void)get_user(reqId, &arg->reqId);   //always 0
	(void)get_user(compId, &arg->compId); //module 1/2
	(void)get_user(eepromOffset, &arg->eepromOffset);
	(void)get_user(user_buff, &arg->buff);
	(void)get_user(len, &arg->len);

	if (!dev->initialized || (compId == -1)) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

	buffSize = sizeof(*buf) * len;
	if (rd)
		ret = !access_ok(VERIFY_WRITE, user_buff, buffSize);
	else
		ret = !access_ok(VERIFY_READ, user_buff, buffSize);
	
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	buf = kmalloc(buffSize, GFP_KERNEL);
	if (!buf) {
		rc = NAI_UNABLE_TO_ALLOCATE_MEMORY;
		ret = -ENOMEM;
		goto exit;
	}

	if (rd) {
		rc = nai_read_module_eeprom_request(chipId, reqId, compId, eepromOffset, buf, len);
		if (rc == NAI__SUCCESS)
			ret = copy_to_user(user_buff, buf, buffSize);
	} else {
		ret = copy_from_user(buf, user_buff, buffSize);
		if (ret == 0)
			rc = nai_write_module_eeprom_request(chipId, reqId, compId, eepromOffset, buf, len);
	}
	
	if (ret)
		rc = NAI_USER_COPY_FAILED;
		
exit:
	(void)put_user(rc, &arg->rc);
#ifdef DEBUG_IT
        pr_info("dev=%s chipId=%d rd=%d reqId=0x%x compId=0x%x eepromOffset=0x%x len=%d rc=%d \n",dev_name(dev->miscdev.this_device), chipId, rd, reqId, compId, eepromOffset, len, rc);
#endif
	dev_dbg(dev->miscdev.this_device,
		"%s: chipId=%d rd=%d reqId=0x%x compId=0x%x eepromOffset=0x%x len=%d rc=%d \n",
		__func__, chipId, rd, reqId, compId, eepromOffset, len, rc);
	
#ifdef DEBUG	
	if (buf) {
		for (index = 0; index < len; ++index)
			dev_dbg(dev->miscdev.this_device, "0x%08x\n", buf[index]);
	}
#endif
        if (buf)
	        kfree(buf);
	        
        return ret;
}

static long rdwr_module_flash(struct nai_serdes_dev *dev, nai_serdes_module_flash *arg, u8 op) {
	
	u8 reqId;
	u8 compId;
	u32 flashOffset;
	u8 numPage;
	s32 len;
	
	long ret = 0;
	s32 rc;
	s32 buffSize;
	
	void __user *user_buff;
	u8 *buf = NULL;
#ifdef DEBUG	
	u32 index = 0;
#endif

	(void)get_user(reqId, &arg->reqId);
	(void)get_user(compId, &arg->compId);
	(void)get_user(flashOffset, &arg->flashOffset);
	(void)get_user(numPage, &arg->numPage);
	(void)get_user(user_buff, &arg->buff);
	(void)get_user(len, &arg->len);

	if (!dev->initialized || compId == -1) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

	if (op == NAI_MODULE_FLASH_ERASE) {
		rc = nai_erase_flash_request(reqId, compId, flashOffset, numPage);
	} else {
		
		buffSize = sizeof(*buf) * len;
		if (op == NAI_MODULE_FLASH_READ) {
			ret = !access_ok(VERIFY_WRITE, user_buff, buffSize);
		} else if (op ==  NAI_MODULE_FLASH_WRITE) {
			ret = !access_ok(VERIFY_READ, user_buff, buffSize);
		}
		if (ret) {
			ret = -EFAULT;
			goto exit;
		}

		buf = kmalloc(buffSize, GFP_KERNEL);
		if (!buf) {
			rc = NAI__SUCCESS;
			ret = -ENOMEM;
			goto exit;
		}

		if (op == NAI_MODULE_FLASH_READ) {

			rc = nai_read_module_flash_request(reqId, compId, flashOffset, buf, len);
			if (rc == NAI__SUCCESS)
				ret = copy_to_user(user_buff, buf, buffSize);

		} else if ( op ==  NAI_MODULE_FLASH_WRITE ) {
			
			ret = copy_from_user(buf, user_buff, buffSize);
			if (ret == 0)
				rc = nai_write_module_flash_request(reqId, compId, flashOffset, buf, len);
				
		} else {
			rc = nai_erase_flash_request(reqId, compId, flashOffset ,numPage);
		}
	}
	
	if (ret)
		rc = NAI_USER_COPY_FAILED;
		
exit:
	(void)put_user(rc, &arg->rc);

	dev_dbg(dev->miscdev.this_device,
		"%s: op=%d reqId=0x%08X compId=0x%x flashOffset=0x%x len=%d rc=%d\n",
		__func__, op, reqId, compId, flashOffset, len, rc);
#ifdef DEBUG
	if (buf) {
		for (index = 0; index < len; ++index)
			dev_dbg(dev->miscdev.this_device, "0x%08x\n", buf[index]);
	}
#endif
        if (buf)
	        kfree(buf);
#ifdef DEBUG_IT
        pr_info("NAI_IOC_SERDES_ERASE_MODULE_FLASH:0x%x,ret:%d\n",NAI_IOC_SERDES_ERASE_MODULE_FLASH,ret);
#endif
        return ret;
}

static long rdwr_module_micro(struct nai_serdes_dev *dev, nai_serdes_module_micro *arg, u8 op) {
	u8 reqId;
	u8 compId;
	u8 channel;
	u32 flashOffset;	
	s32 len;
	
	long ret = 0;
	s32 rc;
	s32 buffSize;
	
	void __user *user_buff;
	u8 *buf = NULL;
#ifdef DEBUG	
	u32 index = 0;
#endif

	(void)get_user(reqId, &arg->reqId);
	(void)get_user(compId, &arg->compId);
	(void)get_user(channel, &arg->channel);
	(void)get_user(flashOffset, &arg->flashOffset);	
	(void)get_user(user_buff, &arg->buff);
	(void)get_user(len, &arg->len);

	if (!dev->initialized || compId == -1) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

	if (op == NAI_MODULE_MICRO_ERASE) {
		rc = nai_erase_micro_request(reqId, compId, channel);
	} else {
		
		buffSize = sizeof(*buf) * len;
		if (op == NAI_MODULE_MICRO_GET) {
			ret = !access_ok(VERIFY_WRITE, user_buff, buffSize);
		} else if (op ==  NAI_MODULE_MICRO_WRITE) {
			ret = !access_ok(VERIFY_READ, user_buff, buffSize);
		}
		if (ret) {
			ret = -EFAULT;
			goto exit;
		}

		buf = kmalloc(buffSize, GFP_KERNEL);
		if (!buf) {
			rc = NAI__SUCCESS;
			ret = -ENOMEM;
			goto exit;
		}

		if (op == NAI_MODULE_MICRO_GET) {

			rc = nai_get_micro_request(reqId, compId, channel, buf, len);
			if (rc == NAI__SUCCESS)
				ret = copy_to_user(user_buff, buf, buffSize);

		} else if ( op ==  NAI_MODULE_MICRO_WRITE ) {
			ret = copy_from_user(buf, user_buff, buffSize);
			if (ret == 0) 
				rc = nai_write_micro_request(reqId, compId, channel, flashOffset, buf, len);							
		} else {
			rc = nai_erase_micro_request(reqId, compId, channel);
		}
	}
	
	if (ret)
		rc = NAI_USER_COPY_FAILED;
		
exit:
	(void)put_user(rc, &arg->rc);

	dev_dbg(dev->miscdev.this_device,
		"%s: op=%d reqId=0x%08X compId=0x%x flashOffset=0x%x len=%d rc=%d\n",
		__func__, op, reqId, compId, flashOffset, len, rc);
#ifdef DEBUG
	if (buf) {
		for (index = 0; index < len; ++index)
			dev_dbg(dev->miscdev.this_device, "0x%08x\n", buf[index]);
	}
#endif
        if (buf)
	        kfree(buf);
	        
        return ret;	
}

static long get_module_mode_ready_state(struct nai_serdes_dev *dev, nai_module_op *arg, u32 rd) {
	
	long ret = 0;
	u32 val = 0;
	u32 slot = 0;
	u32 rc = 0;

	(void)get_user(slot, &arg->slot);

	if ( !dev->initialized && 
		((slot < NAI_MODULE_1_SLOT) || (slot > NAI_MODULE_6_SLOT)) ) {
		rc = NAI_SYSTEM_NOT_READY;
		ret = -EPERM;
		goto exit;
	}

	rc = nai_read_reg32_by_slot_request(slot, NAI_MODULE_COMMON_READY_OFFSET, &val);
	if (rc == NAI__SUCCESS) {
		switch(rd) {
		
			case NAI_MODULE_COMMON_CONFIGSTATE_REQUEST_ACK_BIT:
				val = (val & NAI_MODULE_COMMON_CONFIGSTATE_REQUEST_ACK_BIT) ? 1 : 0;
				break;
			
			case NAI_MODULE_COMMON_CONFIGSTATE_READY_BIT:
				val = (val & NAI_MODULE_COMMON_CONFIGSTATE_READY_BIT) ? 1 : 0;
				break;
			
			case NAI_MODULE_COMMON_OPERSTATE_BM_ENTERED_BIT:
				val = (val & NAI_MODULE_COMMON_OPERSTATE_BM_ENTERED_BIT) ? 1 : 0;
				break;
			
			case NAI_MODULE_COMMON_OPERSTATE_BM_COMMONPOPULATED_BIT:
				val = (val & NAI_MODULE_COMMON_OPERSTATE_BM_COMMONPOPULATED_BIT) ? 1 : 0;
				break;
			
			case NAI_MODULE_COMMON_OPERSTATE_BM_PARAM_LOADED_BIT:
				val = (val & NAI_MODULE_COMMON_OPERSTATE_BM_PARAM_LOADED_BIT) ? 1 : 0;
				break;
			
			case NAI_MODULE_COMMON_OPERSTATE_BM_CALIB_LOADED_BIT:
				val = (val & NAI_MODULE_COMMON_OPERSTATE_BM_CALIB_LOADED_BIT) ? 1 : 0;
				break;
			
			case NAI_MODULE_COMMON_OPERSTATE_BM_READY_BIT:
				val = (val & NAI_MODULE_COMMON_OPERSTATE_BM_READY_BIT) ? 1 : 0;
				break;
			
			default:
				val = 0;
				rc = NAI_INVALID_PARAMETER_VALUE;
			break;
		}
		(void)put_user(val, &arg->enable);
	}	

	ret = (rc == NAI__SUCCESS) ? 0 : -EIO;
	
exit:
	(void)put_user(rc, &arg->rc);
	(void)put_user(val, &arg->enable);
	return ret;
}

static long get_module_detected(struct nai_serdes_dev *dev,
				nai_module_op __user *arg) {
	long ret = 0;
	u32 val = 0;
	u32 slot = 0;
	u32 done = 0;
	u32 rc = 0;

	(void)get_user(slot, &arg->slot);

	if ( (slot >= NAI_MODULE_1_SLOT) && (slot <= NAI_MODULE_6_SLOT) ) {
		slot = slot - 1;
		val = ioread32(dev->swserdes_common_base + NAI_MODULE_DETECTED_OFFSET);
		/*
		 * upper byte is module detect done
		 * lower byte is module ready
		*/
		done = val >> NAI_MODULE_DETECTED_DONE_SHIFT;
		
		//return 1 to user app if both module detect done and module ready are set to 1
		if (done & (1 << slot)) {
			val = (val & (1 << slot)) ? 1 : 0;
		} else {
			rc = ret = -EAGAIN;
		}
	} else {
		rc = ret = -EINVAL;
	}
	
	(void)put_user(val, &arg->enable);
	(void)put_user(rc, &arg->rc);
	return ret;
}

static long get_serdes_module_revision(nai_module_serdes_revision __user *arg) {
	long ret = 0;
	u8 major = NAI_DRV_SERDES_VER_MAJOR;
	u8 minor = NAI_DRV_SERDES_VER_MINOR;
	
	if((ret = put_user(major, &arg->serdesMajorRev)) == 0)
    {
      ret = put_user(minor, &arg->serdesMinorRev);
    }
	return ret;
}

static long get_module_link_init(struct nai_serdes_dev *dev,
				 nai_module_op __user *arg) {
	long ret = 0;
	u32 val = 0;
	u32 slot = 0;
	u32 rc = 0;

	
	(void)get_user(slot, &arg->slot);

	if ( (slot >= NAI_MODULE_1_SLOT) && (slot <= NAI_MODULE_6_SLOT) ) {
		
		slot = slot - 1;
		
		val = ioread32(dev->swserdes_common_base + NAI_MODULE_LINK_INIT_OFFSET);
		val = (val & (1 << slot)) ? 1 : 0;
		
		if (!val) {
			rc = ret = -EAGAIN;
		}
	} else {
		rc = ret = -EINVAL;
	}
	
	(void)put_user(val, &arg->enable);
	(void)put_user(rc, &arg->rc);
	return ret;
}

static long set_module_reset
(
   struct nai_serdes_dev *dev,
   nai_module_op __user *arg)
{
   long result = 0;
   u32 val = 0;
   u32 slot = 0;
   u32 rc = 0;
   u32 timer = 0;

   (void)get_user(slot, &arg->slot);

   if((slot >= NAI_MODULE_1_SLOT) && (slot <= NAI_MODULE_6_SLOT))
   {
      /* Module Reset and HSS is a bitmap register
       * Bit 0 = Module 1
       * Bit 1 = Module 2
       * Bit 2 = Module 3
       * Bit 3 = Module 4
       * Bit 4 = Module 5
       * Bit 5 = Module 6
       */
      
      slot = slot - 1;
      /* SoftReset module operation
       * 1. Held module HSS in reset
       * 2. wait 1ms (based on KF's module reset spec)
       * 3. Held module reset 
       * 4. wait 1ms (based on KF's module reset spec)
       * 5. Release module from reset
       * 6. wait for module detect done and module ready (timeout in 1seconds)
       * 7. take module hss out of reset 
       * 8. wait for link init done (timeout in 1seconds)
       */
      //1. Held module HSS in reset
      val = ioread32(dev->swserdes_common_base + NAI_MODULE_HSS_OFFSET);
      val |= (1 << slot);
      iowrite32(val, dev->swserdes_common_base + NAI_MODULE_HSS_OFFSET);
      // 2. wait 1ms (based on KF's module reset spec)
      udelay(NAI_MODULE_RESET_DELAY_US);
      // 3. Held module nReset
      val = ioread32(dev->swserdes_common_base + NAI_MODULE_RESET_OFFSET);
      val &= ~(1 << slot);
      iowrite32(val, dev->swserdes_common_base + NAI_MODULE_RESET_OFFSET);
      // 4. wait 1ms (based on KF's module reset spec)
      udelay(NAI_MODULE_RESET_DELAY_US);
      // 5. Release module from nReset
      val = ioread32(dev->swserdes_common_base + NAI_MODULE_RESET_OFFSET);
      val |= (1 << slot);
      iowrite32(val, dev->swserdes_common_base + NAI_MODULE_RESET_OFFSET);
      
      //6. wait for module detect done and module ready (timeout in 1seconds)
      timer = jiffies;
      while ( 0 != get_module_detected(dev, (nai_module_op __user *)arg) )
      {
         if (((s32)jiffies - (s32)timer) > NAI_MODULE_DETECT_COMPLETION_TIMEOUT)
         {
            rc = NAI_MODULE_DETECT_READY_TIMEOUT;
            result =  -EAGAIN;
            pr_err("slot:%d,NAI_MODULE_DETECT_READY_TIMEOUT:%d,result:%d\n",slot,NAI_MODULE_DETECT_READY_TIMEOUT,result);
            break;
         }
         schedule();
      }
      
      // 7. Release module HSS from reset
      val = ioread32(dev->swserdes_common_base + NAI_MODULE_HSS_OFFSET);
      val &= ~(1 << slot);
      //TODO: A bug in MB FPGA that we can't use bitmap to take each module out of HSS reset
      //Temp WR for the above bug: take all module out of HSS reset by clear all bits in the HSS register
      val = 0x0;
      iowrite32(val, dev->swserdes_common_base + NAI_MODULE_HSS_OFFSET);
      
      //8. wait for link init done (timeout in 1seconds)
      timer = jiffies;
      while (0 != get_module_link_init(dev, (nai_module_op __user *)arg))
      {   
         if (((s32)jiffies - (s32)timer) > NAI_MODULE_DETECT_COMPLETION_TIMEOUT)
         {
            rc = NAI_MODULE_LINK_DETECT_TIMEOUT;
            result =  -EAGAIN;
            pr_err("slot:%d,NAI_MODULE_LINK_DETECT_TIMEOUT:%d,result:%d\n",slot,NAI_MODULE_LINK_DETECT_TIMEOUT,result);
            break;
         }
         schedule();
      }
   }
   else
   {
      rc = result = -EINVAL;
      pr_err("slot:%d,result:%d\n",slot,result);
   }

   (void)put_user(rc, &arg->rc);
#ifdef DEBUG_IT
   pr_info("slot:%d,rc:%d\n",slot,rc);
#endif
   return result;
}

static long kill_module_serdes(struct nai_serdes_dev *dev,
			     nai_module_op __user *arg) {
	long ret = 0;
	u32 val = 0;
	u32 slot = 0;
	u32 rc = 0;

	(void)get_user(slot, &arg->slot);
	
	if ( (slot >= NAI_MODULE_1_SLOT) && (slot <= NAI_MODULE_6_SLOT) ) {
		/* Module Power is a bitmap register
		 * Bit 0 = Module 1
		 * Bit 1 = Module 2
		 * Bit 2 = Module 3
		 * Bit 3 = Module 4
		 * Bit 4 = Module 5
		 * Bit 5 = Module 6
		 */
		
		slot = slot - 1;
				
		// 1. Kill module serdes for desired module
		val = ioread32(dev->swserdes_common_base + NAI_MODULE_RESET_OFFSET);
		val &= ~(1 << slot);
		iowrite32(val, dev->swserdes_common_base + NAI_MODULE_RESET_OFFSET);
			
	} else {
		rc = ret = -EINVAL;
	}
	(void)put_user(rc, &arg->rc);
	return ret;
}

static long set_module_config(struct nai_serdes_dev *dev,
			      nai_module_op __user *arg, u32 op) {
	long ret = 0;
	u32 val = 0;
	u32 slot = 0;
	u32 rc = 0;

	(void)get_user(slot, &arg->slot);

	if ( (slot >= NAI_MODULE_1_SLOT) && (slot <= NAI_MODULE_6_SLOT) ) {
		
		/* Module Config mode is a bitmap register
		 * Bit 0 = Module 1
		 * Bit 1 = Module 2
		 * Bit 2 = Module 3
		 * Bit 3 = Module 4
		 * Bit 4 = Module 5
		 * Bit 5 = Module 6
		 */
		
		slot = slot - 1;
		
		val = ioread32(dev->swserdes_common_base + NAI_MODULE_CONFIG_MODE_OFFSET);
		val &= ~(1 << slot);
		
		if ( NAI_SET_MODULE_CONFIG == op )
			val |= (1 << slot);
		iowrite32(val, dev->swserdes_common_base + NAI_MODULE_CONFIG_MODE_OFFSET);

	} else {
		rc = ret = -EINVAL;
	}
	
	(void)put_user(rc, &arg->rc);
	return ret;
}

long nai_serdes_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
   struct nai_serdes_dev *dev;
   long ret = 0;
   u32 rd = 0;
   u8 op = 0;
#ifdef DEBUG_IT
   pr_info("file name:\"%s\",minor:%d,cmd:0x%x\n",filp->f_path.dentry->d_iname,iminor(filp->f_path.dentry->d_inode),cmd);
#endif
	dev = container_of(filp->private_data, struct nai_serdes_dev, miscdev);

	dev_dbg(dev->miscdev.this_device, "%s: type=%d dir=%d size=%d\n",
		__func__, _IOC_TYPE(cmd), _IOC_DIR(cmd), _IOC_SIZE(cmd));

	if (_IOC_TYPE(cmd) != NAI_SERDES_MAGIC) {
		ret = -ENOTTY;
		goto exit;
	}

	if (_IOC_DIR(cmd) & _IOC_READ)
		ret = !access_ok(VERIFY_WRITE, (void __user *)arg,
				 _IOC_SIZE(cmd));
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
		ret = !access_ok(VERIFY_READ, (void __user *)arg,
				 _IOC_SIZE(cmd));
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	switch (cmd) {
	case NAI_IOC_SERDES_SET_SLOTID:
		ret = set_slot(dev, (nai_serdes_slot *)arg);	
		break;

	case NAI_IOC_SERDES_INIT_ADDR:
		ret = init_addr(dev, (nai_serdes_addr *)arg);
		break;

	default:
		/* TODO should be a down_read but we want to serialize
		 * access to all modules
		 */
		down_write(&dev->rwsem);

		switch (cmd) {
		case NAI_IOC_SERDES_RD_REG16:
			rd = 1; /* Intentional fall-through */
		case NAI_IOC_SERDES_WR_REG16:
			ret = rdwr_16(dev, (nai_serdes_16 *)arg, rd);
			break;

		case NAI_IOC_SERDES_RD_REG32:
			rd = 1; /* Intentional fall-through */
		case NAI_IOC_SERDES_WR_REG32:
			ret = rdwr_32(dev, (nai_serdes_32 *)arg, rd);
			break;
		case NAI_IOC_SERDES_RD_BLK16:
			rd = 1; /* Intentional fall-through */
		case NAI_IOC_SERDES_WR_BLK16:
			ret = rdwr_blk_16(dev, (nai_serdes_blk_16 *)arg, rd);
			break;
      case NAI_IOC_SERDES_RD_BLK16_LARGE:
			rd = 1; /* Intentional fall-through */
		case NAI_IOC_SERDES_WR_BLK16_LARGE:
			ret = rdwr_blk_16_large(dev, (nai_serdes_blk_16_large *)arg, rd);
			break;
 		case NAI_IOC_SERDES_RD_BLK32:
			rd = 1; /* Intentional fall-through */
		case NAI_IOC_SERDES_WR_BLK32:
			ret = rdwr_blk_32(dev, (nai_serdes_blk_32 *)arg, rd);
			break;
      	case NAI_IOC_SERDES_RD_BLK32_LARGE:
			rd = 1; /* Intentional fall-through */
		case NAI_IOC_SERDES_WR_BLK32_LARGE:
			ret = rdwr_blk_32_large(dev, (nai_serdes_blk_32_large *)arg, rd);
			break;
		case NAI_IOC_SERDES_RD_MODULE_EEPROM:
#ifdef DEBUG_IT
                        pr_info("cmd:0x%x,NAI_IOC_SERDES_RD_MODULE_EEPROM:0x%x\n",cmd,NAI_IOC_SERDES_RD_MODULE_EEPROM);
#endif
			rd = 1; /* Intentional fall-through */
		case NAI_IOC_SERDES_WR_MODULE_EEPROM:
#ifdef DEBUG_IT
                        pr_info("cmd:0x%x,NAI_IOC_SERDES_WR_MODULE_EEPROM:0x%x\n",cmd,NAI_IOC_SERDES_WR_MODULE_EEPROM);
#endif
			ret = rdwr_module_eeprom(dev, (nai_serdes_module_eeprom *)arg, rd);
			break;
		case NAI_IOC_SERDES_RD_MODULE_FLASH:
			op = NAI_MODULE_FLASH_READ;
			ret = rdwr_module_flash(dev, (nai_serdes_module_flash *)arg, op);
			break;
		case NAI_IOC_SERDES_WR_MODULE_FLASH:
			op = NAI_MODULE_FLASH_WRITE;
			ret = rdwr_module_flash(dev, (nai_serdes_module_flash *)arg, op);
			break;
		case NAI_IOC_SERDES_ERASE_MODULE_FLASH:
			op = NAI_MODULE_FLASH_ERASE;
			ret = rdwr_module_flash(dev, (nai_serdes_module_flash *)arg, op);
#ifdef DEBUG_IT
                        pr_info("cmd:0x%x,NAI_IOC_SERDES_ERASE_MODULE_FLASH:0x%x,ret:%ld\n",cmd,NAI_IOC_SERDES_ERASE_MODULE_FLASH,ret);
#endif
			break;
		case NAI_IOC_GET_MODULE_DETECTED:
			ret = get_module_detected(dev, (nai_module_op __user *)arg);
			break;
		case NAI_IOC_GET_MODULE_LINK_INIT:
			ret = get_module_link_init(dev, (nai_module_op __user *)arg);
			break;
		case NAI_IOC_SET_MODULE_RESET:
			ret = set_module_reset(dev, (nai_module_op __user *)arg);
#ifdef DEBUG_IT
                        pr_info("cmd:0x%x,NAI_IOC_SET_MODULE_RESET:0x%x,ret:%ld\n",cmd,NAI_IOC_SET_MODULE_RESET,ret);
#endif
			break;
		case NAI_IOC_KILL_MODULE_SERDES:
			ret = kill_module_serdes(dev, (nai_module_op __user *)arg);
			break;
		case NAI_IOC_SET_MODULE_CONFIG_MODE:
			rd = NAI_SET_MODULE_CONFIG; /* Intentional fall-through */
		case NAI_IOC_CLR_MODULE_CONFIG_MODE:
			ret = set_module_config(dev, (nai_module_op __user *)arg, rd);
			break;
		case NAI_IOC_GET_MODULE_CONFIG_STATE_RQ_ACK:
			rd = NAI_MODULE_COMMON_CONFIGSTATE_REQUEST_ACK_BIT;
			ret = get_module_mode_ready_state(dev, (nai_module_op __user *)arg, rd);
			break;
		case NAI_IOC_GET_MODULE_CONFIG_STATE_RDY:
			rd = NAI_MODULE_COMMON_CONFIGSTATE_READY_BIT;
			ret = get_module_mode_ready_state(dev, (nai_module_op __user *)arg, rd);
			break;
		case NAI_IOC_GET_MODULE_OPER_BM_ENTERED:
			rd = NAI_MODULE_COMMON_OPERSTATE_BM_ENTERED_BIT;
			ret = get_module_mode_ready_state(dev, (nai_module_op __user *)arg, rd);
			break;
		case NAI_IOC_GET_MODULE_OPER_BM_COMMONPOPULATED:
			rd =  NAI_MODULE_COMMON_OPERSTATE_BM_COMMONPOPULATED_BIT;
			ret = get_module_mode_ready_state(dev, (nai_module_op __user *)arg, rd);
			break;
		case NAI_IOC_GET_MODULE_OPER_BM_PARAM_LOADED:
			rd = NAI_MODULE_COMMON_OPERSTATE_BM_PARAM_LOADED_BIT;
			ret = get_module_mode_ready_state(dev, (nai_module_op __user *)arg, rd);
			break;
		case NAI_IOC_GET_MODULE_OPER_BM_CALIB_LOADED:
			rd = NAI_MODULE_COMMON_OPERSTATE_BM_CALIB_LOADED_BIT;
			ret = get_module_mode_ready_state(dev, (nai_module_op __user *)arg, rd);
			break;
		case NAI_IOC_GET_MODULE_OPER_BM_RDY:    			
			rd = NAI_MODULE_COMMON_OPERSTATE_BM_READY_BIT;
			ret = get_module_mode_ready_state(dev, (nai_module_op __user *)arg, rd);
			break;
		case NAI_IOC_SERDES_WR_MICRO:    			
			op = NAI_MODULE_MICRO_WRITE;
			ret = rdwr_module_micro(dev, (nai_serdes_module_micro *)arg, op);
			break;
		case NAI_IOC_SERDES_GET_MICRO: 
			op = NAI_MODULE_MICRO_GET;
			ret = rdwr_module_micro(dev, (nai_serdes_module_micro *)arg, op);
			break; 		
		case NAI_IOC_SERDES_ERASE_MICRO: 
			op = NAI_MODULE_MICRO_ERASE;
			ret = rdwr_module_micro(dev, (nai_serdes_module_micro *)arg, op);
			break;
      case NAI_IOC_GET_MODULE_SERDES_REVISION: 
			ret = get_serdes_module_revision((nai_module_serdes_revision *)arg);
			break; 	   
		default:
			ret = -ENOTTY;
			break;
		}

		up_write(&dev->rwsem);
	}
exit:
	return ret;
}

static const struct file_operations nai_serdes_fops = {
	.open           = nai_serdes_open,
	.release        = nai_serdes_release,
	.unlocked_ioctl = nai_serdes_ioctl,
	.compat_ioctl   = nai_serdes_ioctl,
};

static const struct of_device_id of_nai_mb_serdes_match[] = {
	{ .compatible = NAI_DRV_NAME_VER, },
	{},
};

static int nai_serdes_probe(struct platform_device *pdev)
{
   int ret = 0;
   struct nai_serdes_dev *dev = NULL;   
   struct device_node *pNode, *cNode;
#ifdef DEBUG_IT
   pr_info("!!!!!!!!!!!!!!!!!!!!!!!!!\n");
#endif
   dev_info(&pdev->dev, "%s\n",__func__);
   
   /* Allocate and initialize device */
   dev = kmalloc(sizeof(*dev), GFP_KERNEL);
   if (!dev)
   {
      ret = -ENOMEM;
      goto exit;
   }
   memset(dev, 0, sizeof(*dev));

   /* Initialize sem */
   init_rwsem(&dev->rwsem);

   /* Initialize and register miscdevice */
   dev->miscdev.name = NAI_MISC_DEV_NAME;
   dev->miscdev.minor = MISC_DYNAMIC_MINOR;
   dev->miscdev.fops = &nai_serdes_fops;
   ret = misc_register((struct miscdevice *)dev);
   if (ret)
   {
      dev_err(&pdev->dev, "Failed to allocate misc device\n");
      goto err_misc;
   }
#ifdef DEBUG_IT
   else
   {
      pr_info("misc_register '%s' OK\n",NAI_MISC_DEV_NAME);
   }
#endif

   //get the parent node of the compatible device
   pNode = of_find_compatible_node(NULL, NULL, NAI_DRV_NAME_VER);
   if(!pNode) {
      dev_err(&pdev->dev, "Unable to find compatible %s in DTB \n", NAI_DRV_NAME_VER);
      goto err_find_comp_node;
   }
#ifdef DEBUG_IT
   else
   {
      pr_info("find compatible node '%s' OK\n",NAI_DRV_NAME_VER);
   }
#endif
   
   /* Map slow-access (i.e. software serdes) */
   cNode = of_get_child_by_name(pNode, "slow-access");
   if(cNode != NULL)
   {
      dev->swserdes_common_base = of_iomap(cNode,0);
      if (!dev->swserdes_common_base)
      {
         dev_err(&pdev->dev, "Failed to map software serdes registers\n");
         ret = -ENOMEM;
         goto err_swserdes_res;
      }
#ifdef DEBUG_IT
      else
      {
         pr_info("swserdes_common_base %p \n", dev->swserdes_common_base);
         dev_info(&pdev->dev, "swserdes_common_base %p \n", dev->swserdes_common_base);
      }
#endif
   }
   else
   {
      goto err_swserdes_res; /* everything (all modules) should be capabale of SW SERDES */
   }
   
   /* Map fast-access (i.e. hardware serdes) */
   cNode = of_get_child_by_name(pNode, "fast-access");
   if(cNode != NULL)
   {
      dev->hwserdes_common_base = of_iomap(cNode,0);
      if (!dev->hwserdes_common_base)
      {
         dev_err(&pdev->dev, "Failed to map hardware serdes registers\n");
         ret = -ENOMEM;
         goto err_hwserdes_res;
      }
#ifdef DEBUG_IT
      else
      {
         pr_info("hwserdes_common_base %p\n", dev->hwserdes_common_base);
         dev_info(&pdev->dev, "hwserdes_common_base %p\n", dev->hwserdes_common_base);
      }
#endif
   }
   else
   {
      dev->hwserdes_common_base = NULL;
   }   

   /* Map module-pkt-cfg (i.e. module configuration modules used for serdes packets) */
   cNode = of_get_child_by_name(pNode, "module-pkt-cfg");
   if(cNode != NULL)
   {
      dev->modpktconfig_base = of_iomap(cNode,0);
      if (!dev->modpktconfig_base)
      {
         dev_err(&pdev->dev, "Failed to map module serdes packet configuration registers\n");
         ret = -ENOMEM;
         goto err_modpktconfig_res;
      }
#ifdef DEBUG_IT
      else
      {
         pr_info("modpktconfig_base %p\n", dev->modpktconfig_base);
         dev_info(&pdev->dev, "modpktconfig_base %p\n", dev->modpktconfig_base);
      }
#endif
   }
   else
   {
      dev->modpktconfig_base = NULL;
   }      

   /* Set base address for util and oper code 
    * NOTE: Seperate base addresses are used for 16 and 32 bit access
    * 32 bit access uses hardware for forming serdes packets 
    * 16 bit still uses software for forming serdes packets */
   nai_set_virtual_base_sw(dev->swserdes_common_base);  /* 16 bit access will still use original software packetizing method */ 
   nai_set_virtual_base_hw(dev->hwserdes_common_base);  /* 32 bit access will now use new hardware packetizing method */   
   nai_set_virtual_base_module_pkt_config(dev->modpktconfig_base); /* Module Serdes Packet Configuration */
   
   /* Save misc device in platform dev */
   platform_set_drvdata(pdev, dev);
#ifdef DEBUG_IT
   pr_info("!!!!!!!!!!!!!!!!!!!!!!!!!\n");
#endif
   return 0;
err_swserdes_res:
   iounmap(dev->swserdes_common_base);   
err_hwserdes_res:
   iounmap(dev->hwserdes_common_base);      
err_modpktconfig_res:
   iounmap(dev->modpktconfig_base);
err_find_comp_node:
   misc_deregister((struct miscdevice *)dev);
err_misc:
   kfree(dev);
exit:
   return ret;
}

static int nai_serdes_remove(struct platform_device *pdev)
{
   struct nai_serdes_dev *dev = platform_get_drvdata(pdev);

   platform_set_drvdata(pdev, NULL);
   nai_set_virtual_base_hw(NULL);
   nai_set_virtual_base_sw(NULL);
   misc_deregister((struct miscdevice *)dev);
   kfree(dev);

   return 0;
}

static struct platform_driver nai_serdes_drv =
{
   .driver   = {
      .name  = NAI_DRV_NAME_VER,
      .owner = THIS_MODULE,
      .of_match_table = of_match_ptr(of_nai_mb_serdes_match),
   },
   .probe    = nai_serdes_probe,
   .remove   = nai_serdes_remove,
};

static int __init nai_serdes_init(void) {
   return platform_driver_register(&nai_serdes_drv);
}


static void __exit nai_serdes_exit(void) {
   platform_driver_unregister(&nai_serdes_drv);
}

module_init(nai_serdes_init);
module_exit(nai_serdes_exit);

MODULE_AUTHOR("Obi Okafor <ookafor@naii.com>");
MODULE_DESCRIPTION("NAI SERDES driver");
MODULE_LICENSE("GPL");

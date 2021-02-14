/* =========================================================================
 *
 * Copyright (c) 2013 North Atlantic Industries, Inc.  All Rights Reserved.
 *
 * Author: North Atlantic Industries, Inc.
 *
 * SubSystem: I2C Generic Device Driver to handle generic I2C I/O on Linux kernel 2.6.x
 *
 * FileName: nai_generic_i2c.c
 *
 * History:
 * 02-23-2017 JinH
 *   fixed -EBUSY(-16) error to drive the new NAI Power Supply in which the error -ENXIO(-6) was fixed.
 *
 * 11-29-2016 JinH first release
 * ==========================================================================*/

#include <linux/init.h>
#define pr_fmt(fmt) "NAI %s:%s:%d::" fmt, KBUILD_MODNAME, __func__, __LINE__
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/errno.h>
#if (KERNEL_VERSION(2,6,18) > LINUX_VERSION_CODE)
#include <linux/config.h>
#endif
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/cdev.h>
#include <asm/ioctl.h>
#include <asm/irq.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 3, 0)
#include <asm/switch_to.h>
#else
#include <asm/system.h>
#endif
#include <asm/uaccess.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/delay.h>

//#define DEBUG_IT

enum I2C_ADAPTERS
{
   I2C_INTEL_PERIPHERAL_ADAPTER,
   I2C_ZYNQ_PERIPHERAL_ADAPTER,
   I2C_ADAPTER_NUM
};

#define PROBE_NAME      "nai-gen-i2c"

#if defined(__x86_64__) || defined(__amd_64__)  || defined(__i386__)
#define I2C_DEVICE_FILE "i2c-adapter-0"
#elif defined(__arm__)
#define I2C_DEVICE_FILE "i2c-adapter-1"
#endif

#ifdef DEBUG_IT
static void printk_hex_info(const u8* hex_info,unsigned int info_size)
{
   #define COLUMN_NUM 16
   #define DELIMITER ','
   #define ELEMENT_SIZE 3  /*including 2 bytes hex num and 1 byte field delimiter ',' or '\n'*/
   char work_buf[ELEMENT_SIZE*COLUMN_NUM+sizeof(int)/*sizeof(int) is used for last str delimiter '\0' and mem alignment*/];
   unsigned int i;

   memset(work_buf,0,sizeof(work_buf));
   for(i=0;i<info_size;i++)
   {
      sprintf(work_buf+(i*ELEMENT_SIZE)%(COLUMN_NUM*ELEMENT_SIZE),"%02x%c",hex_info[i],(i==(info_size-1)||(i+1)%COLUMN_NUM==0)?'\n':DELIMITER);
      if(i==(info_size-1)||(i+1)%COLUMN_NUM==0)
      {
         printk(KERN_INFO"%s",work_buf);
         memset(work_buf,0,sizeof(work_buf));
      }
   }
}
#endif /* #ifdef DEBUG_IT */

#define MAX_I2C_X_BUF_LEN    32
#define RETRY_LIMIT 3

struct nai_generic_i2c_driver_data
{
   struct cdev c_dev;                   /*make the link from address of struct cdev to address of struct ps4i2c_driver_data*/
   struct class *generic_i2c_class;     /*device class*/
   dev_t first;                         /*for the first device number*/
   struct mutex mutex;
   struct i2c_adapter *i2c_adapter_ptr;
   u32    user_cnt;
};

enum NAI_I2C_RD_FMT_INDX
{
   /*I2C_ADAPTER_OFF,*/
   I2C_RD_ADDR_OFF,
   I2C_RD_ADDR_EXT_OFF,  /*in case for ten bit chip address*/
   I2C_RD_CMD_OFF,
   I2C_RD_RESPONSE_LEN_OFF,
   I2C_RD_ARGC_OFF,
   I2C_RD_ARGV_OFF,
 
   NAI_I2C_FMT_INDX_TOTAL
};

enum NAI_I2C_WR_FMT_INDX
{
   I2C_WR_ADDR_OFF,
   I2C_WR_ADDR_EXT_OFF,  /*in case for ten bit chip address*/
   I2C_WR_CMD_OFF,
   I2C_WR_ARGC_OFF,
   I2C_WR_ARGV_OFF,
 
   NAI_I2C_WR_FMT_INDX_TOTAL
};

enum I2C_REQ_INDX
{
   I2C_REQ_0_OFF,
   I2C_REQ_1_OFF,
   I2C_REQ_NUM
};

static s32 smbus_i2c_read_block
(
   struct nai_generic_i2c_driver_data* nai_generic_i2c_driver_data_ptr,
   const u8* in_buf,
   u16 in_buf_len,
   u8* out_buf,
   u16 out_buf_len
)
{
   struct i2c_client i2c_cli;
   s32 result = 0;
   int layer_1_retry_cnt;
   i2c_cli.adapter = nai_generic_i2c_driver_data_ptr->i2c_adapter_ptr;

   i2c_cli.flags = 0; /*bug fix for sending invalid I2C slave address*/

   /*little-endian*/
   i2c_cli.addr = in_buf[I2C_WR_ADDR_EXT_OFF];
   i2c_cli.addr <<= 8;
   i2c_cli.addr |= in_buf[I2C_WR_ADDR_OFF];

   if ( in_buf[ I2C_RD_ADDR_EXT_OFF ] & 0xFF )
   {
      i2c_cli.flags |= I2C_M_TEN;
   }
#ifdef DEBUG_IT
   pr_info("input:\n");printk_hex_info(in_buf,in_buf_len);
   pr_info("out_buf_len:%d\n",out_buf_len);printk_hex_info(out_buf,out_buf_len);
#endif
   if ( in_buf[I2C_RD_ARGC_OFF] == 0 )
   {
    /*
     * error codes are defined in:
     *Intel:
     *include/asm-generic/errno-base.h
     *include/asm-generic/errno.h
     *include/linux/errno.h
     *ARM:
     *include/uapi/linux/errno.h
     *include/uapi/asm-generic/errno-base.h
     *include/uapi/asm-generic/errno.h
     *include/linux/errno.h
     */

      for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )
      {
         result = i2c_smbus_read_i2c_block_data( &i2c_cli, in_buf[I2C_RD_CMD_OFF], in_buf[I2C_RD_RESPONSE_LEN_OFF], out_buf );
         if( result < 0 )
         {
            if ( result == -EBUSY )
            {
               pr_err("tried %d,i2c_smbus_read_i2c_block_data(0x%x,0x%02x,%d) err:%d\n",layer_1_retry_cnt+1,i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_RESPONSE_LEN_OFF],result);
               msleep( 1 );
            }
            else
            {
               pr_err("i2c_smbus_read_i2c_block_data(0x%x,0x%02x,%d) err:%d\n",i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_RESPONSE_LEN_OFF],result);
              /*
               * Not -EBUSY err, retry cannot fix the error in i2c_smbus_read_i2c_block_data( &i2c_cli, in_buf[I2C_RD_CMD_OFF], in_buf[I2C_RD_RESPONSE_LEN_OFF], out_buf ).
               * break for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )
               */
              break;
            }
           
         }
         else
         {
#ifdef DEBUG_IT
            pr_info("i2c_smbus_read_i2c_block_data read %d bytes\n",result);printk_hex_info(out_buf,in_buf[I2C_RD_RESPONSE_LEN_OFF]);
#endif
            if ( layer_1_retry_cnt > 0 )
            {
               pr_err("recovered -EBUSY after retried %d,i2c_smbus_read_i2c_block_data(0x%x,0x%02x,%d)\n",layer_1_retry_cnt,i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_RESPONSE_LEN_OFF]);
            }
            break; /* break for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ ) */
         }
      }
   }
   else
   {
     for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )
     {
        result = i2c_smbus_write_i2c_block_data( &i2c_cli, in_buf[I2C_RD_CMD_OFF], in_buf[I2C_RD_ARGC_OFF]+1, &in_buf[I2C_RD_ARGC_OFF] );
#ifdef DEBUG_IT
        pr_info("write to I2C bus 0x%02x:\n",in_buf[I2C_RD_CMD_OFF]);printk_hex_info(&in_buf[I2C_RD_ARGC_OFF],in_buf[I2C_RD_ARGC_OFF]+1);
#endif
        if ( result == 0 )
        {
           int layer_2_retry_cnt;
           for ( layer_2_retry_cnt = 0; layer_2_retry_cnt < RETRY_LIMIT; layer_2_retry_cnt++ )
           {
              result = i2c_smbus_read_i2c_block_data( &i2c_cli, in_buf[I2C_RD_CMD_OFF], in_buf[I2C_RD_RESPONSE_LEN_OFF], out_buf ); 
              if ( result < 0 )
              {
                 if ( result == -EBUSY )
                 {
                   /*
                    * The error -EBUSY is generated before reading real I2C device in the func "static int i801_check_pre(struct i801_priv *priv)" in file "i2c-i801.c" in INTEL uP.
                    * The driver "i2c-cadence.c" does not generate -EBUSY in ARM uP.
                    * Therefore Reading I2C device twice will not happen, so retry op should not confuse the PS4.
                    */
                    pr_err("tried %d,i2c_smbus_read_i2c_block_data(0x%x,0x%02xx,%d,out_buf) err %d\n",layer_2_retry_cnt+1,i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_RESPONSE_LEN_OFF],result);
                    msleep( 1 );
                 }
                 else
                 {
                    pr_err("i2c_smbus_read_i2c_block_data(0x%x,0x02x%x,%d,out_buf) err %d\n",i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_RESPONSE_LEN_OFF],result);
                    /*
                     * Not -EBUSY err, retry cannot fix the error in i2c_smbus_read_i2c_block_data( &i2c_cli, in_buf[I2C_RD_CMD_OFF], in_buf[I2C_RD_RESPONSE_LEN_OFF], out_buf ).
                     * break for ( layer_2_retry_cnt = 0; layer_2_retry_cnt < RETRY_LIMIT; layer_2_retry_cnt++ )
                     */
                    break;
                 }
              }
              else
              {
#ifdef DEBUG_IT
                 pr_info("read %d bytes:\n",result);printk_hex_info(out_buf,in_buf[I2C_RD_RESPONSE_LEN_OFF]);
#endif
                 if ( layer_2_retry_cnt > 0 )
                 {
                    pr_err("recovered -EBUSY after retried %d,i2c_smbus_read_i2c_block_data(0x%x,0x%02xx,%d,out_buf)\n",layer_2_retry_cnt,i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_RESPONSE_LEN_OFF]);
                 }

                 result = in_buf[I2C_RD_RESPONSE_LEN_OFF];
                 /*
                  * Successfull finished i2c_smbus_read_i2c_block_data(&i2c_cli,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_RESPONSE_LEN_OFF],out_buf).
                  * break for ( layer_2_retry_cnt = 0; layer_2_retry_cnt < RETRY_LIMIT; layer_2_retry_cnt++ )
                  */
                 break;
              }
           } /* for ( layer_2_retry_cnt = 0; layer_2_retry_cnt < RETRY_LIMIT; layer_2_retry_cnt++ ) */

           if ( layer_1_retry_cnt > 0 )
           {
              pr_err("recoverd -EBUSY after tried %d, write to i2c addr 0x%x for in_buf[I2C_RD_CMD_OFF]=0x%02x,in_buf[I2C_RD_ARGV_OFF]=0x%02x\n",layer_1_retry_cnt,i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_ARGV_OFF]);
           }

           break; /*!!!VERY IMPORTANT!!! break for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )*/
        }
        else
        {
           if ( result == -EBUSY )
           {
              pr_err("tried %d, write to i2c addr 0x%x for in_buf[I2C_RD_CMD_OFF]=0x%02x,in_buf[I2C_RD_ARGV_OFF]=0x%02x errno=%d\n",layer_1_retry_cnt+1,i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_ARGV_OFF],result);
              msleep( 1 );
           }
           else
           {
              pr_err("write to i2c addr 0x%x for in_buf[I2C_RD_CMD_OFF]=0x%02x,in_buf[I2C_RD_ARGV_OFF]=0x%02x errno=%d\n",i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_ARGV_OFF],result);
              /*
               * Not -EBUSY err, retry cannot fix the error in i2c_smbus_write_i2c_block_data(&i2c_cli,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_RD_ARGC_OFF]+1,&in_buf[I2C_RD_ARGC_OFF]).
               * break for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )
               */
              break;
           }
        }
     } /*for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )*/
  } 
  return result;
}

static s32 smbus_i2c_write_block
(
   struct nai_generic_i2c_driver_data* nai_generic_i2c_driver_data_ptr,
   const u8* in_buf,
   u16 in_buf_len
)
{
   s32 result = -1;
   int layer_1_retry_cnt;
   struct i2c_client i2c_cli;
   i2c_cli.adapter = nai_generic_i2c_driver_data_ptr->i2c_adapter_ptr;

   i2c_cli.flags=0; /*bug fix for sending invalid I2C slave address*/

   /*little-endian*/
   i2c_cli.addr = in_buf[I2C_WR_ADDR_EXT_OFF];
   i2c_cli.addr <<= 8;
   i2c_cli.addr |= in_buf[I2C_WR_ADDR_OFF];

   if ( in_buf[ I2C_WR_ADDR_EXT_OFF ] & 0xFF )
   {
      i2c_cli.flags |= I2C_M_TEN;
   }
#ifdef DEBUG_IT
   pr_info("in_buf:\n");printk_hex_info(in_buf,in_buf_len);
#endif
   for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )
   {
      result = i2c_smbus_write_i2c_block_data( &i2c_cli, in_buf[I2C_WR_CMD_OFF], in_buf[I2C_WR_ARGC_OFF], &in_buf[I2C_WR_ARGV_OFF] );
      if ( result == 0 )
      {
#ifdef DEBUG_IT
         pr_info("wr cmd 0x%x ok with para:\n",in_buf[I2C_WR_CMD_OFF]);printk_hex_info(&in_buf[I2C_WR_ARGV_OFF],in_buf[I2C_WR_ARGC_OFF]);
#endif
         if ( layer_1_retry_cnt > 0 )
         {
            pr_err("recovered after tried %d, i2c_smbus_write_i2c_block_data(0x%x,0x%02x,0x%02x,%02x)\n",layer_1_retry_cnt,i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_WR_ARGC_OFF],in_buf[I2C_WR_ARGV_OFF]);
         }
         result = in_buf[I2C_WR_ARGC_OFF];
         break; /*VERY IMPORTANT !!! break for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )*/
      }
      else
      {
         if ( result == -EBUSY )
         {
            pr_err("tried %d, i2c_smbus_write_i2c_block_data(0x%x,0x%02x,0x%02x,%02x),errno=%d\n",layer_1_retry_cnt,i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_WR_ARGC_OFF],in_buf[I2C_WR_ARGV_OFF],result);
            msleep( 1 );
         }
         else
         {
            pr_err("i2c_smbus_write_i2c_block_data(0x%x,0x%02x,0x%02x,%02x),errno=%d\n",i2c_cli.addr,in_buf[I2C_RD_CMD_OFF],in_buf[I2C_WR_ARGC_OFF],in_buf[I2C_WR_ARGV_OFF],result);
            /*
             * Not -EBUSY err, retry cannot fix the error in i2c_smbus_write_i2c_block_data( &i2c_cli, in_buf[I2C_WR_CMD_OFF], in_buf[I2C_WR_ARGC_OFF], &in_buf[I2C_WR_ARGV_OFF] ); 
             * break for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ ) 
             */
            break;
         }
      }
   }
   return result;
}

static int i2c_open(struct inode *inode, struct file *filp)
{
   int status = 0;
   /*
    * setup filp->private_data for other funcs such as file_operations.read, file_operations.write and file_operations.ioctl to access
    * fields in struct nai_generic_i2c_driver_data
    */
   filp->private_data=(struct nai_generic_i2c_driver_data*)container_of(inode->i_cdev,struct nai_generic_i2c_driver_data,c_dev);
#ifdef DEBUG_IT
   pr_info("nai_generic_i2c_driver_data vir_addr=0x%p\n",filp->private_data);
#endif

   mutex_lock(&((struct nai_generic_i2c_driver_data*)(filp->private_data))->mutex);
   if ( ((struct nai_generic_i2c_driver_data*)(filp->private_data))->user_cnt == 0 )
   {
#if defined(__x86_64__) || defined(__amd_64__)  || defined(__i386__)
      ((struct nai_generic_i2c_driver_data*)(filp->private_data))->i2c_adapter_ptr=i2c_get_adapter(I2C_INTEL_PERIPHERAL_ADAPTER);
#elif defined(__arm__)
      ((struct nai_generic_i2c_driver_data*)(filp->private_data))->i2c_adapter_ptr=i2c_get_adapter(I2C_ZYNQ_PERIPHERAL_ADAPTER);
#endif

      if ( ((struct nai_generic_i2c_driver_data*)(filp->private_data))->i2c_adapter_ptr == NULL )
      {
#if defined(__x86_64__) || defined(__amd_64__)  || defined(__i386__)
         pr_err("can't Attach I2C Adapter %d\n",I2C_INTEL_PERIPHERAL_ADAPTER);
#elif defined(__arm__)
         pr_err("can't Attach I2C Adapter %d\n",I2C_ZYNQ_PERIPHERAL_ADAPTER);
#endif
         status = -ENODEV; /*no such device, include/uapi/asm-generic/errno-base.h*/
         goto done;
      }
#ifdef DEBUG_IT
      if ( i2c_check_functionality( ((struct nai_generic_i2c_driver_data*)(filp->private_data))->i2c_adapter_ptr, I2C_FUNC_SMBUS_PEC ) )
      {
         pr_info("PEC set\n");
      }
      else
      {
         pr_info("PEC not set\n");
      }
#endif
      ((struct nai_generic_i2c_driver_data*)(filp->private_data))->user_cnt++;
   }

done:
   mutex_unlock(&((struct nai_generic_i2c_driver_data*)(filp->private_data))->mutex);

   return status ;
}

static int i2c_close(struct inode *i, struct file *filp)
{
   /* not safe in multi processor, set it to 0 only opened dev cnt is 0 */
   mutex_lock(&((struct nai_generic_i2c_driver_data*)(filp->private_data))->mutex);
   ((struct nai_generic_i2c_driver_data*)(filp->private_data))->user_cnt--;
   if ( ((struct nai_generic_i2c_driver_data*)(filp->private_data))->user_cnt == 0 )
   {
      i2c_put_adapter(((struct nai_generic_i2c_driver_data*)(filp->private_data))->i2c_adapter_ptr);

#ifdef DEBUG_IT
      pr_info("nai_generic_i2c_driver_data vir_addr=0x%p\n",filp->private_data);
#endif
   }
   mutex_unlock(&((struct nai_generic_i2c_driver_data*)(filp->private_data))->mutex);
   return 0;
}

static ssize_t i2c_read(struct file *filp, char __user *usr_buf, size_t len, loff_t *off)
{
   int result=4;
   struct nai_generic_i2c_driver_data *private_data_ptr=filp->private_data;
   
   uint8_t in_buf[MAX_I2C_X_BUF_LEN];
   uint8_t out_buf[MAX_I2C_X_BUF_LEN];

   memset(out_buf,0,sizeof(out_buf));
   memset(in_buf,0,sizeof(in_buf));
   mutex_lock(&private_data_ptr->mutex);

   if ( len < I2C_RD_ARGV_OFF+usr_buf[I2C_RD_ARGC_OFF] )
   {
      pr_err("error input format\n");
      result = -EINVAL;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("len: %d\n",len);
#endif
   if(copy_from_user(in_buf,usr_buf,I2C_RD_ARGV_OFF+usr_buf[I2C_RD_ARGC_OFF]))
   {
      pr_err("error copy_from_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("nai_generic_i2c_driver_data vir_addr=0x%p,input len %d:\n",private_data_ptr,len);printk_hex_info(in_buf,I2C_RD_ARGV_OFF+usr_buf[I2C_RD_ARGC_OFF]);
#endif

   if ( ( result = smbus_i2c_read_block( private_data_ptr, in_buf,I2C_RD_ARGV_OFF+in_buf[I2C_RD_ARGC_OFF],out_buf,in_buf[I2C_RD_RESPONSE_LEN_OFF] ) ) != in_buf[I2C_RD_RESPONSE_LEN_OFF] )
   {
      pr_err("err in smbus_i2c_read_block() %d\n",result);
      goto done;
   }

   if(copy_to_user(usr_buf,out_buf,in_buf[I2C_RD_RESPONSE_LEN_OFF]))
   {
      pr_err("err copy_to_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("read result:\n");printk_hex_info(out_buf,in_buf[I2C_RD_RESPONSE_LEN_OFF]);
#endif
   result = in_buf[I2C_RD_RESPONSE_LEN_OFF];
done:
   mutex_unlock(&private_data_ptr->mutex);

   if( result < 0 )
      pr_err("result err: %d\n",result);

   return result;
}

static ssize_t i2c_write(struct file *filp, const char __user *usr_buf, size_t len, loff_t *off)
{
   int result=0;

   uint8_t in_buf[MAX_I2C_X_BUF_LEN];

   struct nai_generic_i2c_driver_data *private_data_ptr=filp->private_data;

   mutex_lock(&private_data_ptr->mutex);
   if ( len < I2C_WR_ARGV_OFF+usr_buf[I2C_WR_ARGC_OFF] )
   {
      pr_err("len=%zd, I2C_WR_ARGV_OFF=%d,usr_buf[I2C_WR_ARGC_OFF]=%d,error input format\n",len,I2C_WR_ARGV_OFF,usr_buf[I2C_WR_ARGC_OFF]);
      result = -EINVAL;
      goto done;
   }

   if ( copy_from_user(in_buf,usr_buf,I2C_WR_ARGV_OFF+usr_buf[I2C_WR_ARGC_OFF]) )
   {
      pr_err("error copy_from_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("in buf:\n");printk_hex_info(in_buf,I2C_WR_ARGV_OFF+usr_buf[I2C_WR_ARGC_OFF]);
#endif
   if ( ( result = smbus_i2c_write_block( private_data_ptr, in_buf,I2C_WR_ARGV_OFF+in_buf[I2C_WR_ARGC_OFF] ) ) != in_buf[I2C_WR_ARGC_OFF] )
   {
      pr_err("err in smbus_i2c_write_block() %d\n",result);
   }

   mutex_unlock(&private_data_ptr->mutex);

done:
   return result;
}

static struct file_operations nai_generic_i2c_file_ops =
{
   .owner = THIS_MODULE,
   .open = i2c_open,
   .release = i2c_close,
   .read = i2c_read,
   .write = i2c_write,
};

static int nai_gen_i2c_driver_probe(struct platform_device *pdev)
{
   int ret=0;
   struct nai_generic_i2c_driver_data *private_data_ptr;
#ifdef DEBUG_IT
   pr_info("Probe Device: \"%s\"\n",pdev->name);
#endif
   /*
    * devm_kzalloc() is resource-managed kzalloc(). The memory allocated with resource-managed
    * functions is associated with the device. When the device is detached from the system or the
    * driver for the device is unloaded, that memory is freed automatically. It is possible to
    * free the memory with devm_kfree() if it's no longer needed.
    * private_data_ptr=devm_kzalloc(&pdev->dev,sizeof(*private_data_ptr),GFP_KERNEL);
    */
   private_data_ptr=kzalloc(sizeof(*private_data_ptr),GFP_KERNEL);

   if ( private_data_ptr==NULL )
   {
      pr_err("kmalloc(struct nai_generic_i2c_driver_data,GFP_KERNEL) err\n");
      ret= -ENOMEM;
      goto err_step_1;
   }

#ifdef DEBUG_IT
    pr_info("private_data_ptr vir_addr=0x%p,platform_device addr=0x%p\n",private_data_ptr,pdev);
#endif
                                                                 /*check man page, vs register_chrdev*/
   if ((ret = alloc_chrdev_region(&private_data_ptr->first, 0, 1, "i2c-dev-parent")) < 0)
   {
      pr_err("alloc_chrdev_region() err=%d\n",ret);
      goto err_step_2;
   }

   if (IS_ERR(private_data_ptr->generic_i2c_class = class_create(THIS_MODULE, I2C_DEVICE_FILE)))
   {
      pr_err("class_create() err=%ld\n",PTR_ERR(private_data_ptr->generic_i2c_class));
      ret=PTR_ERR(private_data_ptr->generic_i2c_class);
      goto err_step_3;
   }

   if (IS_ERR(device_create(private_data_ptr->generic_i2c_class, NULL, private_data_ptr->first, NULL, I2C_DEVICE_FILE)))
   {
      ret = PTR_ERR(device_create(private_data_ptr->generic_i2c_class, NULL, private_data_ptr->first, NULL, I2C_DEVICE_FILE));
      pr_err("device_create() err=%d\n",ret);
      goto err_step_4;
   }

   cdev_init(&private_data_ptr->c_dev,&nai_generic_i2c_file_ops);
   if ((ret = cdev_add(&private_data_ptr->c_dev, private_data_ptr->first, 1)) < 0)
   {
      pr_err("cdev_add() err=%d\n",ret);
      goto err_step_5;
   }

#ifdef DEBUG_IT
   pr_info("Probe Device: \"%s\",I2C_DEVICE_FILE=\"%s\"\n",pdev->name,I2C_DEVICE_FILE);
#endif
   platform_set_drvdata(pdev,private_data_ptr);
   mutex_init(&private_data_ptr->mutex);
   return 0;

err_step_5:
   device_destroy(private_data_ptr->generic_i2c_class, private_data_ptr->first);
err_step_4:
   class_destroy(private_data_ptr->generic_i2c_class);
err_step_3:
   unregister_chrdev_region(private_data_ptr->first,1);
err_step_2:
   kfree(private_data_ptr);
err_step_1:
   return ret;
}

static int nai_gen_i2c_driver_remove(struct platform_device *pdev)
{
   struct nai_generic_i2c_driver_data *private_data_ptr=platform_get_drvdata(pdev);
#ifdef DEBUG_IT
   pr_info("Remove Device: \"%s\"\n",pdev->name);
#endif
   /*without these release resource API calls, kernel panic when the module is removed by rmmod cmd or is re inserted by insmod cmd.*/
   cdev_del(&private_data_ptr->c_dev);
   device_destroy(private_data_ptr->generic_i2c_class, private_data_ptr->first);
   class_destroy(private_data_ptr->generic_i2c_class);
   unregister_chrdev_region(private_data_ptr->first,1);
#ifdef DEBUG_IT
   pr_info("unregistered\n");
#endif
   return 0;
}

static void nai_gen_i2c_device_release(struct device *dev)
{
#ifdef DEBUG_IT
   struct platform_device *derived_dev=container_of(dev,struct platform_device,dev);
   pr_info("Release Device: \"%s\"\n",derived_dev->name);
#endif
}

static struct platform_driver nai_gen_i2c_driver_driver =
{
   .driver =
   {
      .name = PROBE_NAME, /*The idriver name must match struct platform_device.name*/
      .owner = THIS_MODULE,
   },
   .probe = nai_gen_i2c_driver_probe,
   .remove = nai_gen_i2c_driver_remove,
};

static struct platform_device nai_gen_i2c_driver_device =
{
   .name = PROBE_NAME, /*The device name must match struct platform_driver.driver.name*/
   .id = 0,
   .dev =
   {
      .release = nai_gen_i2c_device_release,
   },
};

static int __init nai_gen_i2c_driver_init(void)
{
#ifdef DEBUG_IT
   pr_info("Driver test init\n");
#endif
   platform_device_register(&nai_gen_i2c_driver_device); /*device is diff from driver*/
   platform_driver_register(&nai_gen_i2c_driver_driver); /*dirver is diff from device*/
   return 0;
}

static void __exit nai_gen_i2c_driver_exit(void)
{
#ifdef DEBUG_IT
   pr_info("Driver Test Exit\n");
#endif
   platform_driver_unregister(&nai_gen_i2c_driver_driver);
   platform_device_unregister(&nai_gen_i2c_driver_device);
}

module_init(nai_gen_i2c_driver_init);
module_exit(nai_gen_i2c_driver_exit);
MODULE_LICENSE("GPL");
MODULE_VERSION("1.40");

#include <linux/init.h>
#define pr_fmt(fmt) "%s:%d>" fmt,strrchr(__FILE__,'/'),__LINE__
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
#include <linux/console.h>
#include <linux/cdev.h>
#include <asm/ioctl.h>
#include <asm/irq.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 3, 0)
#include <asm/switch_to.h>
#else
#include <asm/system.h>
#endif
#include <linux/uaccess.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#include "nai_generic_i2c.h"
#include "shared_utils.h"
#include "shared_smbus.h"

#define UDELAY_VAL 500

#define MULTI_ACCESSING

/******************* nai smbus shared functions begin *******************/
#ifdef MULTI_ACCESSING
static ssize_t smbus_read_block
(
   struct i2c_client* i2c_cli_ptr,
   char *buf,
   uint16_t offset,
   size_t count
)
{
   int status=0;

   if (count > I2C_SMBUS_BLOCK_MAX)
   {
      count = I2C_SMBUS_BLOCK_MAX;
   }

   status = i2c_smbus_read_i2c_block_data(i2c_cli_ptr, offset, count, buf);
   if (status == count)
   {
#ifdef DEBUG_IT
      pr_info("I2C_ADDR:0x%X,adp:0x%p,rd:%zu,cmd/off:0x%02x,sts:%d\n",i2c_cli_ptr->addr,i2c_cli_ptr->adapter,count,offset,status);// printk_hex_info(buf,count);
#endif
      return count;
   }
   else
   {
      pr_err("I2C_ADDR:0x%X,err:adp:0x%p,rd:%zu,cmd/off:0x%02x,buf[0]:0x%02x,sts:%d\n",i2c_cli_ptr->addr,i2c_cli_ptr->adapter,count,offset,buf[0],status);// printk_hex_info(buf,count);
      return status;
   }
}
EXPORT_SYMBOL(smbus_read_block);

int32_t nai_smbus_read_block
(
   struct i2c_adapter *i2c_adapter_ptr,
   const uint8_t* in_buf,
   uint16_t in_buf_len,
   uint8_t* out_buf,
   uint16_t out_buf_len
)
{
   int32_t result = 0, status;
   uint8_t off=in_buf[SMBUS_RD_CMD_OFF];
   size_t cnt=in_buf[SMBUS_RD_LEN_OFF];
   struct i2c_client i2c_cli;

   if(out_buf_len<cnt)
   {
      pr_err("wrong fmt,buf_sz:%u<rd_cnt:%zu\n",out_buf_len,cnt);
      return -ENOEXEC;
   }

   build_i2c_client_addr(&i2c_cli,in_buf);
#ifdef DEBUG_IT
   pr_info("client.addr:0x%x,cnt:0x%x\n",i2c_cli.addr,in_buf[SMBUS_RD_LEN_OFF]);
#endif
   i2c_cli.adapter = i2c_adapter_ptr;

   while ( cnt != 0 )
   {
      uint32_t retry_cnt;

      for(retry_cnt=0;retry_cnt<RETRY_LIMIT;retry_cnt++)
      {
         status = smbus_read_block
         (
            &i2c_cli,
            out_buf,
            off,
            cnt
         );

         if( status > 0 )
         {

            if(retry_cnt>0)
            {
               pr_err("retried %u,recovered -EBUSY|ENXIO rd:%d,cmd/off:0x%02x OK.\n",retry_cnt,status,off);
            }

            out_buf += status;
            off += status;
            cnt -= status;
            result += status;
            break;
         }
         else if ( status == -EBUSY || status == -ENXIO )
         { /*some time, PS4 returns -EBUSY, and AT24 EEPROM returns -ENXIO*/
            pr_err("I2C_ADDR:0x%X,rd_blk err:-EBUSY|ENXIO, retry:%d\n",i2c_cli.addr,retry_cnt+1);
            udelay( UDELAY_VAL );
         }
         else
         {
            result = status;
            pr_err("I2C_ADDR:0x%X,smbus_write_block(...) err:%d",i2c_cli.addr,status);
            break;/*break for(tetry_cnt=0;tetry_cnt<ETRY_LIMIT;retry_cnt++)*/
         }

      }/*for(retry_cnt=0;retry_cnt<RETRY_LIMIT;retry_cnt++)*/

      if( status < 0 )
      {
         pr_err("I2C_ADDR:0x%X,smbus_read_block(...) err:%d\n",i2c_cli.addr,status);
         result=status;
         break;/*break while ( wr_cnt != 0 )*/
      }
#ifdef DEBUG_IT
      else
      {
         pr_info("exp %d,rd %d ok\n",in_buf[SMBUS_RD_LEN_OFF],result);
      }
#endif
   }

   return result;
}
#else /* !MULTI_ACCESSING */
s32 nai_smbus_read_block
(
   struct i2c_adapter *i2c_adapter_ptr, 
   const u8* in_buf,
   u16 in_buf_len,
   u8* out_buf,
   u16 out_buf_len
)
{
   struct i2c_client i2c_cli;
   s32 result = 0;
   int layer_1_retry_cnt;
   i2c_cli.adapter = i2c_adapter_ptr;

   i2c_cli.flags = 0; /*bug fix for sending invalid I2C slave address*/

   /*little-endian*/
   i2c_cli.addr = in_buf[SMBUS_WR_ADDR_EXT_OFF];
   i2c_cli.addr <<= 8;
   i2c_cli.addr |= in_buf[SMBUS_WR_ADDR_OFF];

   if ( in_buf[ SMBUS_RD_ADDR_EXT_OFF ] & 0xFF )
   {
      i2c_cli.flags |= I2C_M_TEN;
   }
#ifdef DEBUG_IT
   pr_info("input:\n");printk_hex_info(in_buf,in_buf_len);
#endif

   for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )
   {
      result = i2c_smbus_read_i2c_block_data( &i2c_cli, in_buf[SMBUS_RD_CMD_OFF], out_buf_len, out_buf );
      if( result < 0 )
      {
         if ( result == -EBUSY || -ENXIO )
         { /*some time, PS4 returns -EBUSY, and AT24 EEPROM returns -ENXIO*/
            pr_err("tried %d,i2c_smbus_read_i2c_block_data(0x%x,0x%02x,%d) err:%d\n",layer_1_retry_cnt+1,i2c_cli.addr,in_buf[SMBUS_RD_CMD_OFF],in_buf[SMBUS_RD_LEN_OFF],result);
            udelay( UDELAY_VAL );
         }
         else
         {
            pr_err("i2c_smbus_read_i2c_block_data(0x%x,0x%02x,%d) err:%d\n",i2c_cli.addr,in_buf[SMBUS_RD_CMD_OFF],in_buf[SMBUS_RD_LEN_OFF],result);
           /*
            * Not -EBUSY/ENXIO err, retry cannot fix the error in i2c_smbus_read_i2c_block_data( &i2c_cli, in_buf[SMBUS_RD_CMD_OFF], out_buf_len, out_buf ).
            * break for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )
            */
            break;
         }
      }
      else
      {
#ifdef DEBUG_IT
         pr_info("i2c_smbus_read_i2c_block_data read %d byte(s)\n",result);printk_hex_info(out_buf,in_buf[SMBUS_RD_LEN_OFF]);
#endif
         if ( layer_1_retry_cnt > 0 )
         {
            pr_err("recovered -EBUSY|ENXIO after retried %d,i2c_smbus_read_i2c_block_data(0x%x,0x%02x,%d)\n",layer_1_retry_cnt,i2c_cli.addr,in_buf[SMBUS_RD_CMD_OFF],in_buf[SMBUS_RD_LEN_OFF]);
         }
         break; /* break for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ ) */
      }
   }

#ifdef DEBUG_IT
   pr_info("out_buf with len:%d:\n",out_buf_len);printk_hex_info(out_buf,out_buf_len);
#endif
   return result;
}
#endif /* MULTI_ACCESSING */
EXPORT_SYMBOL(nai_smbus_read_block);

ssize_t nai_smbus_dev_read(struct file *filp, char __user *usr_buf, size_t len, loff_t *off)
{
   int result=4;
   struct i2c_smbus_drv_data *i2c_drv_data_ptr=filp->private_data;

#ifdef DEBUG_IT
   pr_info("len: %d\n",len);
#endif

   uint8_t in_buf[NAI_I2C_SMBUS_RD_WR_MGR_BUF_LEN];
   uint8_t out_buf[SMBUS_TRX_BUF_LEN_MAX*8];

   memset(out_buf,0,sizeof(out_buf));
   memset(in_buf,0,sizeof(in_buf));

   mutex_lock(i2c_drv_data_ptr->mutex_ptr); /*mutex is set by fs_ops creater*/

   if ( len < the_max (NAI_SMBUS_RD_MGR_HEADER_SZ,usr_buf[SMBUS_RD_LEN_OFF]) )
   {
      pr_err("len=%zd less the max (NAI_SMBUS_RD_MGR_HEADER_SZ=%u,usr_buf[SMBUS_RD_LEN_OFF]=%u,error input format\n",len,NAI_SMBUS_RD_MGR_HEADER_SZ,usr_buf[SMBUS_RD_LEN_OFF]);
#ifdef DEBUG_IT
      printk_hex_info(usr_buf,len);
#endif
      result = -EINVAL;
      goto done;
   }

   if(copy_from_user(in_buf,usr_buf,NAI_SMBUS_RD_MGR_HEADER_SZ))
   {
      pr_err("error copy_from_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("i2c_drv_data vir_addr:0x%p,rd_sz:%d,rd_mgr_str:\n",i2c_drv_data_ptr,len);printk_hex_info(in_buf,NAI_SMBUS_RD_MGR_HEADER_SZ);
#endif

   if ( ( result = nai_smbus_read_block( i2c_drv_data_ptr->i2c_adapter_ptr, in_buf,the_max(NAI_SMBUS_RD_MGR_HEADER_SZ,in_buf[SMBUS_RD_LEN_OFF]),out_buf,in_buf[SMBUS_RD_LEN_OFF] ) ) != in_buf[SMBUS_RD_LEN_OFF] )
   {
      pr_err("I2C_ADDR:0x%X,err in nai_smbus_read_block() %d\n",*((u16*)usr_buf),result);
      goto done;
   }

   if(copy_to_user(usr_buf,out_buf,in_buf[SMBUS_RD_LEN_OFF]))
   {
      pr_err("err copy_to_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("read result:\n");printk_hex_info(out_buf,in_buf[SMBUS_RD_LEN_OFF]);
#endif
   result = in_buf[SMBUS_RD_LEN_OFF];

done:
   mutex_unlock(i2c_drv_data_ptr->mutex_ptr);

   if( result < 0 )
   {
      pr_err("I2C_ADDR:0x%X,result err: %d\n",*((u16*)usr_buf),result);
   }

   return result;
}
EXPORT_SYMBOL(nai_smbus_dev_read);

#ifdef MULTI_ACCESSING
static int32_t smbus_write_block
(
   struct i2c_client* i2c_cli_ptr,
   const char *buf,
   unsigned offset,
   size_t count
)
{
#define ATMEL_AT24_PAGE_WRITE_SIZE    8 /*see the data sheet about Page Write in Atmel AT24CS01 and AT24CS02 Page 10*/
  
  int32_t status;

  if (count > I2C_SMBUS_BLOCK_MAX)
  {
     count = I2C_SMBUS_BLOCK_MAX;
  }

  status = i2c_smbus_write_i2c_block_data( i2c_cli_ptr, offset, count, buf );

  if (status == 0)
  {
#ifdef DEBUG_IT
     pr_info("I2C_ADDR:0x%X,adp.:0x%p,wr:%zu,cmd/off:0x%02x,buf[0]:0x%02x,sts:%d\n",i2c_cli_ptr->addr,i2c_cli_ptr->adapter,count,offset,buf[0],status);
#endif
     status = count;
  }
  else
  {
     pr_err("I2C_ADDR:0x%X,err:adp.:0x%p,wr:%zu,cmd/off:0x%02x,buf[0]:0x%02x,sts:%d\n",i2c_cli_ptr->addr,i2c_cli_ptr->adapter,count,offset,buf[0],status);
  }

  return status;
}
EXPORT_SYMBOL(smbus_write_block);

int32_t nai_smbus_write_block
(
   struct i2c_adapter *i2c_adapter_ptr,
   const uint8_t* io_buf,
   uint16_t io_buf_sz
)
{
   int32_t result = 0, status;
   uint8_t off=io_buf[SMBUS_WR_CMD_OFF];
   size_t wr_cnt=io_buf[SMBUS_WR_DATA_LEN_OFF];
   const uint8_t* wr_buf;
   struct i2c_client i2c_cli;
   build_i2c_client_addr(&i2c_cli,io_buf);
   i2c_cli.adapter = i2c_adapter_ptr;
#ifdef DEBUG_IT
   pr_info("i2c_cli.addr=0x%x\n",i2c_cli.addr);
   printk_hex_info(io_buf,SMBUS_WR_DATA_OFF+io_buf[SMBUS_WR_DATA_LEN_OFF]);
#endif

   wr_buf=io_buf+SMBUS_WR_DATA_OFF;

#ifdef DEBUG_IT
   pr_info("write info:\n");
   printk_hex_info(wr_buf,wr_cnt);
#endif

   while ( wr_cnt != 0 )
   { 
      uint32_t retry_cnt;

      for(retry_cnt=0;retry_cnt<RETRY_LIMIT;retry_cnt++)
      {
         status = smbus_write_block(&i2c_cli,wr_buf,off,wr_cnt);
         if ( status >= 0 )
         {

            if(retry_cnt>0)
            {
               pr_err("retried %u,recovered -EBUSY|ENXIO wr:%d,cmd/off:0x%02x,wr_buf[0]:0x%x OK.\n",retry_cnt,status,off,wr_buf[0]);
            }

            wr_buf += status;
            off += status;
            wr_cnt -= status;
            result += status;
            break;
         }
         else if ( status == -EBUSY || status == -ENXIO )
         { /*some time, PS4 returns -EBUSY, and AT24 EEPROM returns -ENXIO*/
            pr_err("I2C_ADDR:0x%X,wr_blk err:-EBUSY|ENXIO, retry:%d\n",i2c_cli.addr,retry_cnt+1);
            udelay( UDELAY_VAL );
         }
         else
         {
            pr_err("smbus_write_block(...) err:%d",status);
            break;/*break for(tetry_cnt=0;tetry_cnt<ETRY_LIMIT;retry_cnt++)*/
         }
      }/*for(retry_cnt=0;retry_cnt<RETRY_LIMIT;retry_cnt++)*/

      if( status < 0 )
      {
         pr_err("I2C_ADDR:0x%X,smbus_write_block(...) err:%d\n",i2c_cli.addr,status);
         result=status;
         break;/*break while ( wr_cnt != 0 )*/
      }
#ifdef DEBUG_IT
      else
      {
         pr_info("exp %d,wr %d ok\n",io_buf[SMBUS_WR_DATA_LEN_OFF],result);
      }
#endif
   }/*while ( wr_cnt != 0 )*/

#ifdef DEBUG_IT
   pr_info("result:%d\n",result);
#endif

   return result;
}
#else /* !MULTI_ACCESSING */
s32 nai_smbus_write_block
(
   struct i2c_adapter *i2c_adapter_ptr,
   const u8* in_buf,
   u16 in_buf_len
)
{
   s32 result = -1;
   int layer_1_retry_cnt;
   struct i2c_client i2c_cli;
   i2c_cli.adapter = i2c_adapter_ptr;

   i2c_cli.flags=0; /*bug fix for sending invalid I2C slave address*/

   /*little-endian*/
   i2c_cli.addr = in_buf[SMBUS_WR_ADDR_EXT_OFF];
   i2c_cli.addr <<= 8;
   i2c_cli.addr |= in_buf[SMBUS_WR_ADDR_OFF];

   if ( in_buf[ SMBUS_WR_ADDR_EXT_OFF ] & 0xFF )
   {
      i2c_cli.flags |= I2C_M_TEN;
   }
   for ( layer_1_retry_cnt = 0; layer_1_retry_cnt < RETRY_LIMIT; layer_1_retry_cnt++ )
   {
#ifdef DEBUG_IT
      pr_info("in_buf[SMBUS_WR_DATA_LEN_OFF:0x%x]:0x%x",SMBUS_WR_DATA_LEN_OFF,in_buf[SMBUS_WR_DATA_LEN_OFF]);printk_hex_info(&in_buf[SMBUS_WR_DATA_OFF],in_buf[SMBUS_WR_DATA_LEN_OFF]);
#endif
      result = i2c_smbus_write_i2c_block_data( &i2c_cli, in_buf[SMBUS_WR_CMD_OFF], in_buf[SMBUS_WR_DATA_LEN_OFF], &in_buf[SMBUS_WR_DATA_OFF] );
      if ( result == 0 )
      {
#ifdef DEBUG_IT
         pr_info("wr cmd '0x%x',data_len:0x%x:\n",in_buf[SMBUS_WR_CMD_OFF],in_buf[SMBUS_WR_DATA_LEN_OFF]);printk_hex_info(&in_buf[SMBUS_WR_DATA_OFF],in_buf[SMBUS_WR_DATA_LEN_OFF]);
#endif
         if ( layer_1_retry_cnt > 0 )
         {
            pr_err("recovered after tried %d, i2c_smbus_write_i2c_block_data(0x%x,0x%02x,0x%02x,%02x)\n",layer_1_retry_cnt,i2c_cli.addr,in_buf[SMBUS_RD_CMD_OFF],in_buf[SMBUS_WR_DATA_LEN_OFF],in_buf[SMBUS_WR_DATA_OFF]);
         }
         result = in_buf[SMBUS_WR_DATA_LEN_OFF];
         break;
      }
      else
      {
         if ( result == -EBUSY || -ENXIO )
         { /*some time, PS4 returns -EBUSY, and AT24 EEPROM returns -ENXIO*/
            pr_err("tried %d, i2c_smbus_write_i2c_block_data(0x%x,0x%02x,0x%02x,%02x),errno=%d\n",layer_1_retry_cnt,i2c_cli.addr,in_buf[SMBUS_RD_CMD_OFF],in_buf[SMBUS_WR_DATA_LEN_OFF],in_buf[SMBUS_WR_DATA_OFF],result);
            udelay( UDELAY_VAL );
         }
         else
         {
            pr_err("i2c_smbus_write_i2c_block_data(0x%x,0x%02x,0x%02x,%02x),errno=%d\n",i2c_cli.addr,in_buf[SMBUS_RD_CMD_OFF],in_buf[SMBUS_WR_DATA_LEN_OFF],in_buf[SMBUS_WR_DATA_OFF],result);
            break;
         }
      }
   }
   return result;
}
EXPORT_SYMBOL(nai_smbus_write_block);
#endif /* MULTI_ACCESSING */

ssize_t nai_smbus_dev_write(struct file *filp, const char __user *usr_buf, size_t len, loff_t *off)
{
   int result=0;
   uint8_t in_buf[NAI_I2C_SMBUS_RD_WR_MGR_BUF_LEN+SMBUS_TRX_BUF_LEN_MAX];

   struct i2c_smbus_drv_data *i2c_drv_data_ptr=filp->private_data;

#ifdef DEBUG_IT
   pr_info("usr_buf[SMBUS_WR_DATA_LEN_OFF:0x%x]=%d,usr_buf:\n",SMBUS_WR_DATA_LEN_OFF,usr_buf[SMBUS_WR_DATA_LEN_OFF]);printk_hex_info(usr_buf,SMBUS_WR_DATA_OFF+usr_buf[SMBUS_WR_DATA_LEN_OFF]);
#endif
   mutex_lock(i2c_drv_data_ptr->mutex_ptr);
   memset(in_buf,0,sizeof(in_buf));
   if ( len < the_max(NAI_SMBUS_WR_MGR_HEADER_SZ,usr_buf[SMBUS_WR_DATA_LEN_OFF]) )
   {
      pr_err("len=%zd, SMBUS_WR_DATA_OFF=%d,usr_buf[SMBUS_WR_DATA_LEN_OFF]=%d,error input format\n",len,SMBUS_WR_DATA_OFF,usr_buf[SMBUS_WR_DATA_LEN_OFF]);
      result = -EINVAL;
      goto done;
   }

   memset(in_buf,0,sizeof(in_buf));

   if ( copy_from_user(in_buf,usr_buf,SMBUS_WR_DATA_OFF+usr_buf[SMBUS_WR_DATA_LEN_OFF]) )
   {
      pr_err("error copy_from_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("in_buf[SMBUS_WR_DATA_LEN_OFF:0x%x]=%d,payload:\n",SMBUS_WR_DATA_LEN_OFF,in_buf[SMBUS_WR_DATA_LEN_OFF]);printk_hex_info(in_buf+SMBUS_WR_DATA_OFF,in_buf[SMBUS_WR_DATA_LEN_OFF]);
#endif

   result = nai_smbus_write_block( i2c_drv_data_ptr->i2c_adapter_ptr,in_buf,len < SMBUS_WR_DATA_OFF ? SMBUS_WR_DATA_OFF:SMBUS_WR_DATA_OFF+in_buf[SMBUS_WR_DATA_LEN_OFF] );
   if ( result != in_buf[SMBUS_WR_DATA_LEN_OFF] )
   {
      pr_err("I2C_ADDR:0x%X,err in nai_smbus_write_block() %d\n",*((u16*)usr_buf),result);
   }

done:
   mutex_unlock(i2c_drv_data_ptr->mutex_ptr);

   return result;
}
EXPORT_SYMBOL(nai_smbus_dev_write);

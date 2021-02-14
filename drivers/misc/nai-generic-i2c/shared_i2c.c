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
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/platform_data/at24.h>
#include <nai_generic_i2c.h>
#include "shared_utils.h"

#ifdef SUPPORT_I2C_DEVICE_FILE
#include "shared_i2c.h"
/******************* nai i2c shared functions begin *******************/
static ssize_t i2c_read_block
(
   struct i2c_smbus_drv_data* i2c_drv_data_ptr,
   char *buf,
   uint16_t offset,
   size_t count
)
{
   struct i2c_msg msg[2];
   uint8_t msgbuf[2]={0,0};
   unsigned long timeout, read_time;
   int status, i=0;

   memset(msg, 0, sizeof(msg));

   if (i2c_drv_data_ptr->chip_access_flags & AT24_FLAG_ADDR16)
   {
      msgbuf[i++] = offset >> 8;
   }
   msgbuf[i++] = offset;

   if ( count > i2c_drv_data_ptr->io_limit )
   {
      count = i2c_drv_data_ptr->io_limit;
   }

   msg[0].addr = i2c_drv_data_ptr->clients[0]->addr;
   msg[0].buf = msgbuf;
   msg[0].len = i;

   msg[1].addr = i2c_drv_data_ptr->clients[0]->addr;
   msg[1].flags = I2C_M_RD;
   msg[1].buf = buf;
   msg[1].len = count;

   timeout = jiffies + msecs_to_jiffies(i2c_drv_data_ptr->write_timeout);

   do
   {
      status = i2c_transfer(i2c_drv_data_ptr->clients[0]->adapter, msg, 2);//msg has i2c addr info
      if (status == 2)
      {
         status = count;
#ifdef DEBUG_IT
         pr_info("read %zu bytes @off:%d(0x%x) -->status:%d,(jiffies:%lu)\n", count, offset, offset, status, jiffies); printk_hex_info(buf,count);
#endif
         return count;
      }
      /* REVISIT: at HZ=100, this is sloooow */
      msleep(1);
   }
   while (time_before(read_time, timeout));

   pr_err("time out\n");
   return -ETIMEDOUT;
}
EXPORT_SYMBOL(i2c_read_block);

static int32_t nai_i2c_read_block
(
   struct i2c_smbus_drv_data* i2c_drv_data_ptr,
   const uint8_t* in_buf,
   uint16_t in_buf_len,
   uint8_t* out_buf,
   uint16_t out_buf_len
)
{
   int32_t result = 0, status;
   loff_t off=in_buf[I2C_RD_CMD_OFF]*256+in_buf[I2C_RD_ARGV_OFF];
   size_t cnt=in_buf[I2C_RD_LEN_MSB_OFF]*256+in_buf[I2C_RD_LEN_LSB_OFF];

   struct i2c_client client;
   i2c_drv_data_ptr->clients[0] = &client;
   build_i2c_client_addr(i2c_drv_data_ptr->clients[0],in_buf);
#ifdef DEBUG_IT
   pr_info("client.addr=0x%x\n",i2c_drv_data_ptr->clients[0]->addr);
#endif
   i2c_drv_data_ptr->clients[0]->adapter = i2c_drv_data_ptr->i2c_adapter_ptr;

   while ( cnt != 0 )
   {
      status = i2c_read_block
      (
         i2c_drv_data_ptr,
         out_buf,
         off,
         cnt
      );
      if( status <=0 )
      {
         if ( result == 0 )
         {
            result = status;
         }
         break;
      }
      out_buf += status;
      off += status;
      cnt -= status;
      result += status;
   }
   return result;
}
EXPORT_SYMBOL(nai_i2c_read_block);

/*
 * buf: just only data written to chip.
 * off: offset the data will be written.
 * count: data size.
 */
static ssize_t i2c_write_block
(
   struct i2c_smbus_drv_data* i2c_drv_data_ptr,
   const uint8_t* buf,
   uint16_t offset,
   size_t count
)
{
   ssize_t status = 0;
   unsigned long timeout,write_time;
   struct i2c_msg msg;
   s8 tmp_wr_buf[I2C_TRX_BUF_LEN_MAX];
   int i=0; 

   memset(tmp_wr_buf, 0, sizeof(tmp_wr_buf));
   msg.buf = tmp_wr_buf;

   if ( i2c_drv_data_ptr->chip_access_flags & AT24_FLAG_ADDR16 )
   {
      msg.buf[i++] = offset >> 8;
   }
   msg.buf[i++] = offset;

   if ( count > i2c_drv_data_ptr->io_limit )
   {
      count = i2c_drv_data_ptr->io_limit;
   }

   memcpy(&msg.buf[i], buf, count);
   msg.len = i + count;

   msg.flags = 0;
   //msg.addr = i2c_client_ptr->addr;
   msg.addr = i2c_drv_data_ptr->clients[0]->addr;

   timeout = jiffies + msecs_to_jiffies(i2c_drv_data_ptr->write_timeout);
   do
   {
      write_time = jiffies;
      status = i2c_transfer(i2c_drv_data_ptr->clients[0]->adapter, &msg, 1);//msg has i2c addr info
      if (status == 1)
      {
         status = count;
      }
#ifdef DEBUG_IT
      pr_info("cli-dev: write %zu@%u --> %zd (%lu)\n",count,offset,status,jiffies);
#endif 
      if (status == count)
      {
         return count;
      }
 
      /* REVISIT: at HZ=100, this is sloooow */
      msleep(1);
   }
   while (time_before(write_time, timeout));
 
   return -ETIMEDOUT;
}
EXPORT_SYMBOL(i2c_write_block);

static int32_t nai_i2c_write_block
(
   struct i2c_smbus_drv_data* i2c_drv_data_ptr,
   const uint8_t* in_buf,
   uint16_t in_buf_len
)
{
   int32_t result = 0, status;
   loff_t off=in_buf[I2C_WR_CMD_OFF]*256+in_buf[I2C_WR_ARGV_OFF];
   size_t cnt=in_buf[I2C_WR_LEN_MSB_OFF]*256+in_buf[I2C_WR_LEN_LSB_OFF];/*1 is the extra byte for offset LSB*/

   struct i2c_client client;
   const uint8_t* wr_buf = in_buf+I2C_WR_ARGV_OFF+1;/*1 is the extra byte for offset LSB, already handled by off assignment*/
   build_i2c_client_addr(&client,in_buf);
   i2c_drv_data_ptr->clients[0]=&client;
#ifdef DEBUG_IT
   pr_info("client.addr=0x%x\n",client.addr);
   printk_hex_info(wr_buf,in_buf[I2C_WR_LEN_MSB_OFF]*256+in_buf[I2C_WR_LEN_LSB_OFF]);
#endif
   i2c_drv_data_ptr->clients[0]->adapter = i2c_drv_data_ptr->i2c_adapter_ptr;
   while ( cnt != 0 )
   { 
      status = i2c_write_block
               (
                  i2c_drv_data_ptr,
                  wr_buf,
                  off,
                  cnt
               );
      if ( status <= 0 )
      {
         if ( result == 0 )
         {
           result = status;
           break;
         }
      }
      wr_buf += status;
      off += status;
      cnt -= status;
      result += status;
   }
   return result;
}
EXPORT_SYMBOL(nai_i2c_write_block);

ssize_t nai_i2c_dev_read(struct file *filp, char __user *usr_buf, size_t len, loff_t *off)
{
   int result=4;
   struct i2c_smbus_drv_data *i2c_drv_data_ptr=filp->private_data;
   
#ifdef DEBUG_IT
   pr_info("len: %d\n",len);
#endif

   uint8_t in_buf[NAI_I2C_SMBUS_RD_WR_MGR_BUF_LEN];
   uint8_t out_buf[I2C_TRX_BUF_LEN_MAX];

   memset(out_buf,0,sizeof(out_buf));
   memset(in_buf,0,sizeof(in_buf));

   mutex_lock(i2c_drv_data_ptr->mutex_ptr);

   if ( len < I2C_RD_ARGV_OFF+usr_buf[I2C_RD_ARGC_OFF] )
   {
      pr_err("len=%d,I2C_RD_ARGV_OFF+usr_buf[I2C_RD_ARGC_OFF]=%d,error input format\n",len,I2C_RD_ARGV_OFF+usr_buf[I2C_RD_ARGC_OFF]);
#ifdef DEBUG_IT
      printk_hex_info(usr_buf,len);
#endif
      result = -EINVAL;
      goto done;
   }
  
   if(copy_from_user(in_buf,usr_buf,I2C_RD_ARGV_OFF+usr_buf[I2C_RD_ARGC_OFF]))
   {
      pr_err("error copy_from_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("i2c_drv_data vir_addr=0x%p,input len %d:\n",i2c_drv_data_ptr,len);printk_hex_info(in_buf,I2C_RD_ARGV_OFF+usr_buf[I2C_RD_ARGC_OFF]);
#endif
 
   result = nai_i2c_read_block( i2c_drv_data_ptr, in_buf,I2C_RD_ARGV_OFF+in_buf[I2C_RD_ARGC_OFF],out_buf,in_buf[I2C_RD_LEN_MSB_OFF]*256+in_buf[I2C_RD_LEN_LSB_OFF] );
   if ( result != in_buf[I2C_RD_LEN_MSB_OFF]*256+in_buf[I2C_RD_LEN_LSB_OFF] )
   {
      pr_err("err in nai_i2c_read_block() %d\n",result);
      goto done;
   }

   if(copy_to_user(usr_buf,out_buf,in_buf[I2C_RD_LEN_MSB_OFF]*256+in_buf[I2C_RD_LEN_LSB_OFF]))
   {
      pr_err("err copy_to_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("read result:\n");printk_hex_info(out_buf,in_buf[I2C_RD_LEN_MSB_OFF]*256+in_buf[I2C_RD_LEN_LSB_OFF]);
#endif
   result = in_buf[I2C_RD_LEN_MSB_OFF]*256+in_buf[I2C_RD_LEN_LSB_OFF];

done:
   mutex_unlock(i2c_drv_data_ptr->mutex_ptr);

   if( result < 0 )
      pr_err("result err: %d\n",result);

   return result;
}
EXPORT_SYMBOL(nai_i2c_dev_read);

ssize_t nai_i2c_dev_write(struct file *filp, const char __user *usr_buf, size_t len, loff_t *off)
{
   int result=0;
   uint8_t in_buf[NAI_I2C_SMBUS_RD_WR_MGR_BUF_LEN+I2C_TRX_BUF_LEN_MAX];

   struct i2c_smbus_drv_data *i2c_drv_data_ptr=filp->private_data;

   mutex_lock(i2c_drv_data_ptr->mutex_ptr);

   if ( len < I2C_WR_ARGV_OFF+usr_buf[I2C_WR_LEN_MSB_OFF]*256+usr_buf[I2C_WR_LEN_LSB_OFF] )
   {
      pr_err("len=%zd, I2C_WR_ARGV_OFF=%d,usr_buf[I2C_WR__MSB_OFF]*256+usr_buf[I2C_WR__LSB_OFF]=%d,error input format\n",len,I2C_WR_ARGV_OFF,usr_buf[I2C_WR_LEN_MSB_OFF]*256+usr_buf[I2C_WR_LEN_LSB_OFF]);
      result = -EINVAL;
      goto done;
   }

   memset(in_buf,0,sizeof(in_buf));

   if ( copy_from_user(in_buf,usr_buf,I2C_WR_ARGV_OFF+usr_buf[I2C_WR_ARGC_OFF]+usr_buf[I2C_WR_LEN_MSB_OFF]*256+usr_buf[I2C_WR_LEN_LSB_OFF]) )
   {
      pr_err("error copy_from_user()\n");
      result = -EFAULT;
      goto done;
   }
#ifdef DEBUG_IT
   pr_info("in_buf:\n");printk_hex_info(in_buf,I2C_WR_ARGV_OFF+usr_buf[I2C_WR_ARGC_OFF]+usr_buf[I2C_WR_LEN_MSB_OFF]*256+usr_buf[I2C_WR_LEN_LSB_OFF]);
#endif

   result = nai_i2c_write_block( i2c_drv_data_ptr, in_buf,I2C_WR_ARGV_OFF+usr_buf[I2C_WR_LEN_MSB_OFF]*256+usr_buf[I2C_WR_LEN_LSB_OFF] );
   if ( result != usr_buf[I2C_WR_LEN_MSB_OFF]*256+usr_buf[I2C_WR_LEN_LSB_OFF] )
   {
      pr_err("err in nai_i2c_write_block() %d\n",result);
   }

done:
   mutex_unlock(i2c_drv_data_ptr->mutex_ptr);

   return result;
}
EXPORT_SYMBOL(nai_i2c_dev_write);
/******************* nai i2c shared functions end *******************/

/******************* nai i2c serial number read shared functions begin *******************/
#define AT24CS_SERIAL_SIZE 16
#define AT24CS_SERIAL_ADDR(addr) (addr + 0x08) /*A Dummy Write, Datasheet Figure 10-1 Device Address in Atmel-8815-SEEPROM-AT24CS01-02-Datasheet.pdf*/

static int at24cs_eeprom_serial_read
(
  struct i2c_smbus_drv_data* i2c_drv_data_ptr,
  uint8_t *buf,
  unsigned offset,
  size_t count
)
{
   unsigned long timeout, read_time;
   struct i2c_client *client;
   struct i2c_msg msg[2];
   uint8_t addrbuf[2];
   int status;
#ifdef DEBUG_IT
   int retry_cnt=0;
#endif

client = i2c_drv_data_ptr->clients[1];

#ifdef DEBUG_IT
pr_info("here,addr=0x%x,off=%lu,count=%zu\n",client->addr,(long)offset,count);
#endif
   memset(msg, 0, sizeof(msg));
   msg[0].addr = client->addr;
   msg[0].buf = addrbuf;

   /*
    * The address pointer of the device is shared between the regular
    * EEPROM array and the serial number block. The dummy write (part of
    * the sequential read protocol) ensures the address pointer is reset
    * to the desired position.
    */
#if 0
   if (at24->chip_access_flags & AT24_FLAG_ADDR16)
        {
      /*
       * For 16 bit address pointers, the word address must contain
       * a '10' sequence in bits 11 and 10 regardless of the
       * intended position of the address pointer.
       */
      addrbuf[0] = 0x08;
      addrbuf[1] = offset;
      msg[0].len = 2; pr_info("not here\n");
   }
        else
#endif
        {
      /*
       * Otherwise the word address must begin with a '10' sequence,
       * regardless of the intended address.
       */
      addrbuf[0] = 0x80 + offset;
      msg[0].len = 1; pr_info("here:ddrbuf[0]=0x%x\n",addrbuf[0]);
   }

   msg[1].addr = client->addr; pr_info("msg[1].addr=0x%x\n",msg[1].addr);
   msg[1].flags = I2C_M_RD;
   msg[1].buf = buf;
   msg[1].len = count;

   /*
    * Reads fail if the previous write didn't complete yet. We may
    * loop a few times until this one succeeds, waiting at least
    * long enough for one entire page write to work.
    */
   timeout = jiffies + msecs_to_jiffies(i2c_drv_data_ptr->write_timeout);
#ifdef DEBUG_IT
   pr_info("write_timeout=%lu\n",i2c_drv_data_ptr->write_timeout);
#endif
   do
   {
      read_time = jiffies;
      status = i2c_transfer(client->adapter, msg, 2);
      if (status == 2)
      {
#ifdef DEBUG_IT
         pr_info("adapter=0x%p,status %d,retry_cnt=%d\n",client->adapter,status,retry_cnt);printk_hex_info(buf,count);
#endif
         return count;
      }

      /* REVISIT: at HZ=100, this is sloooow */
      msleep(1);
#ifdef DEBUG_IT
      pr_info("adapter=0x%p,time out %d\n",client->adapter,++retry_cnt);
#endif
   }
   while (time_before(read_time, timeout));

   return -ETIMEDOUT;
}
EXPORT_SYMBOL(at24cs_eeprom_serial_read);

int32_t nai_i2c_read_chip_serial
(
   struct i2c_smbus_drv_data* i2c_drv_data_ptr,
   const uint8_t* in_buf,
   uint16_t in_buf_len,
   uint8_t* out_buf,
   uint16_t out_buf_len
)
{
   int32_t result = 0;
   loff_t off=in_buf[SMBUS_RD_CMD_OFF];
   size_t cnt=in_buf[SMBUS_RD_LEN_OFF];

   struct i2c_client client;
   i2c_drv_data_ptr->clients[0] = &client;
   build_i2c_client_addr(i2c_drv_data_ptr->clients[0],in_buf);
#ifdef DEBUG_IT
   pr_info("client.addr=0x%x\n",i2c_drv_data_ptr->clients[0]->addr);
#endif

   i2c_drv_data_ptr->clients[1] = i2c_new_dummy
   (
     i2c_drv_data_ptr->i2c_adapter_ptr,
     AT24CS_SERIAL_ADDR(client.addr)
   );

   if ( i2c_drv_data_ptr->clients[1] == NULL )
   {
      pr_err("address 0x%02x unavailable\n", AT24CS_SERIAL_ADDR(client.addr));

      return result = -EADDRINUSE;
   }

   i2c_drv_data_ptr->clients[1]->adapter = i2c_drv_data_ptr->i2c_adapter_ptr;

   result = at24cs_eeprom_serial_read
   (
      i2c_drv_data_ptr,
      out_buf,
      off,
      cnt
   );

   i2c_unregister_device( i2c_drv_data_ptr->clients[1] );

   return result;
}
EXPORT_SYMBOL(nai_i2c_read_chip_serial);

/******************* nai i2c serial number read shared functions end *******************/
#endif /*#ifdef SUPPORT_I2C_DEVICE_FILE*/

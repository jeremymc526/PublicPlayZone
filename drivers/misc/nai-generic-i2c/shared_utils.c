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

#include "nai_generic_i2c.h"
#include "shared_utils.h"

#define RETRY_LIMIT          3

struct i2c_smbus_drv_data* create_dev_fs
(
   const char* dev_file_name,
   const char* parent_name,
   struct file_operations* file_ops,
   uint32_t i2c_addr_num,
   uint8_t chip_flags,
   int* status
)
{
   struct i2c_smbus_drv_data *i2c_drv_data_ptr;
   /*
    * devm_kzalloc() is resource-managed kzalloc(). The memory allocated with resource-managed
    * functions is associated with the device. When the device is detached from the system or the
    * driver for the device is unloaded, that memory is freed automatically. It is possible to
    * free the memory with devm_kfree() if it's no longer needed.
    * i2c_drv_data_ptr=devm_kzalloc(&pdev->dev,sizeof(*i2c_drv_data_ptr),GFP_KERNEL);
    */
   i2c_drv_data_ptr=kzalloc(sizeof(*i2c_drv_data_ptr)+ i2c_addr_num * sizeof(struct i2c_client *),GFP_KERNEL);

   if ( i2c_drv_data_ptr==NULL )
   {
      pr_err("kmalloc(struct i2c_smbus_drv_data,GFP_KERNEL) err\n");
      *status = -ENOMEM;
      goto err_step_1;
   }

   memset( i2c_drv_data_ptr, 0, sizeof(*i2c_drv_data_ptr) );
#ifdef DEBUG_IT
   pr_info("i2c_drv_data_ptr vir_addr=0x%p\n",i2c_drv_data_ptr);
#endif
                                                                 /*check man page, vs register_chrdev*/
   if ((*status = alloc_chrdev_region(&i2c_drv_data_ptr->first, 0, 1, parent_name)) < 0) /*listed in /proc/devices more than ones*/
   {
      pr_err("alloc_chrdev_region() err=%d\n",*status);
      goto err_step_2;
   }

   if (IS_ERR(i2c_drv_data_ptr->generic_i2c_class = class_create(THIS_MODULE, dev_file_name)))
   {
      pr_err("class_create() err=%ld for file \"%s\"\n",PTR_ERR(i2c_drv_data_ptr->generic_i2c_class),dev_file_name);
      *status=PTR_ERR(i2c_drv_data_ptr->generic_i2c_class);
      goto err_step_3;
   }

   if (IS_ERR(device_create(i2c_drv_data_ptr->generic_i2c_class, NULL, i2c_drv_data_ptr->first, NULL, dev_file_name)))
   {
      *status = PTR_ERR(device_create(i2c_drv_data_ptr->generic_i2c_class, NULL, i2c_drv_data_ptr->first, NULL, dev_file_name));
      pr_err("device_create() err=%d, for file \"%s\"\n",*status,dev_file_name);
      goto err_step_4;
   }

#ifdef DEBUG_IT
   pr_info("device file \"%s\" created\n",dev_file_name);
#endif
   /* c_dev is the member of i2c_drv_data_ptr points to, the address of i2c_drv_data_ptr can be gotten from address of c_dev with container_of(...)*/
   cdev_init(&i2c_drv_data_ptr->c_dev,file_ops); /*make the link from address of struct cdev to address of struct struct i2c_smbus_drv_data*/
   if ((*status = cdev_add(&i2c_drv_data_ptr->c_dev, i2c_drv_data_ptr->first, 1)) < 0)
   {
      pr_err("cdev_add() err=%d\n",*status);
      goto err_step_5;
   }

   i2c_drv_data_ptr->io_limit = g_io_limit;
   i2c_drv_data_ptr->write_timeout = g_write_timeout;
   i2c_drv_data_ptr->chip_access_flags |= chip_flags; 
   i2c_drv_data_ptr->num_addresses=i2c_addr_num;
   return i2c_drv_data_ptr;

err_step_5:
   device_destroy(i2c_drv_data_ptr->generic_i2c_class, i2c_drv_data_ptr->first);
err_step_4:
   class_destroy(i2c_drv_data_ptr->generic_i2c_class);
err_step_3:
   unregister_chrdev_region(i2c_drv_data_ptr->first,1);
err_step_2:
   kfree(i2c_drv_data_ptr);
err_step_1:
   return NULL;
}
EXPORT_SYMBOL(create_dev_fs);

void delete_char_dev( struct i2c_smbus_drv_data* i2c_smbus_drv_data_ptr )
{
   cdev_del(&i2c_smbus_drv_data_ptr->c_dev);
   device_destroy(i2c_smbus_drv_data_ptr->generic_i2c_class, i2c_smbus_drv_data_ptr->first);
   class_destroy(i2c_smbus_drv_data_ptr->generic_i2c_class);
   unregister_chrdev_region(i2c_smbus_drv_data_ptr->first,1);
   kfree(i2c_smbus_drv_data_ptr);
}
EXPORT_SYMBOL(delete_char_dev);

void build_i2c_client_addr(struct i2c_client* i2c_cli,const uint8_t* in_buf)
{
   i2c_cli->flags = 0; /*bug fix for sending invalid I2C slave address*/

   /*little-endian*/
   i2c_cli->addr = in_buf[SMBUS_WR_ADDR_EXT_OFF];
   i2c_cli->addr <<= 8;
   i2c_cli->addr |= in_buf[SMBUS_WR_ADDR_OFF];

   if ( in_buf[ SMBUS_RD_ADDR_EXT_OFF ] & 0xFF )
   {
      i2c_cli->flags |= I2C_M_TEN;
   }
}
EXPORT_SYMBOL(build_i2c_client_addr);

//#ifdef DEBUG_IT
void printk_hex_info(const uint8_t* hex_info,unsigned int info_size)
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
EXPORT_SYMBOL(printk_hex_info);
//#endif /* #ifdef DEBUG_IT */

uint32_t the_max( uint32_t a, uint32_t b )
{
   return a >= b ? a : b;
}
EXPORT_SYMBOL(the_max);

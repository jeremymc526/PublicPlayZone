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
 * 11-29-2016 JinH first release
 * ==========================================================================*/

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
#include "shared_i2c.h"
#include "shared_smbus.h"

#define PROBE_NAME        "nai-eeprom-i2c"

#ifdef SUPPORT_I2C_DEVICE_FILE
#define I2C_DEVICE_FILE   "i2c-adapter-0-i2c"
#endif/*#ifdef SUPPORT_I2C_DEVICE_FILE*/
#define SMBUS_DEVICE_FILE "i2c-adapter-0-smbus"

#define NUM_ADDRESSES 2 //TBD for serial num

struct drv_data
{
   struct mutex *shared_mutex;
#ifdef SUPPORT_I2C_DEVICE_FILE
   /*to create the device /dev/i2c-adapter-0-i2c for future's enhancement*/
   struct i2c_smbus_drv_data* i2c_drv_data_ptr;
#endif/*#ifdef SUPPORT_I2C_DEVICE_FILE*/
   struct i2c_smbus_drv_data* smbus_drv_data_ptr;
};

static int nai_i2c_smbus_dev_open(struct inode *inode, struct file *filp) //TBD, combine another open func together when Intel board is added
{
   int status = 0;
   /*
    * setup filp->private_data for other funcs such as file_operations.read, file_operations.write and file_operations.ioctl to access
    * fields in struct i2c_smbus_drv_data
    */
   filp->private_data=(struct i2c_smbus_drv_data*)container_of(inode->i_cdev,struct i2c_smbus_drv_data,c_dev);
#ifdef DEBUG_IT
   pr_info("i2c_drv_data vir_addr=0x%p\n",filp->private_data);
   pr_info("((struct i2c_smbus_drv_data*)(filp->private_data))->mutex_ptr=0x%p\n",((struct i2c_smbus_drv_data*)(filp->private_data))->mutex_ptr);
   pr_info("file name \"%s\"\n",filp->f_path.dentry->d_iname);
#endif

   mutex_lock(((struct i2c_smbus_drv_data*)(filp->private_data))->mutex_ptr);
   if ( ((struct i2c_smbus_drv_data*)(filp->private_data))->user_cnt == 0 )
   {
      ((struct i2c_smbus_drv_data*)(filp->private_data))->i2c_adapter_ptr=i2c_get_adapter(I2C_ADAPTER_0);

      if ( ((struct i2c_smbus_drv_data*)(filp->private_data))->i2c_adapter_ptr == NULL )
      {
         pr_err("can't Attach I2C Adapter %d\n",I2C_ADAPTER_0);
         status = -ENODEV; /*no such device, include/uapi/asm-generic/errno-base.h*/
         goto done;
      }
#ifdef DEBUG_IT
      if ( i2c_check_functionality( ((struct i2c_smbus_drv_data*)(filp->private_data))->i2c_adapter_ptr, I2C_FUNC_SMBUS_PEC ) )
      {
         pr_info("PEC set\n");
      }
      else
      {
         pr_info("PEC not set\n");
      }
#endif
      ((struct i2c_smbus_drv_data*)(filp->private_data))->user_cnt++;
   }

done:
   mutex_unlock(((struct i2c_smbus_drv_data*)(filp->private_data))->mutex_ptr);

   return status ;
}

static int nai_i2c_smbus_dev_close(struct inode *i, struct file *filp)
{
   /* not safe in multi processor, set it to 0 only opened dev cnt is 0 */
   mutex_lock(((struct i2c_smbus_drv_data*)(filp->private_data))->mutex_ptr);
   ((struct i2c_smbus_drv_data*)(filp->private_data))->user_cnt--;
   if ( ((struct i2c_smbus_drv_data*)(filp->private_data))->user_cnt == 0 )
   {
      i2c_put_adapter(((struct i2c_smbus_drv_data*)(filp->private_data))->i2c_adapter_ptr);
   }

#ifdef DEBUG_IT
   pr_info("i2c_drv_data vir_addr=0x%p\n",filp->private_data);
   pr_info("file name \"%s\"\n",filp->f_path.dentry->d_iname);
   pr_info("user_cnt=%d\n",((struct i2c_smbus_drv_data*)(filp->private_data))->user_cnt);
#endif
   mutex_unlock(((struct i2c_smbus_drv_data*)(filp->private_data))->mutex_ptr);
   return 0;
}

#ifdef SUPPORT_I2C_DEVICE_FILE
static struct file_operations i2c_dev_file_ops =
{
   .owner =   THIS_MODULE,
   .open =    nai_i2c_smbus_dev_open,
   .release = nai_i2c_smbus_dev_close,
   .read =    nai_i2c_dev_read,
   .write =   nai_i2c_dev_write,
};
#endif /*#ifdef SUPPORT_I2C_DEVICE_FILE*/

static struct file_operations smbus_dev_file_ops =
{
   .owner =   THIS_MODULE,
   .open =    nai_i2c_smbus_dev_open,
   .release = nai_i2c_smbus_dev_close,
   .read =    nai_smbus_dev_read,
   .write =   nai_smbus_dev_write,
};

static int nai_gen_i2c_driver_probe(struct platform_device *pdev)
{
   int status=0;
   struct drv_data* drv_data_ptr;
#ifdef DEBUG_IT
   pr_info("Probe Device: \"%s\"\n",pdev->name);
#endif

    drv_data_ptr = kzalloc( sizeof( struct drv_data ), GFP_KERNEL );
    if( drv_data_ptr == NULL )
    {
       status= -ENOMEM;
       goto err_step_1;
    }

    drv_data_ptr->shared_mutex = kzalloc( sizeof( struct mutex ), GFP_KERNEL );
    if( drv_data_ptr->shared_mutex == NULL )
    {
       status= -ENOMEM;
       goto err_step_2;
    }

    mutex_init(drv_data_ptr->shared_mutex);
#ifdef SUPPORT_I2C_DEVICE_FILE
    drv_data_ptr->i2c_drv_data_ptr = create_dev_fs
    (
       I2C_DEVICE_FILE,
       "i2c-dev-parent",
       &i2c_dev_file_ops,
       NUM_ADDRESSES,
       0,/*AT24_FLAG_ADDR16,This flag depends on the chip's capacity and affects i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)*/
       &status
    );

    if( drv_data_ptr->i2c_drv_data_ptr == NULL )
    {
       goto err_step_3;
    }
#ifdef DEBUG_IT
   pr_info("Probe Device: \"%s\",I2C_DEVICE_FILE=\"%s\"\n",pdev->name,I2C_DEVICE_FILE);
#endif
#endif/*#ifdef SUPPORT_I2C_DEVICE_FILE*/
    drv_data_ptr->smbus_drv_data_ptr = create_dev_fs
    (
       SMBUS_DEVICE_FILE,
       "i2c-dev-parent",
       &smbus_dev_file_ops,
       NUM_ADDRESSES,
       0,
       &status
    );

    if( drv_data_ptr->smbus_drv_data_ptr == NULL )
    {
       goto err_step_4;
    }
#ifdef DEBUG_IT
   pr_info("Probe Device: \"%s\",SMBUS_DEVICE_FILE=\"%s\"\n",pdev->name,SMBUS_DEVICE_FILE);
#endif
#ifdef SUPPORT_I2C_DEVICE_FILE
   drv_data_ptr->i2c_drv_data_ptr->mutex_ptr = drv_data_ptr->shared_mutex;
#endif /*#ifdef SUPPORT_I2C_DEVICE_FILE*/
   drv_data_ptr->smbus_drv_data_ptr->mutex_ptr = drv_data_ptr->shared_mutex;

   platform_set_drvdata(pdev,drv_data_ptr);
   return 0;

err_step_4:
#ifdef SUPPORT_I2C_DEVICE_FILE
    kfree( drv_data_ptr->i2c_drv_data_ptr );
#endif/*#ifdef SUPPORT_I2C_DEVICE_FILE*/
#ifdef SUPPORT_I2C_DEVICE_FILE
err_step_3:
    kfree( drv_data_ptr->shared_mutex );
#endif
err_step_2:
   kfree(drv_data_ptr);
err_step_1:
   return status;
}
#if 0
void static delete_char_dev( struct i2c_smbus_drv_data* i2c_smbus_drv_data_ptr )
{
   cdev_del(&i2c_smbus_drv_data_ptr->c_dev);
   device_destroy(i2c_smbus_drv_data_ptr->generic_i2c_class, i2c_smbus_drv_data_ptr->first);
   class_destroy(i2c_smbus_drv_data_ptr->generic_i2c_class);
   unregister_chrdev_region(i2c_smbus_drv_data_ptr->first,1);
   kfree(i2c_smbus_drv_data_ptr);
}
#endif
static int nai_gen_i2c_driver_remove(struct platform_device *pdev)
{
   struct drv_data* drv_data_ptr=platform_get_drvdata(pdev);

   /*without release resource calls, kernel panic when the module is removed by rmmod cmd or is re inserted by insmod cmd.*/
#ifdef SUPPORT_I2C_DEVICE_FILE
   delete_char_dev( drv_data_ptr->i2c_drv_data_ptr );
#endif/*#ifdef SUPPORT_I2C_DEVICE_FILE*/
   delete_char_dev( drv_data_ptr->smbus_drv_data_ptr );

   mutex_destroy(drv_data_ptr->shared_mutex);
   kfree(drv_data_ptr->shared_mutex);

   kfree(drv_data_ptr);
#ifdef DEBUG_IT
   pr_info("Remove Device: \"%s\"\n",pdev->name);
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
   pr_info("Driver test init: iomem_resource.start=%u,iomem_resource.end=%u(0x%x)\n",iomem_resource.start,iomem_resource.end,iomem_resource.end);
#endif
   platform_device_register(&nai_gen_i2c_driver_device); /*device is diff from driver*/
   platform_driver_register(&nai_gen_i2c_driver_driver); /*dirver is diff from device*/
#ifdef DEBUG_IT
   pr_info("Driver test init: iomem_resource.start=%u,iomem_resource.end=%u(0x%x)\n",iomem_resource.start,iomem_resource.end,iomem_resource.end);
#endif
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

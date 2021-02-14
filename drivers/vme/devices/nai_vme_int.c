//#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define pr_fmt(fmt) "%s:%d>" fmt,strrchr(__FILE__,'/'),__LINE__
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/vme.h>

#include "ring_buffer.h" 
#include "nai_vme_int.h"
#include "nai_vme_usr.h"

//#define    DEBUG_IT


#ifdef DEBUG_IT
static void printk_hex_info(const char* func_name,int line_nu,const u8* hex_info,unsigned int info_size)
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
#endif /*DEBUG_IT*/

static int vme_int_dev_open(struct inode *inode, struct file *filp)
{
   /*
    * setup filp->private_data for other funcs such as file_operations.read, file_operations.write and file_operations.ioctl to access
    * fields in struct dev_file_mgr_info
    */
   filp->private_data=(struct dev_file_mgr_info*)container_of(inode->i_cdev,struct dev_file_mgr_info,char_dev);

   ((struct dev_file_mgr_info*)(filp->private_data))->device_file_opend=DEVICE_FILE_OPENED;
   ((struct dev_file_mgr_info*)(filp->private_data))->naii_int_ring_buf_mgr_ptr->rd_nonblock=(filp->f_flags&O_NONBLOCK)!=0;   //!=0->0,it is what we want

#ifdef DEV_FILE_WR_OP
   ((struct dev_file_mgr_info*)(filp->private_data))->naii_int_ring_buf_mgr_ptr->wr_nonblock=(filp->f_flags&O_NONBLOCK)!=0;
#endif /* DEV_FILE_WR_OP */

#ifdef DEBUG_IT
   pr_info("\tfile name:\"%s\",vir_addr:0x%p\n",filp->f_path.dentry->d_iname,filp->private_data);
   pr_info("((struct dev_file_mgr_info*)(filp->private_data))->naii_int_ring_buf_mgr_ptr->rd_nonblock=%d\n",((struct dev_file_mgr_info*)(filp->private_data))->naii_int_ring_buf_mgr_ptr->rd_nonblock);
#endif

   return nonseekable_open(inode, filp);
}

static int vme_int_dev_close(struct inode *i, struct file *filp)
{
#ifdef DEBUG_IT
   pr_info("\tfile name:\"%s\",vir_addr:0x%p\n",filp->f_path.dentry->d_iname,filp->private_data);
#endif
   ((struct dev_file_mgr_info*)(filp->private_data))->device_file_opend=!DEVICE_FILE_OPENED;
   return 0;
}
EXPORT_SYMBOL(vme_int_dev_close);

static ssize_t vme_int_dev_read(struct file *filp, char __user *usr_buf, size_t count, loff_t *off)
{
#ifdef DEBUG_IT
   pr_info("\tfile name:\"%s\",vir_addr:0x%p\n",filp->f_path.dentry->d_iname,filp->private_data);
#endif

   struct dev_file_mgr_info *dev_file_mgr_info_ptr=filp->private_data;

   return naii_ring_buffer_read(dev_file_mgr_info_ptr->naii_int_ring_buf_mgr_ptr,usr_buf,count);
}

#ifdef DEV_FILE_WR_OP
static ssize_t vme_int_dev_write(struct file* filp,const char __user *usr_buf,size_t count,loff_t* off)
{
   struct dev_file_mgr_info *dev_file_mgr_info_ptr=filp->private_data;
   return naii_ring_buffer_write(dev_file_mgr_info_ptr->naii_int_ring_buf_mgr_ptr,usr_buf,count);
#else /* !DEV_FILE_WR_OP */
static int vme_int_dev_write(struct dev_file_mgr_info* dev_file_mgr_info_ptr,const char* in_buf,size_t count)
{
   #ifdef DEBUG_IT
   pr_info("count:%d,dev_file_mgr_info_ptr:0x%p\n",count,dev_file_mgr_info_ptr);
   #endif /* DEBUG_IT */

   if(dev_file_mgr_info_ptr->device_file_opend!=DEVICE_FILE_OPENED)
   {
      pr_err("DEVICE FILE \"%s\" IS NOT OPENED!!!\n",dev_file_mgr_info_ptr->nai_vme_int_class->name);
      return -ERESTARTSYS;
   }

   return naii_ring_buffer_write(dev_file_mgr_info_ptr->naii_int_ring_buf_mgr_ptr,in_buf,count);
#endif /* DEV_FILE_WR_OP */
}

#ifdef DEV_FILE_WR_OP
/* TBD test it if it is uesed */
static int vme_int_fasync(int fd, struct file *filp, int mode)
{
   struct dev_file_mgr_info *dev_file_mgr_info_ptr=filp->private_data;
   return fasync_helper(fd, filp, mode, &dev_file_mgr_info_ptr->naii_int_ring_buf_mgr_ptr->async_queue);
}

static int vme_int_release(struct inode *inode, struct file *filp)
{
   struct dev_file_mgr_info *dev_file_mgr_info_ptr=filp->private_data;
   vme_int_fasync(-1,filp,0);
}
#endif /* DEV_FILE_WR_OP */

static struct file_operations nai_vme_int_dev_file_ops =
{
   .owner =   THIS_MODULE,
   .open =    vme_int_dev_open,
   .release = vme_int_dev_close,
   .read =    vme_int_dev_read,
#ifdef DEV_FILE_WR_OP
   .write =   vme_int_dev_write,
   .fasync =  vme_int_fasync,
   .release = vme_int_release,
#endif /* DEV_FILE_WR_OP */
};

static int delete_dev_fs(struct dev_file_mgr_info *dev_file_mgr_info_ptr)
{
   int status=0;

   device_destroy(dev_file_mgr_info_ptr->nai_vme_int_class, dev_file_mgr_info_ptr->first);

   class_destroy(dev_file_mgr_info_ptr->nai_vme_int_class);

   unregister_chrdev_region(dev_file_mgr_info_ptr->first,1);

   return status;
}

static int create_dev_fs
(
   struct dev_file_mgr_info* dev_file_mgr_info_ptr,
   const char* dev_file_name,
   const char* parent_name,
   struct file_operations* file_ops,
   short ring_buf_sz,
   int* status
)
{
   *status=0;
   memset( dev_file_mgr_info_ptr, 0, sizeof(*dev_file_mgr_info_ptr) );
#ifdef DEBUG_IT
   pr_info("dev_file_mgr_info_ptr vir_addr=0x%p\n",dev_file_mgr_info_ptr);
#endif

   if ((*status = alloc_chrdev_region(&dev_file_mgr_info_ptr->first, 0, 1, parent_name)) < 0) /*parent_name is in the file /proc/devices more than ones*/
   {
      pr_err("alloc_chrdev_region() err=%d\n",*status);
      goto err_step_1;
   }

   /* creating "/sys/class/dev_file_name/dev_file_name" and "/sys/class/dev_file_name" */
   if (IS_ERR(dev_file_mgr_info_ptr->nai_vme_int_class = class_create(THIS_MODULE, dev_file_name)))
   {
      pr_err("class_create() err=%ld for file \"%s\"\n",PTR_ERR(dev_file_mgr_info_ptr->nai_vme_int_class),dev_file_name);
      *status=PTR_ERR(dev_file_mgr_info_ptr->nai_vme_int_class);
      goto err_step_2;
   }

   if (IS_ERR(device_create(dev_file_mgr_info_ptr->nai_vme_int_class, NULL, dev_file_mgr_info_ptr->first, NULL, dev_file_name)))
   {
      *status = PTR_ERR(device_create(dev_file_mgr_info_ptr->nai_vme_int_class, NULL, dev_file_mgr_info_ptr->first, NULL, dev_file_name));
      pr_err("device_create() err=%d, for file \"%s\"\n",*status,dev_file_name);
      goto err_step_3;
   }

#ifdef DEBUG_IT
   pr_info("device file \"%s\" created\n",dev_file_name);
#endif
   /* char_dev is the member of dev_file_mgr_info_ptr points to, the address of dev_file_mgr_info_ptr can be gotten from address of char_dev with container_of(...)*/
   cdev_init(&dev_file_mgr_info_ptr->char_dev,file_ops); /*make the link from address of struct cdev to address of struct struct dev_file_mgr_info*/
   if ((*status = cdev_add(&dev_file_mgr_info_ptr->char_dev, dev_file_mgr_info_ptr->first, 1)) < 0)
   {
      pr_err("cdev_add() err=%d\n",*status);
      goto err_step_4;
   }

#ifdef DEV_FILE_WR_OP
   naii_ring_buffer_init(&dev_file_mgr_info_ptr->naii_int_ring_buf_mgr_ptr,ring_buf_sz,g_read_time_out_msecs,g_write_time_out_msecs);
#else /*!DEV_FILE_WR_OP*/
   naii_ring_buffer_init(&dev_file_mgr_info_ptr->naii_int_ring_buf_mgr_ptr,ring_buf_sz,g_read_time_out_msecs,g_write_time_out_msecs,g_non_block_wr_flag);
#endif /*!DEV_FILE_WR_OP*/
   return *status;

err_step_4:
   device_destroy(dev_file_mgr_info_ptr->nai_vme_int_class, dev_file_mgr_info_ptr->first);

err_step_3:
   class_destroy(dev_file_mgr_info_ptr->nai_vme_int_class);

err_step_2:
   unregister_chrdev_region(dev_file_mgr_info_ptr->first,1);

err_step_1:

   return *status;
}
/*EXPORT_SYMBOL(create_dev_fs);*/

int create_nai_vme_int_dev_file(struct dev_file_mgr_info** dev_file_mgr_info_ptr,int int_level,short ring_buf_sz,int* status)
{
   *status=0;

   *dev_file_mgr_info_ptr=kzalloc(sizeof(struct dev_file_mgr_info),GFP_KERNEL);

   if(*dev_file_mgr_info_ptr==NULL)
   {
      pr_err("kzalloc(sizeof(struct dev_file_mgr_info),GFP_KERNEL) err\n");
      *status = -ENOMEM;
      goto err_step_1;
   }

#ifdef DEBUG_IT
   pr_info("**dev_file_mgr_info_ptr=0x%p,*dev_file_mgr_info_ptr=0x%p\n",**dev_file_mgr_info_ptr,*dev_file_mgr_info_ptr);
#endif

   *status=create_dev_fs
           (
              *dev_file_mgr_info_ptr,
              kasprintf(GFP_KERNEL,"nai_vme_int_%d",int_level),
              "nai_vme_int_parent_name",
              &nai_vme_int_dev_file_ops,
              ring_buf_sz,
              status
           );

#ifdef DEBUG_IT
   pr_info("ring_buf_sz:%d\n",ring_buf_sz);
#endif

   if((*status)!=0)
   {
       goto err_step_2;
   }

#ifdef DEBUG_IT
   pr_info("*dev_file_mgr_info_ptr=0x%p\n",*dev_file_mgr_info_ptr);
#endif

   return *status;

err_step_2:
   kfree(*dev_file_mgr_info_ptr);

err_step_1:
   return *status;
}
EXPORT_SYMBOL(create_nai_vme_int_dev_file);

void vme_isr_bottom_half(struct work_struct* work_struct_ptr)
{
   int status=0;
   struct int_work_q* int_work_q_ptr=container_of(work_struct_ptr,struct int_work_q,work_q);

#ifdef DEBUG_IT
   pr_info("dev_file_mgr_info_ptr=0x%p\n",int_work_q_ptr->dev_file_mgr_info_ptr);

   printk(KERN_INFO"l:%d,v:0x%02x\n",int_work_q_ptr->int_level,int_work_q_ptr->int_vec);
#endif

#ifndef DEV_FILE_WR_OP
   status=vme_int_dev_write(int_work_q_ptr->dev_file_mgr_info_ptr,((char*)(&int_work_q_ptr->int_vec)),sizeof(int_work_q_ptr->int_vec));
#endif /*#ifndef DEV_FILE_WR_OP*/

   if (status==-EAGAIN)
   {
      pr_err("INT LEVEL<%d>RING BUFFER WITH <%d> BYTES SIZE IS FULL,\"int_vec:0x%02x\" lost!!!\n",int_work_q_ptr->int_level,int_work_q_ptr->dev_file_mgr_info_ptr->naii_int_ring_buf_mgr_ptr->ring_buf_sz,int_work_q_ptr->int_vec);
   }
#ifdef DEBUG_IT
   else
   {
      pr_info("vme_int_dev_write(...) successfully for \"int_vec:0x%02x\".\n",int_work_q_ptr->int_vec);
   }
#endif

  /*
   * The mem was allocated by the work queue's creator by calling
   * struct int_work_q* int_work_q_ptr=(struct int_work_q*)kmalloc(sizeof(struct int_work_q),GFP_ATOMIC);
   */
   kfree((void*)int_work_q_ptr);
}
EXPORT_SYMBOL(vme_isr_bottom_half);

int delete_nai_vme_int_dev_file(struct dev_file_mgr_info** dev_file_mgr_info_ptr,bool force)
{
   int status=0;

#ifdef DEBUG_IT
   pr_info("*dev_file_mgr_info_ptr:%p\n",*dev_file_mgr_info_ptr);
#endif
   if(*dev_file_mgr_info_ptr!=NULL)
   {
      if
      (
         (*dev_file_mgr_info_ptr)->device_file_opend==DEVICE_FILE_OPENED
         &&
         force!=true
      )
      {
        pr_err("can't delete dev file \"%s\", it is still opened\n",(*dev_file_mgr_info_ptr)->nai_vme_int_class->name); 
        status=-EBUSY;
      }
      else
      {
        naii_ring_buffer_release(&(*dev_file_mgr_info_ptr)->naii_int_ring_buf_mgr_ptr);
        delete_dev_fs(*dev_file_mgr_info_ptr);
        kfree(*dev_file_mgr_info_ptr);
      }
   }

   return status;
}
EXPORT_SYMBOL(delete_nai_vme_int_dev_file);

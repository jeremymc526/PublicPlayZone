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

//#define    DEBUG_IT

enum POINTER_TYPE
{
   WRITE_PTR,
   READ_PTR
};

#ifdef DEBUG_IT
static void printk_hex_info(const char* func_name,int line_nu,const u8* hex_info,unsigned int info_size);
static void check_ring_buffer(const char* calling_func,unsigned int calling_line,struct ring_buf_mgr* ring_buf_mgr_ptr);
#endif

static int ring_buf_data_size(struct ring_buf_mgr* ring_buf_mgr_ptr);
static int ring_buf_free_space(struct ring_buf_mgr* ring_buf_mgr_ptr);
static int ring_buf_get_linux_read_available_size(struct ring_buf_mgr* ring_buf_mgr_ptr);
static int ring_buf_get_linux_write_available_size(struct ring_buf_mgr* ring_buf_mgr_ptr);
static void ring_buf_modulus_ptr(struct ring_buf_mgr* ring_buf_mgr_ptr,size_t delta,char** access_ptr);
static void ring_buf_increase_ptr(struct ring_buf_mgr* ring_buf_mgr_ptr,size_t delta,enum POINTER_TYPE pointer_type);
#ifdef DEV_FILE_WR_OP
static int ring_buf_wrap_copy_from_user(struct ring_buf_mgr* ring_buf_mgr_ptr,const char __user *buf,size_t count);
#else /* !DEV_FILE_WR_OP */
static int ring_buf_wrap_kernel_copy(struct ring_buf_mgr* ring_buf_mgr_ptr,const char *buf,size_t count);
#endif /* DEV_FILE_WR_OP */
#define CHECK_BUFFER(RING_BUF_PTR) /*check_ring_buffer(__func__,__LINE__,RING_BUF_PTR)*/

/**************************************************************************************************************************
           ********* public functins implemetations *********
**************************************************************************************************************************/
#ifdef DEV_FILE_WR_OP
int naii_ring_buffer_init(struct ring_buf_mgr **ring_buf_mgr_ptr,short ring_buf_sz,int rd_time_out,int wr_time_out)
#else /*!DEV_FILE_WR_OP*/
int naii_ring_buffer_init(struct ring_buf_mgr **ring_buf_mgr_ptr,short ring_buf_sz,int rd_time_out,int wr_time_out,bool non_block_wr)
#endif /*DEV_FILE_WR_OP*/
{
  int status=0;

  *ring_buf_mgr_ptr=kmalloc(sizeof(struct ring_buf_mgr),GFP_KERNEL);
  if(*ring_buf_mgr_ptr==NULL)
  {
     pr_err("kmalloc(izeof(struct ring_buf_mgr)GFP_KERNEL) err!!!\n");
     status= -ENOMEM;
     goto func_exit;
  }

  memset(*ring_buf_mgr_ptr,0,sizeof(struct ring_buf_mgr));

#ifdef DEBUG_IT
  pr_info("*ring_buf_mgr_ptr:0x%p\n",*ring_buf_mgr_ptr);
#endif

  init_waitqueue_head(&(*ring_buf_mgr_ptr)->rd_wait_q);
  init_waitqueue_head(&(*ring_buf_mgr_ptr)->wr_wait_q);

  sema_init(&(*ring_buf_mgr_ptr)->sem,1);

  (*ring_buf_mgr_ptr)->ring_buf_sz=ring_buf_sz;

  (*ring_buf_mgr_ptr)->read_time_out_msecs = rd_time_out;
  (*ring_buf_mgr_ptr)->write_time_out_msecs = wr_time_out;

  (*ring_buf_mgr_ptr)->ring_buf=kmalloc((*ring_buf_mgr_ptr)->ring_buf_sz,GFP_KERNEL);
  if((*ring_buf_mgr_ptr)->ring_buf==NULL)
  {
     kfree(*ring_buf_mgr_ptr);
     pr_err("kmalloc((*ring_buf_mgr_ptr)->ring_buf_sz,GFP_KERNEL) err!!!\n");
     status= -ENOMEM;
     goto func_exit;
  }

#ifdef DEBUG_IT
  pr_info("(*ring_buf_mgr_ptr)->ring_buf:0x%p\n",(*ring_buf_mgr_ptr)->ring_buf);
#endif

  memset((*ring_buf_mgr_ptr)->ring_buf,0,(*ring_buf_mgr_ptr)->ring_buf_sz);
  (*ring_buf_mgr_ptr)->wrap_flag&=~SET_WRAP_FLAG;
  (*ring_buf_mgr_ptr)->rd_ptr=(*ring_buf_mgr_ptr)->wr_ptr=(*ring_buf_mgr_ptr)->ring_buf; // rd and wr from the beginning

#ifndef DEV_FILE_WR_OP
  /* to keep logic simple and to avoid creating large work queue when user space read op is suspended, set write op to non-block mode */
  //dev_file_mgr_info_ptr->int_mgr_ring_buf.wr_nonblock=NON_BLOCK_WR_MODE;
  (*ring_buf_mgr_ptr)->rd_nonblock=non_block_wr;
#endif /* DEV_FILE_WR_OP */

func_exit:
  return status;
}
EXPORT_SYMBOL(naii_ring_buffer_init);

int naii_ring_buffer_release(struct ring_buf_mgr** ring_buf_mgr_ptr)
{
   int status=0;
#ifdef DEBUG_IT
   pr_info("passed test kfree, (*ring_buf_mgr_ptr)->ring_buf:%p\n",(*ring_buf_mgr_ptr)->ring_buf);
#endif
   kfree((*ring_buf_mgr_ptr)->ring_buf);
#ifdef DEBUG_IT
   pr_info("passed test kfree, (*ring_buf_mgr_ptr)->ring_buf:%p OK\n",(*ring_buf_mgr_ptr)->ring_buf);
#endif

#ifdef DEBUG_IT
   pr_info("passed test kfree, *ring_buf_mgr_ptr:%p\n",*ring_buf_mgr_ptr);
#endif
   kfree(*ring_buf_mgr_ptr);
#ifdef DEBUG_IT
   pr_info("passed test kfree, *ring_buf_mgr_ptr:%p OK\n",*ring_buf_mgr_ptr);
#endif
   return status;
}
EXPORT_SYMBOL(naii_ring_buffer_release);

int naii_ring_buffer_read(struct ring_buf_mgr* ring_buf_mgr_ptr,char __user *usr_buf, size_t count)
{
   int result;
   if(count==0)
      return 0;
//pr_info("count=%d\n",count);
   if(down_interruptible(&ring_buf_mgr_ptr->sem))
      return -ERESTARTSYS;

   result=ring_buf_get_linux_read_available_size(ring_buf_mgr_ptr);
   if(result<0)
   {
      return result;
   }
   CHECK_BUFFER(ring_buf_mgr_ptr);

   if(count>result) //read all data in current buffer
   {
      count=result; 
   }

   if(ring_buf_mgr_ptr->wr_ptr>ring_buf_mgr_ptr->rd_ptr)
   {
#if 0
              --------------- <-ring_buf
              .             .
              .             .
              .             .
            - --------------- <-rd_ptr
            ^ ./////////////.
            | ./////////////.
        count ./////////////.
            | ./////////////.
            v ./////////////.
            - ---------------
              ./////////////.
              ./////////////.
              --------------- <-wr_ptr
              .             .
              .             .
              .             .
              --------------- <-ring_buf+ring_buf_sz
pattern "/" is existing data in the buffer
#endif//#if 0

      if(copy_to_user(usr_buf,ring_buf_mgr_ptr->rd_ptr,count)!=0)
      {
         up(&ring_buf_mgr_ptr->sem);
         return -EFAULT;
      }

#ifdef DEBUG_IT
printk_hex_info(__func__,__LINE__,ring_buf_mgr_ptr->rd_ptr,count);
#endif
   CHECK_BUFFER(ring_buf_mgr);
      ring_buf_increase_ptr(ring_buf_mgr_ptr,count,READ_PTR);
   CHECK_BUFFER(ring_buf_mgr);
   }
   else//(ring_buf_mgr_ptr->wr_ptr<=ring_buf_mgr_ptr->rd_ptr),wr_ptr wrapped and rd_ptr not wrapped
   {
      if((size_t)(ring_buf_mgr_ptr->ring_buf+ring_buf_mgr_ptr->ring_buf_sz-ring_buf_mgr_ptr->rd_ptr)>=count)
      {
#ifdef DEBUG_IT
         pr_info("rd_ptr idx=%lu,count=%lu\n",(size_t)(ring_buf_mgr_ptr->ring_buf+ring_buf_mgr_ptr->ring_buf_sz-ring_buf_mgr_ptr->rd_ptr),count);
#endif
#if 0
              --------------- <-ring_buf
              ./////////////.
              ./////////////.
              ./////////////.
              --------------- <-wr_ptr
              .             .
              .             .
            - --------------- <-rd_ptr
            ^ ./////////////.
            | ./////////////.
        count ./////////////.
            | ./////////////.
            v ./////////////.
            - ---------------
              ./////////////.
              ./////////////.
              ./////////////.
              --------------- <-ring_buf+buffer_size
pattern "/" is existing data in the buffer
#endif//#if 0

         if(copy_to_user(usr_buf,ring_buf_mgr_ptr->rd_ptr,count)!=0)
         {
            up(&ring_buf_mgr_ptr->sem);
            return -EFAULT;
         }

         ring_buf_increase_ptr(ring_buf_mgr_ptr,count,READ_PTR);
      }
      else
      {
         size_t part_1=ring_buf_mgr_ptr->ring_buf+ring_buf_mgr_ptr->ring_buf_sz-ring_buf_mgr_ptr->rd_ptr;
#ifdef DEBUG_IT
         pr_info("(capacity-rd_ptr)=%lu,count=%lu\n",(size_t)(ring_buf_mgr_ptr->ring_buf+ring_buf_mgr_ptr->ring_buf_sz-ring_buf_mgr_ptr->rd_ptr),count);
#endif
#if 0
              --------------- <-ring_buf
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              --------------- <-wr_ptr
              .             .
              .             .
            - --------------- <-rd_ptr
            ^ ./////////////.
            | ./////////////.
       part_1 ./////////////.
            | ./////////////.
            v ./////////////.
            - --------------- <-ring_buf+ring_buf_sz
pattern "/" is existing data in the ring_buf
#endif//#if 0

            if(copy_to_user(usr_buf,ring_buf_mgr_ptr->rd_ptr,part_1)!=0)
            {
               up(&ring_buf_mgr_ptr->sem);
               return -EFAULT;
            }
            ring_buf_increase_ptr(ring_buf_mgr_ptr,part_1,READ_PTR);
#if 0
            - --------------- <-ring_buf
            ^ ./////////////.
            | ./////////////.
            | ./////////////.
 count-part_1 ./////////////.
            | ./////////////.
            v ./////////////.
            - .-------------.
              ./////////////.
              ./////////////.
              ./////////////.
              --------------- <-wr_ptr
              .             .
              .             .
            - --------------- <-rd_ptr
            ^ ./////////////.
            | ./////////////.
       part_1 ./////////////.
            | ./////////////.
            v ./////////////.
            - --------------- <-ring_buf+ring_buf_sz
pattern "/" is existing data in the ring_buf
#endif//#if 0

            if(copy_to_user(usr_buf+part_1,ring_buf_mgr_ptr->ring_buf,count-part_1)!=0)
            {
               up(&ring_buf_mgr_ptr->sem);
               return -EFAULT;
            }
            ring_buf_increase_ptr(ring_buf_mgr_ptr,count-part_1,READ_PTR);
      }
   }

   up(&ring_buf_mgr_ptr->sem);

   // finally, awake any writers and return
   wake_up_interruptible(&ring_buf_mgr_ptr->wr_wait_q);

#ifdef DEBUG_IT
   pr_info("\"%s\" did read %li bytes\n",current->comm,(long)count);
#endif
/////////////////////////////////////////////////////////////////////////////////////////

   return count;
}
EXPORT_SYMBOL(naii_ring_buffer_read);

#ifdef DEV_FILE_WR_OP
int naii_ring_buffer_write(struct ring_buf_mgr* ring_buf_mgr_ptr,const char __user *usr_buf,size_t count)
{
#else /*!DEV_FILE_WR_OP*/
int naii_ring_buffer_write(struct ring_buf_mgr* ring_buf_mgr_ptr,const char *in_buf,size_t count)
{
#ifdef DEBUG_IT
   pr_info("count:%d,ring_buf_mgr_ptr:0x%p\n",count,ring_buf_mgr_ptr);
#endif /* DEBUG_IT */
#endif /* DEV_FILE_WR_OP */
   int result;

   if(count==0)
   {
      return 0;
   }

   if(down_interruptible(&ring_buf_mgr_ptr->sem))
   {
      pr_err("down_interruptible(&ring_buf_mgr_ptr->sem) err!!!\n");
      return -ERESTARTSYS;
   }

   result=ring_buf_get_linux_write_available_size(ring_buf_mgr_ptr);

   if(result<0)
   {
      //if result less than 0, the sem was released by ring_buf_get_linux_write_available_size(struct ring_buf_mgr*)
      pr_err("no wrte_space,result:%d\n",result);
      return result; 
   }

#ifdef DEBUG_IT
   pr_info("count=%lu,freespace=%d,rd_ptr=%u,wr_ptr=%u\n",count,result,ring_buf_mgr_ptr->rd_ptr-ring_buf_mgr_ptr->ring_buf,ring_buf_mgr_ptr->wr_ptr-ring_buf_mgr_ptr->ring_buf);
#endif
   if(count>result)
   {
      up(&ring_buf_mgr_ptr->sem);
//#ifdef DEBUG_IT
      pr_err("\"%s\" DID NOT WRITE %li BYTES DUE TO NO ENOUGH SPACE\n",current->comm,(long)count);
//#endif
      return -EAGAIN; //PAY MORE ATTENTION IN HERE
   }

   if(ring_buf_mgr_ptr->wr_ptr>=ring_buf_mgr_ptr->rd_ptr)
   {
      if(ring_buf_mgr_ptr->ring_buf+ring_buf_mgr_ptr->ring_buf_sz-ring_buf_mgr_ptr->wr_ptr>count)
      {
#if 0
              --------------- <-ring_buf
              .             .
              .             .
              .             .
            - .-------------. <-wr_ptr=wr_ptr
            ^ .             .
            | .             .
        count .             .
            | .             .
            v .             .
            - .             .
              .             .
              --------------- <-ring_buf+ring_buf_sz
the current buffer is empty

or
              --------------- <-ring_buf
              .             .
              .             .
              .             .
              --------------- <-rd_ptr
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
            - --------------- <-wr_ptr
            ^ .+++++++++++++.
            | .+++++++++++++.
        count .+++++++++++++.
            | .+++++++++++++.
            v .+++++++++++++.
            - .+++++++++++++.
              .             .
              .             .
              .             .
              --------------- <-ring_buf+ring_buf_sz

the current buffer is not empty
pattern "/" is existing data in the buffer
pattern "+" is new inserted data in the buffer
#endif//#if 0

         CHECK_BUFFER(ring_buf_mgr_ptr);
#ifdef DEV_FILE_WR_OP
         if(copy_from_user(ring_buf_mgr_ptr->wr_ptr,usr_buf,count)!=0)
#else /* !DEV_FILE_WR_OP */
         if(memcpy(ring_buf_mgr_ptr->wr_ptr,in_buf,count)!=ring_buf_mgr_ptr->wr_ptr)
#endif /* DEV_FILE_WR_OP */
         {
            pr_err("ring_buf_mgr_ptr->wr_ptr:0x%p,ring_buf_mgr_ptr->ring_buf:0x%p\n",ring_buf_mgr_ptr->wr_ptr,ring_buf_mgr_ptr->ring_buf);
#ifdef DEV_FILE_WR_OP
            pr_err("(copy_from_user(ring_buf_mgr_ptr->wr_ptr,usr_buf,count) err!!!\n");
#else /* !DEV_FILE_WR_OP */
            pr_err("(memcpy(ring_buf_mgr_ptr->wr_ptr,in_buf,count) err!!!\n");
#endif /* DEV_FILE_WR_OP */
            up(&ring_buf_mgr_ptr->sem);
            return -EFAULT;
         }
         ring_buf_increase_ptr(ring_buf_mgr_ptr,count,WRITE_PTR);
         CHECK_BUFFER(ring_buf_mgr_ptr);
      }
      else
      {
#if 0
            - --------------- <-ring_buf
            ^ .             .
            | .             .
      count_2 .             .
            | .             .
            v .             .
            - .-------------. <-wr_ptr=wr_ptr
            ^ .             .
            | .             .
      count_1 .             .
            | .             .
            v .             .
            - --------------- <-ring_buf+ring_buf_sz
the current buffer is empty, and write_count is equal to or less than the buffer capacity
count=count_1+count2

or
            - --------------- <-ring_buf
            ^ .+++++++++++++.
            | .+++++++++++++.
      count_2 .+++++++++++++.
            | .+++++++++++++.
            v .+++++++++++++.
            - ---------------
              .             .
              .             .
              .             .
              --------------- <-rd_ptr
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
            - --------------- <-wr_ptr
            ^ .+++++++++++++.
            | .+++++++++++++.
      count_1 .+++++++++++++.
            | .+++++++++++++.
            v .+++++++++++++.
            - --------------- <-ring_buf+ring_buf_sz
the current buffer is not empty
count=count_1+count2
pattern "/" is existing data in the buffer
pattern "+" is new inserted data in the buffer
#endif//#if 0

#ifdef DEV_FILE_WR_OP
         int sub_result=ring_buf_wrap_copy_from_user(ring_buf_mgr_ptr,usr_buf,count);
#else /* !DEV_FILE_WR_OP */
         int sub_result=ring_buf_wrap_kernel_copy(ring_buf_mgr_ptr,in_buf,count);
#endif /* DEV_FILE_WR_OP */
         if(sub_result!=0)
         {
           /*ring_buf_wrap_copy_from_user/ring_buf_wrap_kernel_copy(ring_buf_mgr_ptr,in_buf,count) already released semaphore if any error happened*/
#ifdef DEV_FILE_WR_OP
           pr_err("ring_buf_wrap_copy_from_user(ring_buf_mgr_ptr,usnr_buf,count) err.\n");
#else /* !DEV_FILE_WR_OP */
           pr_err("ring_buf_wrap_kernel_copy(ring_buf_mgr_ptr,in_buf,count) err.\n");
#endif /* DEV_FILE_WR_OP */
           return sub_result;
         }
      }
   }
   else
   {
#if 0
              --------------- <-ring_buf
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
            - --------------- <-wr_ptr
            ^ .+++++++++++++.
            | .+++++++++++++.
        count .+++++++++++++.
            | .+++++++++++++.
            v .+++++++++++++.
            - .+++++++++++++. 
              .             .
              .             .
              .             .
              --------------- <-rd_ptr
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              ./////////////.
              --------------- <-ring_buf+ring_buf_sz

pattern "/" is existing data in the buffer
pattern "+" is new inserted data in the buffer
#endif//#if 0

      CHECK_BUFFER(ring_buf_mgr_ptr);
#ifdef DEV_FILE_WR_OP
      if(copy_from_user(ring_buf_mgr_ptr->wr_ptr,usr_buf,count)!=0)
#else /* !DEV_FILE_WR_OP */
      if(memcpy(ring_buf_mgr_ptr->wr_ptr,in_buf,count)!=ring_buf_mgr_ptr->wr_ptr)
#endif /* DEV_FILE_WR_OP */
      {
         up(&ring_buf_mgr_ptr->sem);
#ifdef DEV_FILE_WR_OP
         pr_err("copy_from_user err.\n");
#else /* !DEV_FILE_WR_OP */
         pr_err("memcpy err.\n");
#endif /* DEV_FILE_WR_OP */
         return -EFAULT;
      }
      ring_buf_increase_ptr(ring_buf_mgr_ptr,count,WRITE_PTR);
   }

   CHECK_BUFFER(ring_buf_mgr_ptr);
   up(&ring_buf_mgr_ptr->sem);

   // finally, awake any reader
   wake_up_interruptible(&ring_buf_mgr_ptr->rd_wait_q);  // blocked in read() and select()

#ifdef DEV_FILE_WR_OP
   // TBD and signal asynchronous readers, explained in chapter 5
   if(ring_buf_mgr_ptr->async_queue)
   {
      kill_fasync(&ring_buf_mgr_ptr->async_queue,SIGIO,POLL_IN);
   }
#endif /* DEV_FILE_WR_OP */

#ifdef DEBUG_IT
   pr_info("\"%s\" did write %li bytes.\n",current->comm,(long)count);
#endif

   return result;
}
EXPORT_SYMBOL(naii_ring_buffer_write);

/**************************************************************************************************************************
 *                         static/private functions
 ***************************************************************************************************************************/
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

static void check_ring_buffer(const char* calling_func,unsigned int calling_line,struct ring_buf_mgr* ring_buf_mgr_ptr)
{
   printk(KERN_INFO"\n%s:%u:",calling_func,calling_line);
   printk(KERN_INFO"free_space=%u,data_size=%u,",ring_buf_free_space(ring_buf_mgr_ptr),ring_buf_data_size(ring_buf_mgr_ptr));
   printk(KERN_INFO"rd_ptr_idx=%u,wr_ptr_idx=%u\n",ring_buf_mgr_ptr->rd_ptr-ring_buf_mgr_ptr->ring_buf,ring_buf_mgr_ptr->wr_ptr-ring_buf_mgr_ptr->    ring_buf);
   printk_hex_info(NULL,calling_line,ring_buf_mgr_ptr->ring_buf,ring_buf_mgr_ptr->ring_buf_sz);
}
#endif

static void ring_buf_modulus_ptr(struct ring_buf_mgr* ring_buf_mgr_ptr,size_t delta,char** access_ptr)
{
   delta+=(*access_ptr-ring_buf_mgr_ptr->ring_buf);
   *access_ptr=ring_buf_mgr_ptr->ring_buf+delta%ring_buf_mgr_ptr->ring_buf_sz;
#ifdef DEBUG_IT
   pr_info("access_inx=%lu,delta=%lu\n",*access_ptr-ring_buf_mgr_ptr->ring_buf,delta);
#endif
}

//the caller must make sure the buffer is not overflow after the write/read pointer is modified
//by checking the writen size is less than free size and read size is less than data size in the buffer
static void ring_buf_increase_ptr(struct ring_buf_mgr* ring_buf_mgr_ptr,size_t delta,enum POINTER_TYPE pointer_type)
{
   if(pointer_type==WRITE_PTR)
   {
      if(ring_buf_mgr_ptr->wr_ptr+delta>=ring_buf_mgr_ptr->ring_buf+ring_buf_mgr_ptr->ring_buf_sz)
      {
         ring_buf_mgr_ptr->wrap_flag|=SET_WRAP_FLAG;
      }
      ring_buf_modulus_ptr(ring_buf_mgr_ptr,delta,&ring_buf_mgr_ptr->wr_ptr);
#ifdef DEBUG_IT
      pr_info("wr_ptr_idx=%lu,delta=%lu\n",ring_buf_mgr_ptr->wr_ptr-ring_buf_mgr_ptr->ring_buf,delta);
#endif
   }
   else
   {
      if(ring_buf_mgr_ptr->rd_ptr+delta>=ring_buf_mgr_ptr->ring_buf+ring_buf_mgr_ptr->ring_buf_sz)
      {
         ring_buf_mgr_ptr->wrap_flag&=~SET_WRAP_FLAG;
      }
      ring_buf_modulus_ptr(ring_buf_mgr_ptr,delta,&ring_buf_mgr_ptr->rd_ptr);
#ifdef DEBUG_IT
      pr_info("rd_ptr_idx=%lu,delta=%lu\n",ring_buf_mgr_ptr->rd_ptr-ring_buf_mgr_ptr->ring_buf,delta);
#endif
   }
}

static int ring_buf_free_space(struct ring_buf_mgr* ring_buf_mgr_ptr)
{
   int result=0;
   if(ring_buf_mgr_ptr->rd_ptr==ring_buf_mgr_ptr->wr_ptr)//if the buffer is full ring_buf_mgr_ptr->rd_ptr is also equal to ring_buf_mgr_ptr->wr_ptr
   {
      if(ring_buf_mgr_ptr->wrap_flag==SET_WRAP_FLAG)
      {
         result=0;
      }
      else
      {
         result=ring_buf_mgr_ptr->ring_buf_sz;
      }
   }
   else
   {
      /// modulus % is an effective way to calculate buffer size
      /// an easy way to think this algorithm: 
      /// if wr_ptr>rd_ptr: ring_buf_mgr_ptr->buffer_size-ring_buf_mgr_ptr->wr_ptr+ring_buf_mgr_ptr->rd_ptr
      /// if rd_ptr>wr_ptr: ring_buf_mgr_ptr->rd_ptr-ring_buf_mgr_ptr->wr_ptr+ring_buf_mgr_ptr->buffer_size
      /// TBD drawing a picture
      result=(ring_buf_mgr_ptr->rd_ptr+ring_buf_mgr_ptr->ring_buf_sz-ring_buf_mgr_ptr->wr_ptr)%ring_buf_mgr_ptr->ring_buf_sz;
   }

   return result; 
}

static int ring_buf_data_size(struct ring_buf_mgr* ring_buf_mgr_ptr)
{
   return ring_buf_mgr_ptr->ring_buf_sz-ring_buf_free_space(ring_buf_mgr_ptr);
}

#ifdef DEV_FILE_WR_OP
int ring_buf_wrap_copy_from_user(struct ring_buf_mgr* ring_buf_mgr_ptr,const char __user *buf,size_t count)
#else /* DEV_FILE_WR_OP */
int ring_buf_wrap_kernel_copy(struct ring_buf_mgr* ring_buf_mgr_ptr,const char *buf,size_t count)
#endif /* DEV_FILE_WR_OP */
{
   size_t count_1=ring_buf_mgr_ptr->ring_buf+ring_buf_mgr_ptr->ring_buf_sz-ring_buf_mgr_ptr->wr_ptr;

#ifdef DEV_FILE_WR_OP
   if(copy_from_user(ring_buf_mgr_ptr->wr_ptr,buf,count_1)!=0)
#else /* DEV_FILE_WR_OP */
   if(memcpy(ring_buf_mgr_ptr->wr_ptr,buf,count_1)!=ring_buf_mgr_ptr->wr_ptr)
#endif /* DEV_FILE_WR_OP */
   {
      up(&ring_buf_mgr_ptr->sem);
      return -EFAULT;
   }

   ring_buf_increase_ptr(ring_buf_mgr_ptr,count_1,WRITE_PTR);

#ifdef DEV_FILE_WR_OP
   if(copy_from_user(ring_buf_mgr_ptr->wr_ptr,buf+count_1,count-count_1)!=0)
#else /* DEV_FILE_WR_OP */
   if(memcpy(ring_buf_mgr_ptr->wr_ptr,buf+count_1,count-count_1)!=ring_buf_mgr_ptr->wr_ptr)
#endif /* DEV_FILE_WR_OP */
   {
      up(&ring_buf_mgr_ptr->sem);
      return -EFAULT;
   }
   ring_buf_increase_ptr(ring_buf_mgr_ptr,count-count_1,WRITE_PTR);
   return 0;
}

// wait for space for writing; caller must hold device semaphore.
// on error the semaphore will be released before returning.
static int ring_buf_get_linux_read_available_size(struct ring_buf_mgr* ring_buf_mgr_ptr)
{
   int result=0;
   while((result=ring_buf_data_size(ring_buf_mgr_ptr))==0)
   { // nothing to read
      int wait_result;
      up(&ring_buf_mgr_ptr->sem); // release the lock
      //if(filp->f_flags&O_NONBLOCK)
      if(ring_buf_mgr_ptr->rd_nonblock==true)
      {
         return -EAGAIN;
      }

#ifdef DEBUG_IT
      pr_info("\"%s\" reading: going to wait queue\n", current->comm);
#endif

      if(ring_buf_mgr_ptr->read_time_out_msecs==WAIT_FOREVER)
      {
         if(wait_event_interruptible(ring_buf_mgr_ptr->rd_wait_q,ring_buf_data_size(ring_buf_mgr_ptr)>0))
         {
            return -ERESTARTSYS; // signal: tell the fs layer to handle it
         }
      }
      else
      {
         wait_result=wait_event_interruptible_timeout(ring_buf_mgr_ptr->rd_wait_q,ring_buf_data_size(ring_buf_mgr_ptr)>0,msecs_to_jiffies(ring_buf_mgr_ptr->read_time_out_msecs));
         if(wait_result==0)
         {
            return -EIO;
         }
         else if(wait_result==-ERESTARTSYS)
         {
            return -ERESTARTSYS; // signal: tell the fs layer to handle it
         }
      }

      // otherwise loop, but first reacquire the lock which released on line 340
      if(down_interruptible(&ring_buf_mgr_ptr->sem))
      {
         return -ERESTARTSYS;
      }
   }
   return result;
}

// wait for space for writing; caller must hold device semaphore.
// on error the semaphore will be released before returning.
static int ring_buf_get_linux_write_available_size(struct ring_buf_mgr* ring_buf_mgr_ptr)
{
   int result;

#ifdef DEBUG_IT
   pr_info("dev_file_mgr_info_ptr:0x%p\n",dev_file_mgr_info_ptr);
#endif
   /* the semaphore is held when ing_buf_free_space(dev_file_mgr_info_ptr) is called */
   while((result=ring_buf_free_space(ring_buf_mgr_ptr))==0)
   {
      DEFINE_WAIT(wait); 
      up(&ring_buf_mgr_ptr->sem);

      if(ring_buf_mgr_ptr->wr_nonblock==true)
      {
#ifdef DEBUG_IT
         pr_info("no blk wr\n");
#endif
         return -EAGAIN;
      }
#ifdef DEBUG_IT
      pr_info("blk wr\n");
#endif
     /*
      *one reason is the write task dominate CPU resource and the read task does not have chance to kick in.
      *if schedule() is called, the write task can yield the CPU resource and the read task can kick in.
      *if schedule() is not used, the user program should call usleep(useconds_t usec)
      *after write(int fd, const void *buf, size_t count) returns -1 with errno=EAGAIN, then calls
      *write(int fd, const void *buf, size_t count) again. 01/09/2015
      */

#ifdef DEBUG_IT
      pr_info("\"%s\" writing: going to wait queue,result=%d\n",current->comm,result);
#endif
      prepare_to_wait(&ring_buf_mgr_ptr->wr_wait_q,&wait,TASK_INTERRUPTIBLE);

      if((result=ring_buf_free_space(ring_buf_mgr_ptr))==0)
      {
#ifdef DEBUG_IT
         pr_info("\"%s\" writing: sleeping at here\n",current->comm);
#endif

#ifdef DEV_FILE_WR_OP
         schedule(); //schedule_timeout(WRITE_WAIT_TIME_OUT_JIFFIES) or udelay(unsigned long); an enhanced solution
#else /* !DEV_FILE_WR_OP */
         /*
          * using schedule() or schedule_timeout(signed long) will change the order of tast in the work queue,
          * so the sequence of vectors generated from ISR top half will be changed.
          * using udelay(signed long) instead of schedule_timeout(signed long)
          */
         udelay(1);
#endif /* DEV_FILE_WR_OP */

#ifdef DEBUG_IT
         pr_info("\"%s\" writing: not sleeping\n",current->comm);
#endif
      }

#ifdef DEBUG_IT
      pr_info("\"%s\" writing: not waking up sleep at here\n",current->comm);
#endif
      finish_wait(&ring_buf_mgr_ptr->wr_wait_q,&wait);
#ifdef DEBUG_IT
      pr_info("\"%s\" writing: waking up sleep at here\n",current->comm);
#endif
      if(signal_pending(current))
      {
         pr_err("signal_pending(current) err!!!\n");
         return -ERESTARTSYS; // signal: tell the fs layer to handle it
      }

      if(down_interruptible(&ring_buf_mgr_ptr->sem))
      {
         pr_err("down_interruptible(&dev_file_mgr_info_ptr->sem) err!!!\n");
         return -ERESTARTSYS;
      }
   }

   return result;
}

#ifndef _RING_BUFFER_H_
#define _RING_BUFFER_H_

//#define DEV_FILE_WR_OP

#define WAIT_FOREVER                               -1

#define DEFAULT_RING_BUFFER_SIZE                   512

struct ring_buf_mgr
{
  wait_queue_head_t rd_wait_q,wr_wait_q;  /* write and read queues */

  char *ring_buf;                      /* begin of buf, end of buf */
  char *rd_ptr,*wr_ptr;                /* where to read, where to write */
  short ring_buf_sz;                   /* short not unit16_t/u16 type is used due to module_param(g_ring_buffer_size,short,S_IRUGO) */
#define SET_WRAP_FLAG           1
#define NON_BLOCK_WR_MODE       1
#define NON_BLOCK_RD_MODE       1
#define DEVICE_FILE_OPENED      1

  u64  wrap_flag: 1;                   /* set to 1 when wr_ptr wraps, set to 0 when rd_ptr wraps. */
  u64  wr_nonblock: 1;                 /* non-blocking write mode. */
  u64  rd_nonblock: 1;                 /* non-blocking read mode. */
  struct semaphore sem;                /* mutual exclusion semaphore */

  struct fasync_struct *async_queue;   /* asynchronous readers */

  int read_time_out_msecs;
  int write_time_out_msecs;
};

#ifdef DEV_FILE_WR_OP
int naii_ring_buffer_init(struct ring_buf_mgr** ring_buf_mgr_ptr,short ring_buf_sz,int read_time_out,int write_time_out);
#else /*!DEV_FILE_WR_OP*/
int naii_ring_buffer_init(struct ring_buf_mgr** ring_buf_mgr_ptr,short ring_buf_sz,int rd_time_out,int wr_time_out,bool non_block_wr);
#endif /*DEV_FILE_WR_OP*/
int naii_ring_buffer_release(struct ring_buf_mgr** ring_buf_mgr_ptr);

int naii_ring_buffer_read(struct ring_buf_mgr* ring_buf_mgr_ptr,char __user *usr_buf, size_t count);
#ifdef DEV_FILE_WR_OP
int naii_ring_buffer_write(struct ring_buf_mgr* ring_buf_mgr_ptr,const char __user *usr_buf,size_t count);
#else /*!DEV_FILE_WR_OP*/
int naii_ring_buffer_write(struct ring_buf_mgr* ring_buf_mgr_ptr,const char *in_buf,size_t count);
#endif /*DEV_FILE_WR_OP*/

#endif /*_RING_BUFFER_H_*/

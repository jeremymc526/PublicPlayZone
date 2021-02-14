#ifndef _NAI_VME_INT_H_
#define _NAI_VME_INT_H_

static int g_read_time_out_msecs=WAIT_FOREVER;
static int g_write_time_out_msecs=60*60*1000;            // 1 hour

#ifndef DEV_FILE_WR_OP
static int g_non_block_wr_flag=false;
#endif /*!DEV_FILE_WR_OP*/

struct dev_file_mgr_info
{
  struct ring_buf_mgr* naii_int_ring_buf_mgr_ptr;
  u64  device_file_opend: 1;               /* device file opened. */

  dev_t first;                             /* for the first device number */

  struct class *nai_vme_int_class;         /* device class */
  struct cdev char_dev;                    /* char device structure */
};

struct int_work_q
{
  struct work_struct work_q;               /* for "Top Half and Bottom Half" implementation */
  struct dev_file_mgr_info* dev_file_mgr_info_ptr;
  u8 int_vec;
  u8 int_level;
};

void vme_isr_bottom_half(struct work_struct* work_struct_ptr);
int create_nai_vme_int_dev_file(struct dev_file_mgr_info** int_mgr_ptr,int int_level,short ring_buf_sz,int* status);
int delete_nai_vme_int_dev_file(struct dev_file_mgr_info** dev_file_mgr_info_ptr,bool force);
#endif /* _NAI_VME_INT_H_*/

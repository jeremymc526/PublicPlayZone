#ifndef _SHARED_UTILS_H_
#define _SHARED_UTILS_H_

//#define DEBUG_IT
#define RETRY_LIMIT 3
static unsigned g_io_limit = 128;
module_param(g_io_limit, uint, 0);
MODULE_PARM_DESC(g_io_limit, "Maximum bytes per I/O (default 128)");
 
static unsigned g_write_timeout = 25;
module_param(g_write_timeout, uint, 0);
MODULE_PARM_DESC(g_write_timeout, "Time (in ms) to try writes (default 25)");

enum I2C_ADAPTERS
{
   I2C_ADAPTER_0,
   I2C_ADAPTER_1,
   I2C_ADAPTER_NUM
};

struct i2c_smbus_drv_data
{
   struct cdev c_dev;                   /*make the link from address of struct cdev to address of struct struct i2c_smbus_drv_data*/
   struct class *generic_i2c_class;     /*device class*/
   dev_t first;                         /*for the first device number*/
   struct mutex *mutex_ptr;             /*shared by all instances*/
   struct i2c_adapter *i2c_adapter_ptr; //TBD shared by all instances ???
   uint16_t user_cnt;
   uint8_t chip_access_flags;           //TBD create ioctl(...) to re-set/change it, 2018-11-19
   uint16_t io_limit;                   //TBD create ioctl(...) to re-set/change it, 2018-11-19
   uint32_t write_timeout;
   uint16_t num_addresses;
   struct i2c_client *clients[];
};

void build_i2c_client_addr(struct i2c_client* i2c_cli,const u8* in_buf);

struct i2c_smbus_drv_data* create_dev_fs
(
   const char* dev_file_name,
   const char* parent_name,
   struct file_operations* file_ops,
   uint32_t i2c_addr_num,
   uint8_t chip_flags,
   int* status
);

void delete_char_dev( struct i2c_smbus_drv_data* i2c_smbus_drv_data_ptr );

//#ifdef DEBUG_IT
void printk_hex_info(const u8* hex_info,unsigned int info_size);
//#endif DEBUG_IT

uint32_t the_max( uint32_t a, uint32_t b );

#endif /*_SHARED_UTILS_H_*/

#ifndef _SHARED_SMBUS_H_
#define _SHARED_SMBUS_H_

ssize_t nai_smbus_dev_write(struct file *filp, const char __user *usr_buf, size_t len, loff_t *off);
ssize_t nai_smbus_dev_read(struct file *filp, char __user *usr_buf, size_t len, loff_t *off);

int32_t nai_smbus_write_block(struct i2c_adapter*i2c_adapter_ptr, const uint8_t* io_buf, uint16_t io_buf_sz);
int32_t nai_smbus_read_block(struct i2c_adapter *i2c_adapter_ptr, const uint8_t* in_buf, uint16_t in_buf_len, uint8_t* out_buf, uint16_t out_buf_len);

#endif /*_SHARED_SMBUS_H_*/

#ifndef _SHARED_I2C_H_
#define _SHARED_I2C_H_

ssize_t nai_i2c_dev_read(struct file *filp, char __user *usr_buf, size_t len, loff_t *off);
ssize_t nai_i2c_dev_write(struct file *filp, const char __user *usr_buf, size_t len, loff_t *off);

#endif /*_SHARED_I2C_H_*/

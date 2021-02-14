obj-$(CONFIG_NAI_GENERIC_SMBUS)	+= nai_generic_smbus.o
nai_generic_smbus-objs          := nai_smbus.o shared_utils.o shared_smbus.o

## This module needs shared_utils.o, which is compiled in nai_generic_smbus.ko,
## so nai_generic_i2c.ko depends on nai_generic_smbus.ko
obj-$(CONFIG_NAI_GENERIC_I2C)   += nai_generic_i2c.o
nai_generic_i2c-objs            := nai_i2c.o shared_i2c.o #in here CANNOT add shared_utils.o
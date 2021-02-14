#ifndef _NAI_GENERIC_I2C_H_
#define _NAI_GENERIC_I2C_H_

/* This Protocol can be used for both standard and NAI Internal SMBUS read operations.
 * In standard SMBUS, value in SMBUS_RD_ARGC_OFF is 0 and no data in SMBUS_RD_ARGV_OFF. 
 */
enum NAI_SMBUS_RD_FMT_INDX
{
   SMBUS_RD_ADDR_OFF,
   SMBUS_RD_ADDR_EXT_OFF,  /*In case for ten bit chip address*/
   SMBUS_RD_CMD_OFF,       /*CommandCode in SMBUS Specification BlockRead*/
   SMBUS_RD_LEN_OFF,       /*ByteCount in SMBUS Specification BlockRead*/
   SMBUS_RD_ARGC_OFF,      /*In NAI internal protocol, SMBUS_RD_ARGC_OFF includes the size of ByteCount and DataByte[1:N] in SMBUS Specification BlockWrite*/
   SMBUS_RD_ARGV_OFF,      /*In NAI internal protocol, SMBUS_RD_ARGV_OFF contains ByteCount and DataByte[1:N] in SMBUS Specification BlockWrite*/

   NAI_SMBUS_RD_MGR_HEADER_SZ
};

/* This Protocol can be used for both standard and NAI Internal SMBUS write operations.
 * In standard SMBUS, value in SMBUS_WR_DATA_LEN_OFF is written data size.
 * In standard SMBUS, value(s) in SMBUS_WR_DATA_OFF is/are written data.
 */
enum NAI_SMBUS_WR_FMT_INDX
{
   SMBUS_WR_ADDR_OFF,
   SMBUS_WR_ADDR_EXT_OFF,  /*In case for ten bit chip address*/
   SMBUS_WR_CMD_OFF,       /*CommandCode in SMBUS Specification BlockWrite*/
   SMBUS_WR_DATA_LEN_OFF,  /*In NAI internal protocol, SMBUS_WR_DATA_LEN_OFF includes the size of ByteCount and DataByte[1:N] in SMBUS Specification BlockWrite*/
   SMBUS_WR_DATA_OFF,      /*In NAI internal protocol, SMBUS_WR_DATA_OFF contains ByteCount and DataByte[1:N] in SMBUS Specification BlockWrite*/

   NAI_SMBUS_WR_MGR_HEADER_SZ
};

enum I2C_RD_FMT_INDX
{
   /*I2C_ADAPTER_OFF,*/
   I2C_RD_ADDR_OFF,
   I2C_RD_ADDR_EXT_OFF,  /*in case for ten bit chip address*/
   I2C_RD_CMD_OFF,
   I2C_RD_LEN_LSB_OFF,
   I2C_RD_LEN_MSB_OFF,
   I2C_RD_ARGC_OFF,      /*put/add 1 as the parameter count for offset LSB*/
   I2C_RD_ARGV_OFF,      /*put offset LSB to here*/
   NAI_I2C_RD_MGR_HEADER_SZ
};

enum I2C_WR_FMT_INDX
{
   I2C_WR_ADDR_OFF,
   I2C_WR_ADDR_EXT_OFF,      /*in case for ten bit chip address*/
   I2C_WR_CMD_OFF,           /*put offset MSB byte in here*/
   I2C_WR_LEN_LSB_OFF,       /*put (whole write data size + 1) LSB in here, 1 to indicate offset LSB*/
   I2C_WR_LEN_MSB_OFF,       /*put (whole write data size + 1) MSB in here, 1 to indicate offset LSB*/
   I2C_WR_ARGC_OFF,          /*put/add 1 as the parameter count for offset LSB*/
   I2C_WR_ARGV_OFF,          /*put offset LSB in first byte then put write data to here*/
   NAI_I2C_WR_MGR_HEADER_SZ
};

//#define SUPPORT_I2C_DEVICE_FILE

#define NAI_I2C_SMBUS_RD_WR_MGR_BUF_LEN     32
#define SMBUS_TRX_BUF_LEN_MAX               I2C_SMBUS_BLOCK_MAX /*I2C_SMBUS_BLOCK_MAX defined by the kernel file "i2c.h"*/
#define I2C_TRX_BUF_LEN_MAX                 512
#endif/* _NAI_GENERIC_I2C_H_*/

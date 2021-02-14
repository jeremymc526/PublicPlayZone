/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __NAI_SERDES_PRV_H__
#define __NAI_SERDES_PRV_H__

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/param.h>

/* Johnny?? */
#define NAI_IGNORE_MISSING_TOP_MODULE                  1
/* Operation timeout In milliseconds (converted to jiffies) */
/* 5 seconds timeout */
//#define COMPLETION_TIMEOUT                         (5000 / HZ)
#define COMPLETION_TIMEOUT                         (5 * HZ)

/* Total number of words in SERDES header */
#define TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS     6

/* Maximum number of words in the payload when in operational mode */
#define NAI_OPER_MAX_PAYLOAD_IN_WORDS	               250

/* Maximum number of words in the SERDES message */
#define MAX_SERDES_MSG_IN_WORDS 	               256

/* SERDES Command Types */
#define NAI_SERDES_READREG                             0x0002
#define NAI_SERDES_WRITEREG                            0x0003

/* SERDES Byte Enable */
#define NAI_SERDES_32BITDATA                           0x0F

/* SERDES protocol data structures */
typedef struct {
	/* Bits  0-3 : Type
	 * Bit     4 : Credit Limit
	 * Bit     5 : SPARE
	 * Bit     6 : SeqNumTx
	 * Bit     7 : SeqNumRx
	 * Bits 8-15 : Byte Enable
	 */
	union {
   		u16 usSERDES0;	/* 2 Bytes */
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			u8 ucType        : 4;
			u8 ucCreditLimit : 1;
			u8 ucToHPS 	 	: 1;
			u8 ucSeqNumTx 	: 1;
			u8 ucSeqNumRx 	: 1;
			u8 ucByteEnable : 4;
			u8 ucSpare1		: 1;
			u8 ucSpare2		: 1;
			u8 ucSpare3		: 1;
			u8 ucDataMode	: 1;
#elif defined(__BIG_ENDIAN_BITFIELD)
			u8 ucDataMode	: 1;
			u8 ucSpare3		: 1;
			u8 ucSpare2   	: 1;
			u8 ucSpare1		: 1;
			u8 ucByteEnable  : 4;
			u8 ucSeqNumRx 	 : 1;
			u8 ucSeqNumTx 	 : 1;
			u8 ucToHPS 	 : 1;
			u8 ucCreditLimit : 1;
			u8 ucType 	 : 4;
#else
#error "Asjust your <asm/byteorder.h> defines"
#endif
		};
	};

	/* Bits    0-7 : Payload Max Length
	 * Bits   8-11 : Requester ID
	 * Bits  12-15 : Completer ID
	  */
	union {
		u16 usSERDES1;    /* 2 Bytes */
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			u8 ucPayloadLength : 8;
			u8 ucRequesterID   : 4;
			u8 ucCompleterID   : 4;
#elif defined(__BIG_ENDIAN_BITFIELD)
			u8 ucCompleterID   : 4;
			u8 ucRequesterID   : 4;
			u8 ucPayloadLength : 8;
#else
#error "Asjust your <asm/byteorder.h> defines"
#endif
		};
	};

	/* Bits  0-1 : Reserved
	 * Bits 2-15 : Address Lo
	 */
	union {
   		u16 usSERDES2;	/* 2 Bytes */
		u16 usAddressLo;
	};

	/* Bits 0-15 : Address Hi */
	union {
   		u16 usSERDES3;	/* 2 Bytes */
		u16 usAddressHi;
	};

	/* Bits   0-7 : Block Address Increment Value
	 * Bits  8-15 : Spare
	 */
	union {
   		u16 usSERDES4;	/* 2 Bytes */
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			u8 ucBlockAddrIncrVal : 8;
			u8 usSPARE1	      : 8;
#elif defined(__BIG_ENDIAN_BITFIELD)
			u8 usSPARE1	      : 8;
			u8 ucBlockAddrIncrVal : 8;
#else
#error "Asjust your <asm/byteorder.h> defines"
#endif
		};
	};

	/* Bits  0-15 : Spare */
	union {
   		u16 usSERDES5;	/* 2 Bytes */
		u16 usSPARE2;
	};
} SerdesHdr;


typedef struct {
	/* Array of payload data of size: OPER_MAX_PAYLOAD_IN_WORDS */
	u16 usData[NAI_OPER_MAX_PAYLOAD_IN_WORDS];
} SerdesOperPayLd;

typedef union {
	/* Array representation of the entire NAIMsg */
	u16 msg[MAX_SERDES_MSG_IN_WORDS];
	struct {
		SerdesHdr       tSerdesHdr;
		SerdesOperPayLd tSerdesPayLd;
	};
} NAIOperMsg;

typedef union {
	/* 32-bit value which is comprised of the unioned Lo Word and Hi Word */
	u32 unValue;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u32 usLoWord : 16;
		u32 usHiWord : 16;
#elif defined(__BIG_ENDIAN_BITFIELD)
		u32 usHiWord : 16;
		u32 usLoWord : 16;
#else
#error "Asjust your <asm/byteorder.h> defines"
#endif
	};
} FIFOValue;

typedef union {
	/* 16-bit value which is comprised of the unioned Lo Byte and Hi Byte */
	u16 usValue;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u16 ucLoByte : 8;
		u16 ucHiByte : 8;
#elif defined(__BIG_ENDIAN_BITFIELD)
		u16 ucHiByte : 8;
		u16 ucLoByte : 8;
#else
#error "Asjust your <asm/byteorder.h> defines"
#endif
	};
} WORDValue;

/* Comms protocol definitions and data structures */

/* Handshake address in "Common" area */
#define MODULE_COMMON_HANDSHAKE_ADDR  0x0000025C

/* No of words in the packet header: 6W-Serdes, 8W-Transport, 6W-Command */
#define CONFIG_TOTAL_PKT_HDR_IN_WORDS	       20

/* No of words in TRANSPORT header in configuration mode */
#define CONFIG_TOTAL_TRANSPORT_HDR_IN_WORDS        8

/* No of words in COMMAND header in configuration mode **/
#define CONFIG_TOTAL_COMMAND_HDR_IN_WORDS          6

/* Max of payload words in configuration mode **/
#define CONFIG_MAX_PAYLOAD_IN_WORDS                236

/* Command Types */
/* READ EEPROM command : used to read from the EEPROM storage */
#define	COMMAND_TYPECODE_READEEPROM		       0x0001
/* WRITE EEPROM command : used to write to the EEPROM storage */
#define	COMMAND_TYPECODE_WRITEEEPROM	       0x0002
/* ERASE FLASH command : used to erase FLASH pages */
#define COMMAND_TYPECODE_ERASEFLASH		       0x0003
/* READ FLASH command : used to read from the FLASH storage */
#define	COMMAND_TYPECODE_READFLASH		       0x0004
/* WRITE FLASH command : used to write to the FLASH storage */
#define	COMMAND_TYPECODE_WRITEFLASH		       0x0005
/* ASSIGN SLOT command : used to tell a module what slot it is located in */
#define COMMAND_TYPECODE_ASSIGNSLOT		       0x0006
/* RETRIEVE SLOT command : used to retrieve the current slot */
#define COMMAND_TYPECODE_RETRIEVESLOT	       0x0007
/* EXIT CONFIG MODE command : used to force exit of the CONFIG MODE app */
#define COMMAND_TYPECODE_EXIT_CONFIG_MODE	   0x0008
/* RESET command : used to reset this module's CPU */
#define COMMAND_TYPECODE_RESET_MODULE	       0x0009
/* REQUEST FINISHED command : used to send response back to caller */
#define COMMAND_TYPECODE_REQUEST_FINISHED      0x000A
/* CONFIG microcontroller command : used to configure all channels on a microcontroller */
#define COMMAND_TYPECODE_CONFIG_MICRO 0x000B
/* GET microcontroller command : used to fetch version of microcontroller bootloader and supported bootloader commands */
#define COMMAND_TYPECODE_GET_MICRO    0x000C
/* ERASE microcontroller command : used to erase entire EPROM in microcontroller*/
#define COMMAND_TYPECODE_ERASE_MICRO  0x000D
/* DEBUG command : used to run some debug tests */
#define COMMAND_TYPECODE_DEBUG		           0x0FFF

typedef struct {
	u16 usID;
	u16 usSequenceNum;
	u32 unMsgLength;
	u16 usPacketPayLdLength;
	u16 usExpectedSequenceCount;
	u32 unCRC;
} TransportHdr;

typedef struct {
	u16  usCommandType;
	u16  usChipID;
	u32  unOffset;
	u32  unPayLdRequestLength;
} CommandHdr;

typedef struct {
	u16 usData[CONFIG_MAX_PAYLOAD_IN_WORDS];
} CommandPayLd;

typedef struct {
	CommandHdr   tCommandHdr;
	CommandPayLd tCommandPayLd;
} TransportPayLd;

typedef struct {
	TransportHdr   tTransportHdr;
	TransportPayLd tTransportPayLd;
} SerdesConfigPayLd;


typedef union {
	/* Array representation of the entire NAIMsg */
	u16 msg[MAX_SERDES_MSG_IN_WORDS];
	struct {
		/* SerdesHdr encapsulates SERDES specific information */
		SerdesHdr   tSerdesHdr;
		/* SerdesConfigPayLd encapsulates all of the other layers of the
		 * message protocol including the Transport mechanism and
		 * Command information
		 */
		SerdesConfigPayLd tSerdesPayLd;
	};
} NAIMsg;


typedef struct _MsgPacket {
	NAIMsg tNAIMsg;
	struct _MsgPacket *ptNext;
} MsgPacket;

typedef struct _MsgPacketList {
	s32 nCount;
	s32 unWordsLeftToRead;
	MsgPacket *ptStart;
	MsgPacket *ptEnd;
	struct _MsgPacketList *ptNext;
} MsgPacketList;

typedef struct {
	s32 nCount;
	MsgPacketList *ptStart;
	MsgPacketList *ptEnd;
} MsgList;

#endif /* __NAI_SERDES_PRV_H__ */

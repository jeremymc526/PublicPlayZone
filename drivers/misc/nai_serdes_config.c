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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/decompress/mm.h>

#include "nai_serdes.h"
#include "nai_serdes_prv.h"
#include "nai_serdes_utils.h"
#include "nai_serdes_config.h"
#include "nai_serdes_oper.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static u16 g_usTranID = 1;
static bool g_bLock = false;

/* Isolated DT Globals */
#define NUM_DT_CHANNELS 16
const uint8_t ALL_CHANNELS = 0xFF;

/* MessageCreation */
static MsgPacket * create_nai_msg_packet(u16* pusData);
static MsgPacketList * create_nai_msg_packet_list(void);
static MsgPacketList * find_nai_msg_packet_list(MsgList *ptMsgList, u16 usTranID);
static void init_nai_msgs(MsgList *ptMsgList);
static void create_and_append_nai_msg_packet(MsgPacketList *ptMsgPacketList, u16* pusData);
static void delete_nai_msg_packets(MsgPacketList *ptMsgPacketList);
static void add_nai_msg(MsgList *ptMsgList, MsgPacketList *ptMsgPacketList);
static void delete_nai_msgs(MsgList *ptMsgList);

/* PrintMessage */
#ifdef _VERBOSE
static void print_nai_msgs(MsgList *ptMsgList, bool bPrintPayLd);
static void print_nai_msg(MsgPacketList *ptMsgPacketList, bool bPrintPayLd);
static void print_nai_msg_payload(MsgPacket *ptMsgPacket);
#endif

/* MessageValidation */
static s32 compute_nai_msg_crc(MsgPacketList *ptMsgPacketList);
static s32 validate_nai_msgs(MsgList *ptMsgList);
static s32 validate_nai_msg(MsgPacketList *ptMsgPacketList);

/* MessageProcessing */
static bool nai_msg_requires_finished_response(MsgPacketList *ptMsgPacketList);
static u32  nai_get_completion_timeout(MsgPacket *ptMsgPacket);

static s32 nai_send_msg(MsgPacketList *ptMsgPackets);
static s32 nai_receive_msg_packet(u8 ucRequesterID, u8 ucCompleterID, MsgList *ptMsgList);
static u8 nai_get_serdes_completer_id(MsgPacket *ptMsgPacket);
static u8 nai_get_serdes_requester_id(MsgPacket *ptMsgPacket);
static u16 calculate_nai_expected_sequence_count(s32 ulPayloadWordLength);

/* We provide higher level calls so no need to expose the inner workings of actual read or write requests */
static s32 make_read_request(u16 usCommandType, u8 ucRequesterID, u8 ucCompleterID, u16 usChipID, u32 unOffset, u8 *pucBuf, s32 nLen);
static s32 make_write_request(u16 usCommandType, u8 ucRequesterID, u8 ucCompleterID, u16 usChipID, u32 unEepromOffset, u8 *pucBuf, s32 nLen);
static void* aligned_malloc(size_t size, size_t alignment);
static void aligned_free(void* p);
static u16 get_next_tran_id(void);

static void* aligned_malloc(size_t size, size_t alignment) {

    uintptr_t r = (uintptr_t)malloc(size + --alignment + sizeof(uintptr_t));
    uintptr_t t = r + sizeof(uintptr_t);
    uintptr_t o =(t + alignment) & ~(uintptr_t)alignment;

    if (!r) 
		return NULL;

    ((uintptr_t*)o)[-1] = r;

    return (void*)o;
}

static void aligned_free(void* p) {

    if (!p) 
		return;

    free((void*)(((uintptr_t*)p)[-1]));
}

static u16 get_next_tran_id(void)
{
	static u16 usTranID = 0;
	if (!g_bLock)
	{
		g_bLock = true;
		g_usTranID++;
		
		if (g_usTranID >= 32767)
			g_usTranID = 1;
		usTranID = g_usTranID;
		g_bLock = false;
	}

	return usTranID;
}

static MsgPacket * create_nai_msg_packet(u16* pusData)
{
//	MsgPacket *ptNewMsgPacket = malloc(sizeof(MsgPacket));
	MsgPacket *ptNewMsgPacket = aligned_malloc(sizeof(MsgPacket), 4);

#ifdef _DEBUG_X
	s32 i=0;

	for (i=0; i < MAX_SERDES_MSG_IN_WORDS; i++)
		printk("usData[%d] = 0x%4.4x\r\n", i, pusData[i]);
#endif
	memcpy(ptNewMsgPacket->tNAIMsg.msg, pusData, (MAX_SERDES_MSG_IN_WORDS*2));
	ptNewMsgPacket->ptNext = NULL;

	return ptNewMsgPacket;
}

static MsgPacketList * create_nai_msg_packet_list()
{
//	MsgPacketList *ptNewMsgPacketList = malloc(sizeof(MsgPacketList));
	MsgPacketList *ptNewMsgPacketList = aligned_malloc(sizeof(MsgPacketList), 4);
		
	ptNewMsgPacketList->ptStart = NULL;
	ptNewMsgPacketList->ptEnd = ptNewMsgPacketList->ptStart;
	ptNewMsgPacketList->ptNext = NULL;
	ptNewMsgPacketList->nCount = 0;

	return ptNewMsgPacketList;
}

static MsgPacketList * find_nai_msg_packet_list(MsgList *ptMsgList, u16 usTranID)
{
	MsgPacketList *ptTraverse = ptMsgList->ptStart;

	while (ptTraverse != NULL)
	{					
		if (ptTraverse->ptStart->tNAIMsg.tSerdesPayLd.tTransportHdr.usID == usTranID)
		{
#ifdef _VERBOSE
			printk("Found Transport ID: 0x%4x\r\n", ptTraverse->ptStart->tNAIMsg.tSerdesPayLd.tTransportHdr.usID);
#endif
			break;
		}
		ptTraverse = ptTraverse->ptNext;
	}	

	return ptTraverse;
}

static void init_nai_msgs(MsgList *ptMsgList)
{
	ptMsgList->ptStart = NULL;
	ptMsgList->ptEnd = ptMsgList->ptStart;
	ptMsgList->nCount = 0;	
}

static void create_and_append_nai_msg_packet(MsgPacketList *ptMsgPacketList, u16* pusData)
{	
	MsgPacket *ptNewMsgPacket = create_nai_msg_packet(pusData);

#ifdef _VERBOSE
	printk("Trans ID: %u Adding New Msg Packet - Sequence #: %u\r\n", 
				ptNewMsgPacket->tNAIMsg.tSerdesPayLd.tTransportHdr.usID, 
				ptNewMsgPacket->tNAIMsg.tSerdesPayLd.tTransportHdr.usSequenceNum);
#endif

	if (ptMsgPacketList->ptEnd == NULL)
	{
		ptMsgPacketList->ptStart = ptNewMsgPacket;
	  	ptMsgPacketList->ptEnd = ptMsgPacketList->ptStart;
	}
	else
	{
		ptMsgPacketList->ptEnd->ptNext = ptNewMsgPacket;
		ptMsgPacketList->ptEnd = ptNewMsgPacket;
	}

	ptMsgPacketList->nCount++;
}

static void delete_nai_msg_packets(MsgPacketList *ptMsgPacketList)
{
	MsgPacket *ptTraverse = NULL;	
	
	while (ptMsgPacketList->ptStart != NULL)
	{
		ptTraverse = ptMsgPacketList->ptStart;
		ptMsgPacketList->ptStart = ptMsgPacketList->ptStart->ptNext;		
//		free(ptTraverse);
		aligned_free(ptTraverse);
		ptMsgPacketList->nCount--;			
	}
}

static void add_nai_msg(MsgList *ptMsgList, MsgPacketList *ptMsgPacketList)
{
#ifdef _VERBOSE
	printk("Adding New Msg - Num of Packets: %u\r\n", ptMsgPacketList->ptEnd->tNAIMsg.tSerdesPayLd.tTransportHdr.usSequenceNum);
#endif

	if (ptMsgList->ptEnd == NULL)
	{
		ptMsgList->ptStart = ptMsgPacketList;
	  	ptMsgList->ptEnd = ptMsgList->ptStart;
	}
	else
	{
		ptMsgList->ptEnd->ptNext = ptMsgPacketList;
		ptMsgList->ptEnd = ptMsgPacketList;
	}

	ptMsgList->nCount++;
}

static void delete_nai_msgs(MsgList *ptMsgList)
{
	MsgPacketList *ptTraverse = NULL;
	
	while (ptMsgList->ptStart != NULL)
	{
		ptTraverse = ptMsgList->ptStart;
		ptMsgList->ptStart = ptMsgList->ptStart->ptNext;
		delete_nai_msg_packets(ptTraverse);
//		free(ptTraverse);
		aligned_free(ptTraverse);
		ptMsgList->nCount--;
	}
}

#ifdef _VERBOSE
static void print_nai_msgs(MsgList *ptMsgList, bool bPrintPayLd)
{
	MsgPacketList *ptTraverse = ptMsgList->ptStart;

	printk("\r\n******************************************************************\r\n");
	printk("Printing NAI Messages\r\n");
	printk("Total Num of Messages In Memory: %d\r\n", (int)ptMsgList->nCount);

	while (ptTraverse != NULL)
	{
		print_nai_msg(ptTraverse, bPrintPayLd);
		ptTraverse = ptTraverse->ptNext;
	}
	printk("\r\n******************************************************************\r\n");
}

static void print_nai_msg(MsgPacketList *ptMsgPacketList, bool bPrintPayLd)
{

	MsgPacket *ptTraverse = ptMsgPacketList->ptStart;	
	s32 nMsgPacketCount = 0;
	
	if (ptMsgPacketList == NULL)
		return;
	
	printk("\r\n******************************************************************\r\n");
	printk("Printing Msg Contents - Total Num Packets in Msg = %d\r\n", (int)ptMsgPacketList->nCount);

	while (ptTraverse != NULL)
	{
		nMsgPacketCount++;
		printk("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\r\n");
		printk("Msg Packet #:     %d\r\n", (unsigned int)nMsgPacketCount);
		printk("Serdes CmdType:   0x%4.4x - (%u)\r\n", ptTraverse->tNAIMsg.tSerdesHdr.ucType, ptTraverse->tNAIMsg.tSerdesHdr.ucType);
		printk("Serdes Length:    0x%4.4x - (%u)\r\n", (ptTraverse->tNAIMsg.tSerdesHdr.ucPayloadLength), (ptTraverse->tNAIMsg.tSerdesHdr.ucPayloadLength));
		printk("Transport ID:     0x%4.4x - (%u)\r\n", ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usID, ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usID);
		printk("Transport Length: 0x%8.4x - (%u)\r\n", (unsigned int)ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.unMsgLength, (unsigned int)ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.unMsgLength);
		printk("Transport Seq #:  0x%4.4x - (%u)\r\n", ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usSequenceNum, ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usSequenceNum);
		printk("Transport Expected Seq Count:  0x%4.4x - (%u)\r\n", ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usExpectedSequenceCount, ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usExpectedSequenceCount);
		printk("Msg CRC: 0x%8.4x - (%u)\r\n", (unsigned int)ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.unCRC, (unsigned int)ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.unCRC);

		if (bPrintPayLd)
			print_nai_msg_payload(ptTraverse);

		printk("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\r\n");

		ptTraverse = ptTraverse->ptNext;
	}	
	printk("******************************************************************\r\n");
}


static void print_nai_msg_payload(MsgPacket *ptMsgPacket)
{
	s32 i = 0;
	u16 usPayloadLengthInWords = 0;

	if (ptMsgPacket != NULL)
	{
		usPayloadLengthInWords = convert_bytes_to_words(ptMsgPacket->tNAIMsg.tSerdesPayLd.tTransportHdr.usPacketPayLdLength);
		printk("Payload Length (in WORDS): %u\r\n", usPayloadLengthInWords);
		
		for (i=0; i < usPayloadLengthInWords; i++)		
			printk("Msg Payload[%d] = %4.4x\r\n", (unsigned int)i, (unsigned short)ptMsgPacket->tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandPayLd.usData[i]);	
	}
}
#endif

static s32 compute_nai_msg_crc(MsgPacketList *ptMsgPacketList)
{
	s32 unCRC = 0;

#ifdef _COMPUTE_CRC
	MsgPacket *ptTraverse = ptMsgPacketList->ptStart;
	NAIMsg *ptMsgCopy = NULL;
//	ptMsgCopy = (NAIMsg *)malloc(sizeof(NAIMsg));
	ptMsgCopy = (NAIMsg *)aligned_malloc(sizeof(NAIMsg), 4);

	while (ptTraverse != NULL)
	{	
		/* Let's make sure our buffer is all zero to start */
		memset(ptMsgCopy->msg, 0, sizeof(NAIMsg));

		/* Make a copy of the message we want to perform a CRC on...we do this because we need to 
           strip some information that is ok to change during transit before we compute the CRC */
		memcpy(ptMsgCopy->msg, ptTraverse->tNAIMsg.msg, sizeof(NAIMsg));
		ptMsgCopy->tSerdesHdr.usSERDES0 &= 0x3F; /* This zeros out SeqNumRx (bit 7) and SeqNumTx (bit 6) */
		ptMsgCopy->tSerdesHdr.usSERDES4 &= 0x0F; /* This zeros out COMPLETER_ID (bits 4 - 7) */
		ptMsgCopy->tSerdesPayLd.tTransportHdr.unCRC = 0; /* Do not take CRC into account when computing CRC (since it is not present when we 1st calculate CRC */

		unCRC = crc32(unCRC, (void *)ptMsgCopy->msg, ptMsgCopy->tSerdesHdr.ucPayloadLength);
		ptTraverse = ptTraverse->ptNext;
	}
#else
#ifdef _VERBOSE
	printk("***COMPUTING OF CRC HAS BEEN TURNED OFF***\r\n");	
#endif
#endif
	return unCRC;
}



static s32 validate_nai_msgs(MsgList *ptMsgList)
{
	s32 nStatus = NAI__SUCCESS;
	s32 nTempStatus = NAI__SUCCESS;
	MsgPacketList *ptTraverse = ptMsgList->ptStart;

	while ( (ptTraverse != NULL) && (nStatus == NAI__SUCCESS) )
	{
		nTempStatus = validate_nai_msg(ptTraverse);
		if (nTempStatus != NAI__SUCCESS)
			nStatus = nTempStatus;
		ptTraverse = ptTraverse->ptNext;
	}

	return nStatus;
}

static s32 validate_nai_msg(MsgPacketList *ptMsgPacketList)
{
	s32 nStatus = NAI__SUCCESS;
#ifdef _VALIDATE_CRC
	s32 unMsgCRC = 0;

	unMsgCRC = compute_nai_msg_crc(ptMsgPacketList);
	if (unMsgCRC > 0 && unMsgCRC == ptMsgPacketList->ptStart->tNAIMsg.tSerdesPayLd.tTransportHdr.unCRC)
	{
#ifdef _VERBOSE
		printk("Transport ID: 0x%4.4x PASSED CRC! - Calculated: 0x%8.4x  Expected: 0x%8.4x\r\n", ptMsgPacketList->ptStart->tNAIMsg.tSerdesPayLd.tTransportHdr.usID, unMsgCRC, ptMsgPacketList->ptStart->tNAIMsg.tSerdesPayLd.tTransportHdr.unCRC);
#endif
	}		
	else
	{
#ifdef _VERBOSE
		printk("**Transport ID: 0x%4.4x FAILED CRC!** - Calculated: %8.4x  Expected: %8.4x\r\n", ptMsgPacketList->ptStart->tNAIMsg.tSerdesPayLd.tTransportHdr.usID, unMsgCRC, ptMsgPacketList->ptStart->tNAIMsg.tSerdesPayLd.tTransportHdr.unCRC);
#endif
		nStatus = -1;
	}
#else
#ifdef _VERBOSE
		printk("**VALIDATION OF CRC IS TURNED OFF**\r\n");
#endif
#endif
	return nStatus;
}


static bool nai_msg_requires_finished_response(MsgPacketList *ptMsgPacketList)
{
	bool bRequiresResponse = false;		
	MsgPacket *ptMsgPacket = NULL;

	if (ptMsgPacketList != NULL)
	{
		ptMsgPacket = ptMsgPacketList->ptStart;
		if (ptMsgPacket != NULL)
		{
			switch (ptMsgPacket->tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usCommandType)
			{
				case COMMAND_TYPECODE_WRITEEEPROM :
				case COMMAND_TYPECODE_ERASEFLASH  :
				case COMMAND_TYPECODE_WRITEFLASH  :
				case COMMAND_TYPECODE_CONFIG_MICRO :
				case COMMAND_TYPECODE_ERASE_MICRO  :
					bRequiresResponse = true;				
					break;

				default:
					bRequiresResponse = false;
					break;
			}
		}
	}

	return bRequiresResponse;
}

static s32 nai_send_msg(MsgPacketList *ptMsgPackets)
{
	s32 nStatus = NAI__SUCCESS;
	bool bWaitForStatusReply = false;
	s32 i = 0;
	volatile s32 unAddr = 0;
	volatile s32 unBeginTxAddr = 0; 
	s32 nNumLoops = 0; 
	s32 unMsgCRC = 0;
	u8 ucCompleterID = 0;
	u8 ucRequesterID = 0;
	s32 ulTimer = 0;
	MsgList tMsgList;
	MsgPacket *ptTraverse = NULL;
	uint32_t unCompletionTimeout = 0;
	
#ifdef _DEBUG_X
	s32 unTemp = 0;
#endif

#ifdef _VERBOSE
	s32 nLoopCount = 0;
	printk("**************nai_send_msg**************\r\n");
	printk("Sending cmd: %x\n", ptMsgPackets->ptStart->tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usCommandType);
#endif

	bWaitForStatusReply = nai_msg_requires_finished_response(ptMsgPackets);
	unMsgCRC = compute_nai_msg_crc(ptMsgPackets);
	ptTraverse = ptMsgPackets->ptStart;
	
	while (ptTraverse != NULL)
	{
		ucRequesterID = nai_get_serdes_requester_id(ptTraverse);
		ucCompleterID = nai_get_serdes_completer_id(ptTraverse);

#ifdef _VERBOSE
		if (nLoopCount == 0)
		{
			printk("RequesterID = 0x%1x\r\n", ucRequesterID);			
			printk("CompleterID = 0x%1x\r\n", ucCompleterID);
		}
		nLoopCount++;	
#endif		
		//ulTimer = nai_get_timer(0);
		ulTimer = jiffies;
		/* We have a message to send...but the FIFO is not ready...need to wait!*/	
		while (!nai_tx_fifo_empty(ucRequesterID, ucCompleterID))
		{
			/* If FIFO did not get serviced within a reasonable amount of time..get out */
			if (((s32)jiffies - (s32)ulTimer) > COMPLETION_TIMEOUT)
			{
				nStatus = NAI_TX_FIFO_NOT_EMPTY_TIMEOUT;
				break;
			}	
		}

		if (nStatus != NAI__SUCCESS)
			break;
		
		ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.unCRC = unMsgCRC; /* Store the calculated MSG CRC in each packet that makes up the entire MSG */			
		unAddr = nai_get_tx_fifo_address(ucRequesterID, ucCompleterID);
		unBeginTxAddr = nai_get_tx_fifo_pkt_ready_address(ucRequesterID, ucCompleterID);

		nNumLoops = ((TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS + (ptTraverse->tNAIMsg.tSerdesHdr.ucPayloadLength))); /* TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS = 6 Words + Num Words in Serdes PayLoad */
#ifdef _VERBOSE
		printk("Sending Msg Sequence #: %u\r\n", ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usSequenceNum);
		printk("Num Words for FIFO    : %d\r\n", nNumLoops); 
#endif
		for (i=0; i <nNumLoops; i++)
		{
			FIFOValue tFIFOVal;
			tFIFOVal.usLoWord = ptTraverse->tNAIMsg.msg[i++];
			tFIFOVal.usHiWord = ptTraverse->tNAIMsg.msg[i];

#ifdef _DEBUG_X
			printk("FIFO VAL = 0x%8x\r\n", tFIFOVal.unValue);
#endif
			nai_write32_SW(unAddr, tFIFOVal.unValue);
		}

		/* Now force transfer of data now that the FIFO is filled with current message*/
		nai_write32_SW(unBeginTxAddr, (s32)1);
		ptTraverse = ptTraverse->ptNext;				
	}

	/* OK - if we got here and no errors...then we need to wait to get completion status on the msg that was sent */
	if (bWaitForStatusReply && (nStatus == NAI__SUCCESS))
	{		
		/*NOTE: Each command may have different requirements regarding how long they will take to complete.
		 *      Hence, we now attempt to specify a reasonable wait time based upon our knowledge of how long
		 *      a given command typically takes. */
		unCompletionTimeout = nai_get_completion_timeout(ptMsgPackets->ptStart);
				
		/* Wait for packet! */
		ulTimer = jiffies;
		while (!nai_rx_fifo_pkt_ready(ucRequesterID, ucCompleterID))
		{
			/* If FIFO did not get serviced within a reasonable amount of time..get out */
			/* NOTE: We make this timeout longer than majority as we don't know how long module "should" take to fulfill request */
			if (((s32)jiffies - (s32)ulTimer) > unCompletionTimeout)
			{
				nStatus = NAI_RX_FIFO_PKT_NOT_READY_TIMEOUT;
				break;
			}			
		}	

		if (nStatus == NAI__SUCCESS)	
		{				
			/* Prepare for response! */
			init_nai_msgs(&tMsgList);
			nStatus = nai_receive_msg_packet(ucRequesterID, ucCompleterID, &tMsgList);

			if (nStatus == NAI__SUCCESS)
			{
#ifdef _VERBOSE
				print_nai_msgs(&tMsgList, true);
#endif
				if (validate_nai_msgs(&tMsgList) == 0)
				{
					/* NOTE: Shound only have 1 message (1 packet) being returned */
					MsgPacketList *ptFirstMsg = tMsgList.ptStart;
					if (ptFirstMsg != NULL)
					{
						/* Extract the "Execution Status" out of the payload */
						MsgPacket *ptTraverse = ptFirstMsg->ptStart;
						if (ptTraverse != NULL && (ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usPacketPayLdLength >= 2))
						{			
							FIFOValue tFIFOValue;
							tFIFOValue.usLoWord = ptTraverse->tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandPayLd.usData[0];
							tFIFOValue.usHiWord = ptTraverse->tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandPayLd.usData[1];
							nStatus = (s32)tFIFOValue.unValue; 
						}
					}				
				}
			}

			delete_nai_msgs(&tMsgList);
		}
	}

#ifdef _VERBOSE
	printk("**************END nai_send_msg**************\r\n");
#endif

	return nStatus;
}

static s32 nai_receive_msg_packet(u8 ucRequesterID, u8 ucCompleterID, MsgList *ptMsgList)
{	
	s32 nStatus = NAI__SUCCESS;
	volatile s32 unAddr = 0;	
	NAIMsg tReceiveMsg;
	FIFOValue tFIFOVal;
	s32 unWordsRead = 0;	
	u16  usSerdesHdrWordsRead = 0;
	s32 i = 0;
	MsgPacketList *ptMsgPacketList = NULL;
	u8 bAddMsgPacketList = 0;
	u16 usPacketLength = 0;

#ifdef _VERBOSE
printk("**************nai_receive_msg_packet**************\r\n");
#endif

	if (ptMsgList == NULL)
		return NAI_INVALID_PARAMETER_VALUE;

	nai_rx_fifo_clear_pkt_ready(ucRequesterID, ucCompleterID);
	memset(tReceiveMsg.msg, 0x0000, sizeof(tReceiveMsg.msg));

	unAddr = nai_get_rx_fifo_address(ucCompleterID);

	/* First we read just the SERDES Header info as we should be guaranteed this is present */
    /* From there, we can determine how many words are part of this SERDES packet */
	for (i=0; i < TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS; i++)
	{	
		tFIFOVal.unValue = nai_read32_SW(unAddr);	

#ifdef _DEBUG_X
//#ifdef _VERBOSE
		printk("Read SERDES HDR FIFO Value: 0x%8x\r\n", tFIFOVal.unValue);
#endif
		tReceiveMsg.msg[i++] = 	tFIFOVal.usLoWord;
		tReceiveMsg.msg[i] = tFIFOVal.usHiWord;
		usSerdesHdrWordsRead += 2;
	}
		
	/* Now we read enough to know how many words are part of this SERDES packet...so let's read the desired amount off of the FIFO */
	usPacketLength = (tReceiveMsg.tSerdesHdr.ucPayloadLength); /* Payload Length for SERDES are bits 0 - 7 */
	while (unWordsRead < usPacketLength)
	{
		tFIFOVal.unValue = nai_read32_SW(unAddr);

#ifdef _DEBUG_X
//#ifdef _VERBOSE
		printk("Read FIFO Value: 0x%8x\r\n", tFIFOVal.unValue);		
#endif

		tReceiveMsg.msg[i++] = 	tFIFOVal.usLoWord;
		unWordsRead++;
		if (unWordsRead < usPacketLength)
		{
			tReceiveMsg.msg[i++] = tFIFOVal.usHiWord;
			unWordsRead++;
		}
	}
	unWordsRead += usSerdesHdrWordsRead; /* Add back in number of SERDES Header words read */

#ifdef _VERBOSE
	/* Create a new msg packet list and fill it with FIFO data... */
	printk("Transport ID: 0x%4x\r\n", tReceiveMsg.tSerdesPayLd.tTransportHdr.usID);
#endif

	ptMsgPacketList = find_nai_msg_packet_list(ptMsgList, tReceiveMsg.tSerdesPayLd.tTransportHdr.usID);
	if (ptMsgPacketList == NULL)
	{
		ptMsgPacketList = create_nai_msg_packet_list();
		ptMsgPacketList->unWordsLeftToRead = tReceiveMsg.tSerdesPayLd.tTransportHdr.unMsgLength;
		bAddMsgPacketList = 1;
	}
    
	if (unWordsRead > ptMsgPacketList->unWordsLeftToRead)
		ptMsgPacketList->unWordsLeftToRead = 0;
	else
		ptMsgPacketList->unWordsLeftToRead -= unWordsRead;

	create_and_append_nai_msg_packet(ptMsgPacketList, &(tReceiveMsg.msg[0]));

	if (bAddMsgPacketList)
		add_nai_msg(ptMsgList, ptMsgPacketList);
#ifdef _VERBOSE
	printk("Transport ID: 0x%4x Remaining Words 0x%8.4x\r\n", tReceiveMsg.tSerdesPayLd.tTransportHdr.usID, ptMsgPacketList->unWordsLeftToRead);
	printk("**************END nai_receive_msg_packet**************\r\n");
#endif

	return nStatus;
}

static u8 nai_get_serdes_completer_id(MsgPacket *ptMsgPacket)
{
	/* Bits 4 - 7 of Serdes Header 4 is the completer ID */
	return  (u8)ptMsgPacket->tNAIMsg.tSerdesHdr.ucCompleterID;
}

static u8 nai_get_serdes_requester_id(MsgPacket *ptMsgPacket)
{
	/* Bits 0 - 3 of Serdes Header 4 is the Requester ID */
	return (u8)ptMsgPacket->tNAIMsg.tSerdesHdr.ucRequesterID;
}

static u16 calculate_nai_expected_sequence_count(s32 unPayloadWordLength)
{
	u16 usExpectedSequenceCount = 0;
	u16 usTempCount = 0;
	s32 unPayloadWithHdr = 0;

#ifdef _DANT
	if (unPayloadWordLength != 0)
	{
#endif
		usTempCount = (u16)(unPayloadWordLength / MAX_SERDES_MSG_IN_WORDS);
		if ((unPayloadWordLength % MAX_SERDES_MSG_IN_WORDS) > 0)
			usTempCount++;	

		unPayloadWithHdr = ((usTempCount * CONFIG_TOTAL_PKT_HDR_IN_WORDS) + unPayloadWordLength);
		usExpectedSequenceCount = (u16)(unPayloadWithHdr / MAX_SERDES_MSG_IN_WORDS);
		if ((unPayloadWithHdr % MAX_SERDES_MSG_IN_WORDS) > 0)
			usExpectedSequenceCount++;
#ifdef _DANT
	}
	else /* Even with a zero payload...if we are looking to send a command in our CmdHeader...we need to have a sequence count of 1 */
		usExpectedSequenceCount = 1;
#endif		
	return usExpectedSequenceCount;
}


static s32 make_read_request(u16 usCommandType, u8 ucRequesterID, u8 ucCompleterID, u16 usChipID, u32 unOffset, u8 *pucBuf, s32 nLen)
{
	s32 nStatus = NAI__SUCCESS;
	s32 nMsgPacketCount = 0;	
	MsgList tMsgList;
	MsgPacketList *ptMsgPacketList = NULL;
	u16 usID = get_next_tran_id();
	u16 usExpectedSequenceCount = 0;	
	u16 usPacketPayLdLength = 0;
	s32 ulTimer;
	NAIMsg tNAIMsg;
	
	init_nai_msgs(&tMsgList);

	/* Create Retrieve Request */
	ptMsgPacketList = create_nai_msg_packet_list();
	nMsgPacketCount = 0;		
		
	/* Determine expected sequence count...NOTE this takes into account all header info */
	usExpectedSequenceCount = 1;

	nMsgPacketCount++;
			
	memset(&tNAIMsg, 0, sizeof(NAIMsg));

	/* SERDES HEADER */
#ifdef _VERBOSE
	printk("IN make_read_request\r\n");
	printk("RequesterID = 0x%2.2x(%u)\r\n", ucRequesterID, ucRequesterID);
	printk("CompleterID = 0x%2.2x(%u)\r\n", ucCompleterID, ucCompleterID);
#endif
	tNAIMsg.tSerdesHdr.ucType = 3;
	tNAIMsg.tSerdesHdr.ucToHPS = 1;
	tNAIMsg.tSerdesHdr.ucPayloadLength = (u8)(CONFIG_TOTAL_PKT_HDR_IN_WORDS - TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS);
	tNAIMsg.tSerdesHdr.usSERDES2 = 0;
	tNAIMsg.tSerdesHdr.usSERDES3 = 0;
	tNAIMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
	tNAIMsg.tSerdesHdr.ucCompleterID = ucCompleterID;
	tNAIMsg.tSerdesHdr.usSERDES5 = 0;

	/* Serdes Payload length must be a multiple of 2 */
	if ((tNAIMsg.tSerdesHdr.ucPayloadLength % 2) != 0)
		tNAIMsg.tSerdesHdr.ucPayloadLength++;

	/* TRANSPORT HEADER */
	tNAIMsg.tSerdesPayLd.tTransportHdr.usID = usID;
	tNAIMsg.tSerdesPayLd.tTransportHdr.unMsgLength = CONFIG_TOTAL_PKT_HDR_IN_WORDS;    /* Length in Words - Single Packet Request with no payload!*/
	tNAIMsg.tSerdesPayLd.tTransportHdr.usSequenceNum = nMsgPacketCount;
	tNAIMsg.tSerdesPayLd.tTransportHdr.usExpectedSequenceCount = usExpectedSequenceCount;

	/* COMMAND HEADER */
	tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usChipID = usChipID;
	tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usCommandType = usCommandType;
	tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.unOffset = (s32)unOffset;
    tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.unPayLdRequestLength = (s32)nLen;

	/* COMMAND PAYLOAD */
	tNAIMsg.tSerdesPayLd.tTransportHdr.usPacketPayLdLength = 0; /* Packet payload length stored in bytes - No Payload since just a request */

	/* Let's build a linked list of message packets that will represent a full message */
	create_and_append_nai_msg_packet(ptMsgPacketList, tNAIMsg.msg);

	/* Send request for data */
	nStatus = nai_send_msg(ptMsgPacketList); /*Don't need to wait for read request..as we will be waiting below!*/
	delete_nai_msg_packets(ptMsgPacketList);
	aligned_free(ptMsgPacketList);
	ptMsgPacketList = NULL;

	if (nStatus == NAI__SUCCESS)
	{
		/* Prepare for response! */
		init_nai_msgs(&tMsgList);

		/* Wait for packet! */		
		do 
		{
			ulTimer = jiffies;
			while (!nai_rx_fifo_pkt_ready(ucRequesterID, ucCompleterID))
			{
				/* If FIFO did not get serviced within a reasonable amount of time..get out */
				if (((s32)jiffies - (s32)ulTimer) > COMPLETION_TIMEOUT)
				{
					nStatus = NAI_RX_FIFO_PKT_NOT_READY_TIMEOUT;
					break;
				}			
			}

			/* Now fetch the next message packet and append it to the MsgList */
			if (nStatus == NAI__SUCCESS)
				nStatus = nai_receive_msg_packet(ucRequesterID, ucCompleterID, &tMsgList);

		} while	(nStatus == NAI__SUCCESS && tMsgList.ptEnd->unWordsLeftToRead > 0);

		if (nStatus == NAI__SUCCESS)
		{
#ifdef _VERBOSE
			print_nai_msgs(&tMsgList, true);
#endif
			if (validate_nai_msgs(&tMsgList) == 0)
			{
				/* NOTE: Shound only have 1 message (1 + more packets) being returned */
				MsgPacketList *ptFirstMsg = tMsgList.ptStart;
				if (ptFirstMsg != NULL)
				{
					MsgPacket *ptTraverse = ptFirstMsg->ptStart;
					u16 usBufOffset = 0;
					while (ptTraverse != NULL)
					{			
						usPacketPayLdLength = ptTraverse->tNAIMsg.tSerdesPayLd.tTransportHdr.usPacketPayLdLength;
						memcpy((pucBuf + usBufOffset), &(ptTraverse->tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandPayLd.usData[0]), usPacketPayLdLength); /* Copy specified number of bytes from response message into output param */		
						usBufOffset += usPacketPayLdLength; /* Increment offset so we don't overwrite what we already have written */
						ptTraverse = ptTraverse->ptNext;
					}
				}				
			}
		}

		delete_nai_msgs(&tMsgList);
	}

	return nStatus;
}

static s32 make_write_request(u16 usCommandType, u8 ucRequesterID, u8 ucCompleterID, u16 usChipID, u32 unOffset, u8 *pucBuf, s32 nLen)
{
	s32 nStatus = NAI__SUCCESS;
	s32 unMsgPayloadWordLength = convert_bytes_to_words(nLen); /* # of words to write */
	s32 i = 0, k = 0;	
	s32 nMsgPacketCount = 0;
	s32 nPayloadWordsLeftToRead = 0;
	s32 nPayloadWordCount = 0;
	MsgPacketList *ptMsgPacketList = NULL;
	NAIMsg tNAIMsg;		
	u16 usID = get_next_tran_id();
	u16 usExpectedSequenceCount = 0;
	WORDValue tWordValue;

	ptMsgPacketList = create_nai_msg_packet_list();

	nMsgPacketCount = 0;		
	nPayloadWordsLeftToRead = (s32)unMsgPayloadWordLength;		

	/* Determine expected sequence count...NOTE this takes into account all header info */
	usExpectedSequenceCount = calculate_nai_expected_sequence_count(unMsgPayloadWordLength);

	do
	{		
		nMsgPacketCount++;
		memset(&tNAIMsg, 0, sizeof(NAIMsg));

		/* SERDES HEADER */
#ifdef _VERBOSE
	printk("IN make_write_request\r\n");
	printk("RequesterID = 0x%2.2x(%u)\r\n", ucRequesterID, ucRequesterID);
	printk("CompleterID = 0x%2.2x(%u)\r\n", ucCompleterID, ucCompleterID);
#endif
		tNAIMsg.tSerdesHdr.ucType = 3;
		tNAIMsg.tSerdesHdr.ucToHPS = 1;
		tNAIMsg.tSerdesHdr.ucPayloadLength = (u8)(MIN((nPayloadWordsLeftToRead+CONFIG_TOTAL_PKT_HDR_IN_WORDS), MAX_SERDES_MSG_IN_WORDS) - TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS);
		tNAIMsg.tSerdesHdr.usSERDES2 = 0;
		tNAIMsg.tSerdesHdr.usSERDES3 = 0;
		tNAIMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
		tNAIMsg.tSerdesHdr.ucCompleterID = ucCompleterID;
		tNAIMsg.tSerdesHdr.usSERDES5 = 0;

		/* Serdes Payload length must be a multiple of 2 */
		if ((tNAIMsg.tSerdesHdr.ucPayloadLength % 2) != 0)	
			tNAIMsg.tSerdesHdr.ucPayloadLength++;

		/* TRANSPORT HEADER */
		tNAIMsg.tSerdesPayLd.tTransportHdr.usID = usID;
		tNAIMsg.tSerdesPayLd.tTransportHdr.unMsgLength = (unMsgPayloadWordLength + (usExpectedSequenceCount * CONFIG_TOTAL_PKT_HDR_IN_WORDS));    /* Length in Words */
		tNAIMsg.tSerdesPayLd.tTransportHdr.usSequenceNum = nMsgPacketCount;
		tNAIMsg.tSerdesPayLd.tTransportHdr.usExpectedSequenceCount = usExpectedSequenceCount;

		/* COMMAND HEADER */
		tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usChipID = usChipID;
		tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usCommandType = usCommandType;
		tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.unOffset = (s32)unOffset;
	    tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.unPayLdRequestLength = 0x0;

		/* COMMAND PAYLOAD */
#ifdef _VERBOSE
		printk("Num Msg Words To Read: %ld\r\n", nPayloadWordsLeftToRead);
#endif
		nPayloadWordCount = MIN(nPayloadWordsLeftToRead, CONFIG_MAX_PAYLOAD_IN_WORDS);
		
		for (i=0; i < nPayloadWordCount; i++)
		{
			tWordValue.ucLoByte = pucBuf[k++];
			/* It is possible a buffer of "odd" size was passed in...let's make sure we don't try to access more than what we were given */
			if (k < nLen) 
				tWordValue.ucHiByte = pucBuf[k++];
			else
				tWordValue.ucHiByte = 0x00;
		   	tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandPayLd.usData[i] = tWordValue.usValue;
		}

		tNAIMsg.tSerdesPayLd.tTransportHdr.usPacketPayLdLength = (nPayloadWordCount * 2); /*Packet payload length stored in bytes */

		/* Let's build a linked list of message packets that will represent a full message */
		create_and_append_nai_msg_packet(ptMsgPacketList, tNAIMsg.msg);
		nPayloadWordsLeftToRead -= nPayloadWordCount;
#ifdef _VERBOSE
		printk("Num Msg Words Left To Read: %ld\r\n", nPayloadWordsLeftToRead);
#endif

	} while (nPayloadWordsLeftToRead > 0);

	/* Send request to write data */
//printk("make_write_request ... about to send message\r\n");
	nStatus = nai_send_msg(ptMsgPacketList); 
//printk("make_write_request ... after send message\r\n");
	delete_nai_msg_packets(ptMsgPacketList);
	aligned_free(ptMsgPacketList);

	return nStatus;
}

s32 nai_read_module_eeprom_request(u16 usChipID, u8 ucRequesterID, u8 ucCompleterID, u32 unEepromOffset, u8 *pucBuf, s32 nLen) 
{
	s32 nStatus = NAI__SUCCESS;
	nStatus = make_read_request(COMMAND_TYPECODE_READEEPROM, ucRequesterID, ucCompleterID, usChipID, unEepromOffset, pucBuf, nLen);
	return nStatus;
}



s32 nai_write_module_eeprom_request(u16 usChipID, u8 ucRequesterID, u8 ucCompleterID, u32 unEepromOffset, u8 *pucBuf, s32 nLen)
{
	s32 nStatus = NAI__SUCCESS;
	nStatus = make_write_request(COMMAND_TYPECODE_WRITEEEPROM, ucRequesterID, ucCompleterID, usChipID, unEepromOffset, pucBuf, nLen);
	return nStatus;
}

s32 nai_erase_flash_request(u8 ucRequesterID, u8 ucCompleterID, u32 unFlashOffset, u8 ucNumPages)
{
	s32 nStatus = NAI__SUCCESS;
	MsgPacketList *ptMsgPacketList = NULL;
	u16 usID = get_next_tran_id();
	NAIMsg tNAIMsg;
		
	ptMsgPacketList = create_nai_msg_packet_list();
	
	memset(&tNAIMsg, 0, sizeof(NAIMsg));

	/* SERDES HEADER */
	tNAIMsg.tSerdesHdr.ucType = 3;
	tNAIMsg.tSerdesHdr.ucToHPS = 1;
	tNAIMsg.tSerdesHdr.ucPayloadLength = (u8)(CONFIG_TOTAL_PKT_HDR_IN_WORDS - TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS);
	tNAIMsg.tSerdesHdr.usSERDES2 = 0;
	tNAIMsg.tSerdesHdr.usSERDES3 = 0;
	tNAIMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
	tNAIMsg.tSerdesHdr.ucCompleterID = ucCompleterID;
	tNAIMsg.tSerdesHdr.usSERDES5 = 0;

	/* TRANSPORT HEADER */
	tNAIMsg.tSerdesPayLd.tTransportHdr.usID = usID;
	tNAIMsg.tSerdesPayLd.tTransportHdr.unMsgLength = CONFIG_TOTAL_PKT_HDR_IN_WORDS;    /* Length in Words */
	tNAIMsg.tSerdesPayLd.tTransportHdr.usSequenceNum = 1;
	tNAIMsg.tSerdesPayLd.tTransportHdr.usExpectedSequenceCount = 1;

	/* COMMAND HEADER */
	tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usChipID = 0;
	tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usCommandType = COMMAND_TYPECODE_ERASEFLASH;
	tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.unOffset = unFlashOffset;
    tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.unPayLdRequestLength = (s32)ucNumPages;  /* Stuff num pages into PayLdRequestLength just to get the data across */

	/* COMMAND PAYLOAD */
	tNAIMsg.tSerdesPayLd.tTransportHdr.usPacketPayLdLength = 0; /*Packet payload length stored in bytes */

	/* Let's build a linked list of message packets that will represent a full message */
	create_and_append_nai_msg_packet(ptMsgPacketList, tNAIMsg.msg);

	/* Send request to write data */
	nStatus = nai_send_msg(ptMsgPacketList); 
	delete_nai_msg_packets(ptMsgPacketList);
	aligned_free(ptMsgPacketList);

	return nStatus;
}

s32 nai_read_module_flash_request(u8 ucRequesterID, u8 ucCompleterID, u32 unFlashOffset, u8 *pucBuf, s32 nLen)
{
	s32 nStatus = NAI__SUCCESS;
	nStatus = make_read_request(COMMAND_TYPECODE_READFLASH, ucRequesterID, ucCompleterID, 0, unFlashOffset, pucBuf, nLen);
	return nStatus;
}

s32 nai_write_module_flash_request(u8 ucRequesterID, u8 ucCompleterID, u32 unFlashOffset, u8 *pucBuf, s32 nLen)
{
	s32 nStatus = NAI__SUCCESS;
	nStatus = make_write_request(COMMAND_TYPECODE_WRITEFLASH, ucRequesterID, ucCompleterID, 0, unFlashOffset, pucBuf, nLen);
	return nStatus;
}

s32 nai_write_micro_request(u8 ucRequesterID, u8 ucCompleterID, u8 ucChannel, u32 unOffset, u8 *pucBuf, s32 nLen)
{
	s32 nStatus = NAI__SUCCESS;
	nStatus = make_write_request(COMMAND_TYPECODE_CONFIG_MICRO, ucRequesterID, ucCompleterID, (u16)ucChannel, unOffset, pucBuf, nLen);
	return nStatus;
}

s32 nai_get_micro_request(u8 ucRequesterID, u8 ucCompleterID, u8 ucChannel, u8 *pucBuf, s32 nLen)
{
	s32 nStatus = NAI__SUCCESS;
	/*NOTE: We pass ucChannel in as chip ID - this will allow us to determine on the module side which channel user is requesting version info from */
	nStatus = make_read_request(COMMAND_TYPECODE_GET_MICRO, ucRequesterID, ucCompleterID, (u16)ucChannel, 0, pucBuf, nLen);
	return nStatus;
}

s32 nai_erase_micro_request(u8 ucRequesterID, u8 ucCompleterID, u8 ucChannel)
{
	s32 nStatus = NAI__SUCCESS;
	nStatus = make_write_request(COMMAND_TYPECODE_ERASE_MICRO, ucRequesterID, ucCompleterID, (u16)ucChannel, 0, NULL, 0);
	return nStatus;	
}

static u32 nai_get_completion_timeout(MsgPacket *ptMsgPacket)
{
	/* Default timeout */
//	u32 unCompletionTimeout = (COMPLETION_TIMEOUT * 4);
	u32 unCompletionTimeout = (COMPLETION_TIMEOUT * 10); /*DANT used to be multiplied by 4 until X2 module with Micron QSPI now takes longer for erase! */
	u8 ucNumChannels = 1;
	
	if (ptMsgPacket != NULL)
	{	    
		switch (ptMsgPacket->tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usCommandType)
		{
			case COMMAND_TYPECODE_CONFIG_MICRO :
			case COMMAND_TYPECODE_ERASE_MICRO :
			if (ptMsgPacket->tNAIMsg.tSerdesPayLd.tTransportPayLd.tCommandHdr.usChipID == (uint32_t)ALL_CHANNELS)
				ucNumChannels = NUM_DT_CHANNELS;			
			else
				ucNumChannels = 1;
			unCompletionTimeout *= (ucNumChannels + 2);  /* Multiply the standard timeout by the number of channels we will be processing + 2 (buffer) */
			break;
			
			default :			
			break;
		}
	}
	
	return unCompletionTimeout;
}

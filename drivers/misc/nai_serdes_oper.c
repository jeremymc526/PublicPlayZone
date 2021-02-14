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

#include "nai_serdes.h"
#include "nai_serdes_prv.h"
#include "nai_serdes_utils.h"
#include "nai_serdes_oper.h"

#define NAI_MAX_AVAIL_SLOTS         10

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static struct semaphore	g_SERDES_Mutex[NAI_MAX_AVAIL_SLOTS + 1];

static s32 nai_send_serdes_oper_msg(NAIOperMsg *ptNAIOperMsg);
static s32 nai_receive_serdes_oper_msg(NAIOperMsg *ptNAIOperMsg);
static s32 initSERDES_Mutex(u8 ucSlotID);
static s32 lockSERDES_Mutex(u8 ucSlotID);
static s32 unlockSERDES_Mutex(u8 ucSlotID);

static s32 initSERDES_Mutex(u8 ucSlotID) {
	sema_init(&g_SERDES_Mutex[ucSlotID], 1);

	return NAI__SUCCESS;
}

static s32 lockSERDES_Mutex(u8 ucSlotID) {
	s32 nStatus = NAI__SUCCESS;

	if (unlikely(down_timeout(&g_SERDES_Mutex[ucSlotID], 4 * HZ)))
		nStatus = NAI_UNABLE_TO_LOCK_MUTEX;

	return nStatus;
}

static s32 unlockSERDES_Mutex(u8 ucSlotID) {
	up(&g_SERDES_Mutex[ucSlotID]);

	return NAI__SUCCESS;
}

s32 nai_init_msg_utils(u8 ucID) {
	u32 i;
	s32 nStatus;

	nStatus = nai_init_as_slot(ucID);
	if (nStatus == NAI__SUCCESS)
		for (i = 0; i <= NAI_MAX_AVAIL_SLOTS; ++i)
			initSERDES_Mutex(i);

	return nStatus;
}

s32 nai_read_reg16_request(u32 unAddress, u16 *pusValue) {
	s32 nStatus = NAI__SUCCESS;
	u8 ucCompleterID = 0;
	u32 unModuleOffset = 0;

	nStatus = nai_get_module_id_and_address(unAddress, &ucCompleterID,
						&unModuleOffset);

	if (nStatus == NAI__SUCCESS)
		nStatus = nai_read_reg16_by_slot_request(ucCompleterID,
							 unModuleOffset,
							 pusValue);

	return nStatus;
}

s32 nai_write_reg16_request(u32 unAddress, u16 usValue) {
	s32 nStatus = NAI__SUCCESS;
	u8 ucCompleterID = 0;
	u32 unModuleOffset = 0;

	nStatus = nai_get_module_id_and_address(unAddress, &ucCompleterID,
						&unModuleOffset);

	if (nStatus == NAI__SUCCESS)
		nStatus = nai_write_reg16_by_slot_request(ucCompleterID,
							  unModuleOffset,
							  usValue);

	return nStatus;
}

s32 nai_read_reg32_request(u32 unAddress, u32 *punValue) {
	s32 nStatus = NAI__SUCCESS;
	u8 ucCompleterID = 0;
	u32 unModuleOffset = 0;	
	
	if (nai_is_hw_serdes_capable()) {

		nStatus = nai_get_module_id_and_address(unAddress, &ucCompleterID,
							&unModuleOffset);

		if (nStatus == NAI__SUCCESS) {
			if (lockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS)
				return NAI_UNABLE_TO_LOCK_MUTEX;
				
			/* NOTE: We need to take into account linux open address space starting at 0x70004000 is module 1
					 so in essence...to access the zero offset of module 1 we need to pass an address of 0x00000000. */
			if (ucCompleterID > 0 && unAddress >= 0x00004000) {
				unAddress -= 0x00004000;				
				*punValue = nai_read32(unAddress);
			}
			else
				nStatus = NAI_COMMAND_FAILED;
		
			if (unlockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS) {
				if (nStatus == NAI__SUCCESS)
					nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;	
			}
		}
	}
	/* revert to SW SERDES */
	else { 
		NAIOperMsg tNAIOperMsg;

		memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

		nStatus = nai_get_module_id_and_address(unAddress, &ucCompleterID,
							&unModuleOffset);

		if (nStatus == NAI__SUCCESS)
			nStatus = nai_read_reg32_by_slot_request(ucCompleterID,
								 unModuleOffset,
								 punValue);		
	}
	return nStatus;
}

s32 nai_write_reg32_request(u32 unAddress, u32 unValue) {
	s32 nStatus = NAI__SUCCESS;
	u8 ucCompleterID = 0;
	u32 unModuleOffset = 0;
	
	if (nai_is_hw_serdes_capable()) {
		nStatus = nai_get_module_id_and_address(unAddress, &ucCompleterID,
							&unModuleOffset);

		if (nStatus == NAI__SUCCESS) {
			if (lockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS)
				return NAI_UNABLE_TO_LOCK_MUTEX;

			/* NOTE: We need to take into account linux open address space starting at 0x70004000 is module 1
					 so in essence...to access the zero offset of module 1 we need to pass an address of 0x00000000. */
			if (ucCompleterID > 0 && unAddress >= 0x00004000) {
				unAddress -= 0x00004000;			
				nai_write32(unAddress, unValue);
			}
		
			if (unlockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS) {
				if (nStatus == NAI__SUCCESS)
					nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
			}
		}
	}
	/* revert to SW SERDES */
	else {
		nStatus = nai_get_module_id_and_address(unAddress, &ucCompleterID,
							&unModuleOffset);

		if (nStatus == NAI__SUCCESS)
			nStatus = nai_write_reg32_by_slot_request(ucCompleterID,
								  unModuleOffset,
								  unValue);		
	}
	return nStatus;
}

s32 nai_read_block16_request(u32 unStartAddress, u32 unCount,
			     u8 ucStride, u16 *pusDataBuf) {
	s32 nStatus = NAI__SUCCESS;
	
	if (nai_is_hw_block_serdes_capable()) {
		u32 unAddressOffset = 0;
		u32 unModuleOffset = 0;
		u8 ucCompleterID = 0;	
		u8 ucPackMode = 1;
		u8 ucExtraRead = 0;
		u32 unBlockConfigVal = 0;
		u32 unBlockChunkCount = 0;
		u32 unNumDataTransactions = 0;
		u32 unDataTransferLeftInWords = unCount;
		u16 i = 0;
		u32 k = 0;
		FIFOValue tValue;
		
		/* Must be 32 bit aligned */
		if ((unStartAddress & 0x0003) != 0)
			return NAI_MIS_ALIGNED_BYTE_ENABLE;
			
		/* SERDES uses 32 bit addressing so make sure Stide is a multiple of 4 */
		if ((ucStride % 4) != 0)
			return NAI_STRIDE_CAUSES_MISALIGNMENT;

		/* If caller requests odd number of 16 bit reads...we need to treat the last read as a non-packing 32 bit read so we don't read more registers than what user requested (bad things could happen if we did under a stride of 0 - FIFO) */
		if ((unDataTransferLeftInWords % 2) > 0)
		{
			ucExtraRead = 1;
			unDataTransferLeftInWords--; /* Subtract 1 so we get even number of words to transfer using packing */
		}

		nStatus = nai_get_module_id_and_address(unStartAddress, &ucCompleterID,
							&unModuleOffset);
							
		if (nStatus == NAI__SUCCESS) {
			nStatus = nai_get_module_packet_config_address_offset(unStartAddress, ucCompleterID, &unAddressOffset);
		
			if (nStatus == NAI__SUCCESS) {
				/* NOTE: We need to take into account linux open address space starting at 0x70004000 is module 1
						 so in essence...to access the zero offset of module 1 we need to pass an address of 0x00000000. */
				if (ucCompleterID > 0 && unStartAddress >= 0x00004000) 
					unStartAddress -= 0x00004000;	
										
				do {
					/* First we need to tell the hardware what we want regarding block read request */
					unBlockConfigVal = 0;
					unBlockChunkCount = MIN(unDataTransferLeftInWords, NAI_OPER_MAX_PAYLOAD_IN_WORDS); 
					unNumDataTransactions = (unBlockChunkCount/2);					
					unBlockConfigVal = (unBlockChunkCount | ((ucStride/4) << 8) | (ucPackMode << 16));	/* HW views stride in 32 bit DWORDS not BYTES */
								
					/* NOTE: We purposely lock and unlock for each block of packet reads as to not starve out other requests. */
					if (lockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS)
						nStatus = NAI_UNABLE_TO_LOCK_MUTEX;										
					
					if (nStatus == NAI__SUCCESS) {				
						
						nai_write_32_ModPktCfg(unAddressOffset, unBlockConfigVal);
						
						/* Now we make desired number of read calls and HW will take care of SERDES packets for a block chunk up to 250 WORDS (or 125 DWORDS) */
						for (i=0; i < unNumDataTransactions; i++) {	
							tValue.unValue = nai_read32(unStartAddress);
							pusDataBuf[k] = tValue.usLoWord;
							pusDataBuf[k+1] = tValue.usHiWord;
							unDataTransferLeftInWords -= 2;
							k += 2;
								
							unStartAddress += (ucStride * 2); /* Need to multiply by 2 here since we are packing and Keith willl be splitting our 32 bit value and placing lower 16 in one register and the upper 16 in a 2nd register hence...the need to multipy start address by 2 for next iteration */							
						}			
						
						if (unlockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS) {
							if (nStatus == NAI__SUCCESS)
								nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;				
						}				
					}
				} while (unDataTransferLeftInWords > 0 && nStatus == NAI__SUCCESS);
				
				/* If we had an odd count requested, we need to read the last 16 bit value straight without packing */
				if (ucExtraRead > 0)
				{
					tValue.unValue = nai_read32(unStartAddress);
					pusDataBuf[k] = tValue.usLoWord;
				}				
			}
		}		
	}
	/* revert to SW SERDES */
	else {	
		u8 ucCompleterID = 0;
		u32 unModuleOffset = 0;

		nStatus = nai_get_module_id_and_address(unStartAddress, &ucCompleterID,
							&unModuleOffset);

		if (nStatus == NAI__SUCCESS)
			nStatus = nai_read_block16_by_slot_request(ucCompleterID,
								   unModuleOffset,
								   unCount, ucStride,
								   pusDataBuf);
	}
	
	return nStatus;
}

s32 nai_write_block16_request(u32 unStartAddress, u32 unCount,
			      u8 ucStride, u16 *pusDataBuf) {
	s32 nStatus = NAI__SUCCESS;
	
	if (nai_is_hw_block_serdes_capable()) {
		u32 unAddressOffset = 0;
		u32 unModuleOffset = 0;
		u8 ucCompleterID = 0;
		u8 ucPackMode = 1;	
		u8 ucExtraWrite = 0;
		u32 unBlockConfigVal = 0;
		u32 unBlockChunkCount = 0;
		u32 unNumDataTransactions = 0;
		u32 unDataTransferLeftInWords = unCount; 
		u16 i = 0;
		u32 k = 0;		
		FIFOValue tValue;
				
		/* Must be 32 bit aligned */
		if ((unStartAddress & 0x0003) != 0)
			return NAI_MIS_ALIGNED_BYTE_ENABLE;
			
		/* SERDES uses 32 bit addressing so make sure Stide is a multiple of 4 */
		if ((ucStride % 4) != 0)
			return NAI_STRIDE_CAUSES_MISALIGNMENT;

		/* If caller requests odd number of 16 bit writes...we need to treat the last write as a non-packing 32 bit write so we don't stomp on more registers than what user requested */
		if ((unDataTransferLeftInWords % 2) > 0)
		{
			ucExtraWrite = 1;
			unDataTransferLeftInWords--; /* Subtract 1 so we get even number of words to transfer using packing */
		}

		nStatus = nai_get_module_id_and_address(unStartAddress, &ucCompleterID,
							&unModuleOffset);
							
		if (nStatus == NAI__SUCCESS) {
			nStatus = nai_get_module_packet_config_address_offset(unStartAddress, ucCompleterID, &unAddressOffset);
		
			if (nStatus == NAI__SUCCESS) {
				/* NOTE: We need to take into account linux open address space starting at 0x70004000 is module 1
						 so in essence...to access the zero offset of module 1 we need to pass an address of 0x00000000. */
				if (ucCompleterID > 0 && unStartAddress >= 0x00004000) 
					unStartAddress -= 0x00004000;	
										
				do {
					/* First we need to tell the hardware what we want regarding block write request */
					unBlockConfigVal = 0;
					unBlockChunkCount = MIN(unDataTransferLeftInWords, NAI_OPER_MAX_PAYLOAD_IN_WORDS);
					unNumDataTransactions = (unBlockChunkCount/2);
					unBlockConfigVal = (unBlockChunkCount | ((ucStride/4) << 8) | (ucPackMode << 16)); /* HW views stride in 32 bit DWORDS not BYTES also since we are packing we will only have 1/2 the number of writes */
												
					/* NOTE: We purposely lock and unlock for each block of packet writes as to not starve out other requests. */
					if (lockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS)
						nStatus = NAI_UNABLE_TO_LOCK_MUTEX;												
					
					if (nStatus == NAI__SUCCESS) {				
						
						nai_write_32_ModPktCfg(unAddressOffset, unBlockConfigVal);
						
						/* Now we make desired number of write calls and HW will take care of SERDES packets for a block chunk up to 250 WORDS (or 125 DWORDS) */
						for (i=0; i < unNumDataTransactions; i++) {	
							tValue.usLoWord = pusDataBuf[k];
							tValue.usHiWord = pusDataBuf[k+1];								
							nai_write32(unStartAddress, tValue.unValue);
							unDataTransferLeftInWords -= 2;
							k += 2;
							
							unStartAddress += (ucStride * 2); /* Need to multiply by 2 here since we are packing and Keith willl be splitting our 32 bit value and placing lower 16 in one register and the upper 16 in a 2nd register hence...the need to multipy start address by 2 for next iteration */
						}			
						
						if (unlockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS) {
							if (nStatus == NAI__SUCCESS)
								nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;				
						}				
					}
				} while (unDataTransferLeftInWords > 0 && nStatus == NAI__SUCCESS);
				
				/* If we had an odd count requested, we need to take the last 16 bit value and write it straight out without packing */
				if (ucExtraWrite > 0)
				{
					tValue.usLoWord = pusDataBuf[k];
					tValue.usHiWord = 0;
					nai_write32(unStartAddress, tValue.unValue);
				}
			}
		}		
	}
	/* revert to SW SERDES */
	else {
		u8 ucCompleterID = 0;
		u32 unModuleOffset = 0;

		nStatus = nai_get_module_id_and_address(unStartAddress, &ucCompleterID,
							&unModuleOffset);

		if (nStatus == NAI__SUCCESS)
			nStatus = nai_write_block16_by_slot_request(ucCompleterID,
									unModuleOffset,
									unCount, ucStride,
									pusDataBuf);		
	}
	
	return nStatus;
}

s32 nai_read_block32_request(u32 unStartAddress, u32 unCount,
			     u8 ucStride, u32 *punDataBuf) {
	s32 nStatus = NAI__SUCCESS;
	
	if (nai_is_hw_block_serdes_capable()) {
		u32 unAddressOffset = 0;
		u32 unModuleOffset = 0;
		u8 ucCompleterID = 0;	
		u32 unBlockConfigVal = 0;
		u32 unBlockChunkCount = 0;
		u32 unNumDataTransactions = 0;
		u32 unDataTransferLeftInWords = (unCount * 2); /* Multiply by 2 since HW expects payload length in 16 bit words and our count is in 32 bit words */ 
		u16 i = 0;
		u32 k = 0;
		
		/* Must be 32 bit aligned */
		if ((unStartAddress & 0x0003) != 0)
			return NAI_MIS_ALIGNED_BYTE_ENABLE;
			
		/* SERDES uses 32 bit addressing so make sure Stide is a multiple of 4 */
		if ((ucStride % 4) != 0)
			return NAI_STRIDE_CAUSES_MISALIGNMENT;

		nStatus = nai_get_module_id_and_address(unStartAddress, &ucCompleterID,
							&unModuleOffset);
							
		if (nStatus == NAI__SUCCESS) {
			nStatus = nai_get_module_packet_config_address_offset(unStartAddress, ucCompleterID, &unAddressOffset);
		
			if (nStatus == NAI__SUCCESS) {
				/* NOTE: We need to take into account linux open address space starting at 0x70004000 is module 1
						 so in essence...to access the zero offset of module 1 we need to pass an address of 0x00000000. */
				if (ucCompleterID > 0 && unStartAddress >= 0x00004000) 
					unStartAddress -= 0x00004000;	
										
				do {
					/* First we need to tell the hardware what we want regarding block read request */
					unBlockConfigVal = 0;
					unBlockChunkCount = MIN(unDataTransferLeftInWords, NAI_OPER_MAX_PAYLOAD_IN_WORDS);
					unNumDataTransactions = (unBlockChunkCount / 2);
					unBlockConfigVal = (unBlockChunkCount | ((ucStride/4) << 8)); /* HW views stride in 32 bit DWORDS not BYTES */						
					
					/* NOTE: We purposely lock and unlock for each block of packet reads as to not starve out other requests. */
					if (lockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS)
						nStatus = NAI_UNABLE_TO_LOCK_MUTEX;										
					
					if (nStatus == NAI__SUCCESS) {				
						
						nai_write_32_ModPktCfg(unAddressOffset, unBlockConfigVal);
						
						/* Now we make desired number of read calls and HW will take care of SERDES packets for a block chunk up to 250 WORDS (or 125 DWORDS) */
						for (i=0; i < unNumDataTransactions; i++) {	
							punDataBuf[k] = nai_read32(unStartAddress);
							unStartAddress += ucStride;
							k++;
						}			
						
						if (unDataTransferLeftInWords >= NAI_OPER_MAX_PAYLOAD_IN_WORDS)
							unDataTransferLeftInWords -= NAI_OPER_MAX_PAYLOAD_IN_WORDS;
						else
							unDataTransferLeftInWords = 0;
						
						if (unlockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS) {
							if (nStatus == NAI__SUCCESS)
								nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;				
						}				
					}
				} while (unDataTransferLeftInWords > 0 && nStatus == NAI__SUCCESS);
			}
		}
	}
	/* revert to SW BLOCK Serdes */
	else {
		u8 ucCompleterID = 0;
		u32 unModuleOffset = 0;

		nStatus = nai_get_module_id_and_address(unStartAddress, &ucCompleterID,
							&unModuleOffset);

		if (nStatus == NAI__SUCCESS)
			nStatus = nai_read_block32_by_slot_request(ucCompleterID,
								   unModuleOffset,
								   unCount, ucStride,
								   punDataBuf);		
	}
	return nStatus;
}

s32 nai_write_block32_request(u32 unStartAddress, u32 unCount,
			      u8 ucStride, u32 *punDataBuf) {
	s32 nStatus = NAI__SUCCESS;
	
	if (nai_is_hw_block_serdes_capable()) {	
		u32 unAddressOffset = 0;
		u32 unModuleOffset = 0;
		u8 ucCompleterID = 0;	
		u32 unBlockConfigVal = 0;
		u32 unBlockChunkCount = 0;
		u32 unNumDataTransactions = 0;
		u32 unDataTransferLeftInWords = (unCount * 2); /* Multiply by 2 since HW expects payload length in 16 bit words and our count is in 32 bit words */ 
		u16 i = 0;
		
		/* Must be 32 bit aligned */
		if ((unStartAddress & 0x0003) != 0)
			return NAI_MIS_ALIGNED_BYTE_ENABLE;
			
		/* SERDES uses 32 bit addressing so make sure Stide is a multiple of 4 */
		if ((ucStride % 4) != 0)
			return NAI_STRIDE_CAUSES_MISALIGNMENT;

		nStatus = nai_get_module_id_and_address(unStartAddress, &ucCompleterID,
							&unModuleOffset);
							
		if (nStatus == NAI__SUCCESS) {
			nStatus = nai_get_module_packet_config_address_offset(unStartAddress, ucCompleterID, &unAddressOffset);
		
			if (nStatus == NAI__SUCCESS) {
				/* NOTE: We need to take into account linux open address space starting at 0x70004000 is module 1
						 so in essence...to access the zero offset of module 1 we need to pass an address of 0x00000000. */
				if (ucCompleterID > 0 && unStartAddress >= 0x00004000) 
					unStartAddress -= 0x00004000;	
										
				do {
					/* First we need to tell the hardware what we want regarding block write request */
					unBlockConfigVal = 0;
					unBlockChunkCount = MIN(unDataTransferLeftInWords, NAI_OPER_MAX_PAYLOAD_IN_WORDS); 
					unNumDataTransactions = (unBlockChunkCount / 2);
					unBlockConfigVal = (unBlockChunkCount | ((ucStride/4) << 8)); /* HW views stride in 32 bit DWORDS not BYTES */
													
					/* NOTE: We purposely lock and unlock for each block of packet writes as to not starve out other requests. */
					if (lockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS)
						nStatus = NAI_UNABLE_TO_LOCK_MUTEX;												
					
					if (nStatus == NAI__SUCCESS) {				
						
						nai_write_32_ModPktCfg(unAddressOffset, unBlockConfigVal);
						
						/* Now we make desired number of write calls and HW will take care of SERDES packets for a block chunk up to 250 WORDS (or 125 DWORDS) */
						for (i=0; i < unNumDataTransactions; i++) {	
							nai_write32(unStartAddress, *punDataBuf);
							unStartAddress += ucStride;
							punDataBuf++;
						}			
						
						if (unDataTransferLeftInWords >= NAI_OPER_MAX_PAYLOAD_IN_WORDS)
							unDataTransferLeftInWords -= NAI_OPER_MAX_PAYLOAD_IN_WORDS;
						else
							unDataTransferLeftInWords = 0;
						
						if (unlockSERDES_Mutex(ucCompleterID) != NAI__SUCCESS) {
							if (nStatus == NAI__SUCCESS)
								nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;				
						}				
					}
				} while (unDataTransferLeftInWords > 0 && nStatus == NAI__SUCCESS);
			}
		}
	}
	/* revert to SW BLOCK Serdes */
	else {
		u8 ucCompleterID = 0;
		u32 unModuleOffset = 0;

		nStatus = nai_get_module_id_and_address(unStartAddress, &ucCompleterID,
							&unModuleOffset);

		if (nStatus == NAI__SUCCESS)
			nStatus = nai_write_block32_by_slot_request(ucCompleterID,
									unModuleOffset,
									unCount, ucStride,
									punDataBuf);		
	}
		    
	return nStatus;
}

static s32 nai_send_serdes_oper_msg(NAIOperMsg *ptNAIOperMsg) {
	s32 nStatus = NAI__SUCCESS;
	u32 i = 0;
	u32 unAddr = 0;
	u32 unBeginTxAddr = 0;
	u32 nNumLoops = 0;
	FIFOValue tFIFOVal;
	u32 ulTimer;

	if (ptNAIOperMsg == NULL)
		return NAI_INVALID_PARAMETER_VALUE;

	ulTimer = jiffies;
	while (!nai_tx_fifo_empty(ptNAIOperMsg->tSerdesHdr.ucRequesterID,
				  ptNAIOperMsg->tSerdesHdr.ucCompleterID)) {
		if (((s32)jiffies - (s32)ulTimer) > COMPLETION_TIMEOUT) {
			nStatus = NAI_TX_FIFO_NOT_EMPTY_TIMEOUT;
			break;
		}
		schedule();
	}

	if (nStatus == NAI__SUCCESS) {
		unAddr = nai_get_tx_fifo_address(ptNAIOperMsg->tSerdesHdr.ucRequesterID,
						 ptNAIOperMsg->tSerdesHdr.ucCompleterID);
		unBeginTxAddr = nai_get_tx_fifo_pkt_ready_address(ptNAIOperMsg->tSerdesHdr.ucRequesterID,
								  ptNAIOperMsg->tSerdesHdr.ucCompleterID);

		if ((ptNAIOperMsg->tSerdesHdr.ucType & 0x01) == 0x01)
			nNumLoops = ((TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS +
				      (ptNAIOperMsg->tSerdesHdr.ucPayloadLength)));
		else
			nNumLoops = TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS;

		for (i = 0; i <nNumLoops; ++i) {
			tFIFOVal.usLoWord = ptNAIOperMsg->msg[i++];
			tFIFOVal.usHiWord = ptNAIOperMsg->msg[i];

			nai_write32_SW(unAddr, tFIFOVal.unValue);
		}

		nai_write32_SW(unBeginTxAddr, 0x01);
	}

	return nStatus;
}

static s32 nai_receive_serdes_oper_msg(NAIOperMsg *ptNAIOperMsg) {
	s32 nStatus = NAI__SUCCESS;
	u32 unAddr = 0;
	FIFOValue tFIFOVal;
	u32 unWordsRead = 0;
	u16  usSerdesHdrWordsRead = 0;
	s32 i = 0;
	u16 usPacketLength = 0;

	if (ptNAIOperMsg == NULL)
		return NAI_INVALID_PARAMETER_VALUE;

	nai_rx_fifo_clear_pkt_ready(ptNAIOperMsg->tSerdesHdr.ucRequesterID,
				    ptNAIOperMsg->tSerdesHdr.ucCompleterID);

	unAddr = nai_get_rx_fifo_address(ptNAIOperMsg->tSerdesHdr.ucCompleterID);

	/* First we read just the SERDES Header info as we should be guaranteed this is present */
    /* From there, we can determine how many words are part of this SERDES packet */
	for (i = 0; i < TOTAL_SERDES_READWRITEREQ_HDR_IN_WORDS; ++i) {
		tFIFOVal.unValue = nai_read32_SW(unAddr);

		ptNAIOperMsg->msg[i++] 	= tFIFOVal.usLoWord;
		ptNAIOperMsg->msg[i] 	= tFIFOVal.usHiWord;
		usSerdesHdrWordsRead += 2;
	}

	/* Now we read enough to know how many words are part of this SERDES packet...so let's read the desired amount off of the FIFO */
	usPacketLength = (ptNAIOperMsg->tSerdesHdr.ucPayloadLength); /* Payload Length for SERDES are bits 0 - 7 */	
	while (unWordsRead < usPacketLength) {
		tFIFOVal.unValue = nai_read32_SW(unAddr);

		ptNAIOperMsg->msg[i++] = tFIFOVal.usLoWord;
		unWordsRead++;
		if (unWordsRead < usPacketLength) {
			ptNAIOperMsg->msg[i++] = tFIFOVal.usHiWord;
			unWordsRead++;
		}
	}

	return nStatus;
}

s32 nai_read_reg16_by_slot_request(u8 ucSlotID, u32 unModuleOffset,
				   u16 *pusValue) {
	s32 nStatus = NAI__SUCCESS;
	
	FIFOValue tFIFOVal;
	u8 ucCompleterID = ucSlotID;
	u8 ucRequesterID = nai_get_global_slot_id();
	u32 ulTimer;
	NAIOperMsg tNAIOperMsg;

	if (lockSERDES_Mutex(ucSlotID) != NAI__SUCCESS)
		return NAI_UNABLE_TO_LOCK_MUTEX;

	memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

	tNAIOperMsg.tSerdesHdr.ucType = NAI_SERDES_READREG;
	tNAIOperMsg.tSerdesHdr.ucToHPS = 0;

	if ((unModuleOffset & 0x0003) == 0)
		tNAIOperMsg.tSerdesHdr.ucByteEnable = 0x3;
	else if ((unModuleOffset & 0x0003) == 2)
		tNAIOperMsg.tSerdesHdr.ucByteEnable = 0xC;
	else
		nStatus = NAI_MIS_ALIGNED_BYTE_ENABLE;

	if (nStatus == NAI__SUCCESS) {
		tNAIOperMsg.tSerdesHdr.ucPayloadLength = (u8)(2); /* No Payload for making the request...but we must tell Keith how much data we are expecting! */
		tNAIOperMsg.tSerdesHdr.ucBlockAddrIncrVal = 0; /* Make sure we have a zero "stride" for a single register read */

		tFIFOVal.unValue = unModuleOffset;
		tNAIOperMsg.tSerdesHdr.usAddressLo = tFIFOVal.usLoWord;
		tNAIOperMsg.tSerdesHdr.usAddressHi = tFIFOVal.usHiWord;

		tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
		tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

		/* Send request for data */
		nStatus = nai_send_serdes_oper_msg(&tNAIOperMsg);

		if (nStatus == NAI__SUCCESS) {			
			/* ** NOW WAIT FOR RESPONSE ** */
			
			/* Prepare for response! */
			memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

			/* Wait for packet! */
			ulTimer = jiffies;
			while (!nai_rx_fifo_pkt_ready(ucRequesterID, ucCompleterID)) {
		     		if (((s32)jiffies - (s32)ulTimer) > COMPLETION_TIMEOUT) {
					nStatus = NAI_RX_FIFO_PKT_NOT_READY_TIMEOUT;
					break;
				}
				schedule();
			}

			if (nStatus == NAI__SUCCESS) {
				tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
				tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

				nStatus = nai_receive_serdes_oper_msg(&tNAIOperMsg);

				if (tNAIOperMsg.tSerdesHdr.ucPayloadLength >= 2) {
					if (tNAIOperMsg.tSerdesHdr.ucByteEnable == 0x3)
						*pusValue = tNAIOperMsg.tSerdesPayLd.usData[0];
					else
						*pusValue = tNAIOperMsg.tSerdesPayLd.usData[1];
				}
				else
					nStatus = NAI_SERDES_UNEXPECTED_PAYLOAD_COUNT;
			}
		}
	}

	if (unlockSERDES_Mutex(ucSlotID) != NAI__SUCCESS) {
		if (nStatus == NAI__SUCCESS)
			nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
	}

	return nStatus;
}

s32 nai_write_reg16_by_slot_request(u8 ucSlotID, u32 unModuleOffset,
				    u16 usValue) {
	s32 nStatus = NAI__SUCCESS;
			
	FIFOValue tFIFOVal;
	u8 ucCompleterID = ucSlotID;
	u8 ucRequesterID = nai_get_global_slot_id();
	NAIOperMsg tNAIOperMsg;

	if (lockSERDES_Mutex(ucSlotID) != NAI__SUCCESS)
		return NAI_UNABLE_TO_LOCK_MUTEX;

	memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

	/* NOTE: We initialized the entire tNAIOperMsg to zero above..so here we just flesh out those items that need to be set for this write command */
	tNAIOperMsg.tSerdesHdr.ucType = NAI_SERDES_WRITEREG;
	tNAIOperMsg.tSerdesHdr.ucToHPS = 0;

	if ((unModuleOffset & 0x0003) == 0)
		tNAIOperMsg.tSerdesHdr.ucByteEnable = 0x3;
	else if ((unModuleOffset & 0x0003) == 2)
		tNAIOperMsg.tSerdesHdr.ucByteEnable = 0xC;
	else
		nStatus = NAI_MIS_ALIGNED_BYTE_ENABLE;

	if (nStatus == NAI__SUCCESS) {
		tNAIOperMsg.tSerdesHdr.ucPayloadLength = 2;    /* 2 Words (32 bit value) (Keith expects 32 bit value for now)*/
		tNAIOperMsg.tSerdesHdr.ucBlockAddrIncrVal = 0; /* Make sure we have a zero "stride" for a sing register write */

		tFIFOVal.unValue = unModuleOffset;
		tNAIOperMsg.tSerdesHdr.usAddressLo = tFIFOVal.usLoWord;
		tNAIOperMsg.tSerdesHdr.usAddressHi = tFIFOVal.usHiWord;

		tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
		tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

		/* Assign Payload */
		/* Depending on ByteEnable...we need to either write lower order or upper order bits */
		if (tNAIOperMsg.tSerdesHdr.ucByteEnable == 0x3)	{
			tNAIOperMsg.tSerdesPayLd.usData[0] = usValue;
			tNAIOperMsg.tSerdesPayLd.usData[1] = 0;
		}
		else {
			tNAIOperMsg.tSerdesPayLd.usData[0] = 0;
			tNAIOperMsg.tSerdesPayLd.usData[1] = usValue;
		}

		nStatus = nai_send_serdes_oper_msg(&tNAIOperMsg);
	}

	if (unlockSERDES_Mutex(ucSlotID) != NAI__SUCCESS) {
		if (nStatus == NAI__SUCCESS)
			nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
	}

	return nStatus;
}

s32 nai_read_reg32_by_slot_request(u8 ucSlotID, u32 unModuleOffset,
				   u32 *punValue) {
	s32 nStatus = NAI__SUCCESS;
	FIFOValue tFIFOVal;
	u8 ucCompleterID = ucSlotID;
	u8 ucRequesterID = nai_get_global_slot_id();
	u32 ulTimer;
	NAIOperMsg tNAIOperMsg;

	if (lockSERDES_Mutex(ucSlotID) != NAI__SUCCESS)
		return NAI_UNABLE_TO_LOCK_MUTEX;

	memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

	tNAIOperMsg.tSerdesHdr.ucType = NAI_SERDES_READREG;
	tNAIOperMsg.tSerdesHdr.ucToHPS = 0;
	tNAIOperMsg.tSerdesHdr.ucByteEnable = NAI_SERDES_32BITDATA;
	tNAIOperMsg.tSerdesHdr.ucPayloadLength = 2; /* No Payload for making the request...but we must tell Keith how much data we are expecting! */
	tNAIOperMsg.tSerdesHdr.ucBlockAddrIncrVal = 0; /* Make sure we have a zero "stride" for a single register read */

	tFIFOVal.unValue = unModuleOffset;
	tNAIOperMsg.tSerdesHdr.usAddressLo = tFIFOVal.usLoWord;
	tNAIOperMsg.tSerdesHdr.usAddressHi = tFIFOVal.usHiWord;

	tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
	tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

	/* Send request for data */
	nStatus = nai_send_serdes_oper_msg(&tNAIOperMsg);

	if (nStatus == NAI__SUCCESS) {
		/* ** NOW WAIT FOR RESPONSE ** */

		/* Prepare for response! */		
		memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

		/* Wait for packet! */
		ulTimer = jiffies;
		while (!nai_rx_fifo_pkt_ready(ucRequesterID, ucCompleterID)) {
			/* If FIFO did not get serviced within a reasonable amount of time..get out */
			if (((s32)jiffies - (s32)ulTimer) > COMPLETION_TIMEOUT) {
				nStatus = NAI_RX_FIFO_PKT_NOT_READY_TIMEOUT;
				break;
			}
			schedule();
		}

		if (nStatus == NAI__SUCCESS) {
			tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
			tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

			/* Now receive the packet */
			nStatus = nai_receive_serdes_oper_msg(&tNAIOperMsg);

			if (tNAIOperMsg.tSerdesHdr.ucPayloadLength >= 2) /* One 32 bit value = 2 Words (4 Bytes) */ {
				tFIFOVal.usLoWord = tNAIOperMsg.tSerdesPayLd.usData[0];
				tFIFOVal.usHiWord = tNAIOperMsg.tSerdesPayLd.usData[1];
				*punValue = (u32)tFIFOVal.unValue;
			}
			else
				nStatus = NAI_SERDES_UNEXPECTED_PAYLOAD_COUNT;
		}
	}

	if (unlockSERDES_Mutex(ucSlotID) != NAI__SUCCESS) {
		if (nStatus == NAI__SUCCESS)
			nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
	}

	return nStatus;
}

s32 nai_write_reg32_by_slot_request(u8 ucSlotID, u32 unModuleOffset,
				    u32 unValue) {
	s32 nStatus = NAI__SUCCESS;
	FIFOValue tFIFOVal;
	u8 ucCompleterID = ucSlotID;
	u8 ucRequesterID = nai_get_global_slot_id();
	NAIOperMsg tNAIOperMsg;

	if (lockSERDES_Mutex(ucSlotID) != NAI__SUCCESS)
		return NAI_UNABLE_TO_LOCK_MUTEX;

	memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

	tNAIOperMsg.tSerdesHdr.ucType = NAI_SERDES_WRITEREG;
	tNAIOperMsg.tSerdesHdr.ucToHPS = 0;
	tNAIOperMsg.tSerdesHdr.ucByteEnable = NAI_SERDES_32BITDATA;
	tNAIOperMsg.tSerdesHdr.ucPayloadLength = 2; /* 2 Words (32 bit value) */
	tNAIOperMsg.tSerdesHdr.ucBlockAddrIncrVal = 0; /* Make sure we have a zero "stride" for a sing register write */

	tFIFOVal.unValue = unModuleOffset;
	tNAIOperMsg.tSerdesHdr.usAddressLo = tFIFOVal.usLoWord;
	tNAIOperMsg.tSerdesHdr.usAddressHi = tFIFOVal.usHiWord;

	tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
	tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

	tFIFOVal.unValue = unValue;
	tNAIOperMsg.tSerdesPayLd.usData[0] = tFIFOVal.usLoWord;
	tNAIOperMsg.tSerdesPayLd.usData[1] = tFIFOVal.usHiWord;

	nStatus = nai_send_serdes_oper_msg(&tNAIOperMsg);

	if (unlockSERDES_Mutex(ucSlotID) != NAI__SUCCESS) {
		if (nStatus == NAI__SUCCESS)
			nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
	}

	return nStatus;
}

s32 nai_read_block16_by_slot_request(u8 ucSlotID, u32 unModuleOffsetStart,
				     u32 unCount, u8 ucStride,
				     u16 *pusDataBuf) {
	s32 nStatus = NAI__SUCCESS;
	s32 nPayloadLeftToRead = (s32)(unCount);
	u16 i;
	u8 ucCompleterID = ucSlotID;
	u8 ucRequesterID = nai_get_global_slot_id();
	u32 ulTimer = 0;
	u16 usChunkIndex = 0;
	
	/* SERDES HEADER */
	FIFOValue tFIFOVal;
	uint16_t usDataIndex = 0;

	NAIOperMsg tNAIOperMsg;
	memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));
		
	if (lockSERDES_Mutex(ucSlotID) != NAI__SUCCESS)
		return NAI_UNABLE_TO_LOCK_MUTEX;

	/* Must be 32 bit aligned */
	if ((unModuleOffsetStart & 0x0003) != 0)
		return NAI_MIS_ALIGNED_BYTE_ENABLE;
								
	/* Make sure Stide is a multiple of 4 for 32 bit alignment */
    if ((ucStride % 4) != 0)
		return NAI_STRIDE_CAUSES_MISALIGNMENT;
		
	while (nPayloadLeftToRead > 0 && nStatus == NAI__SUCCESS)
	{
		memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));
		
		/* NOTE: We expect data to be "packed" */
		tNAIOperMsg.tSerdesHdr.ucDataMode = 1;
		tNAIOperMsg.tSerdesHdr.ucByteEnable = NAI_SERDES_32BITDATA;
						
		tNAIOperMsg.tSerdesHdr.ucType = NAI_SERDES_READREG;
		tNAIOperMsg.tSerdesHdr.ucToHPS = 0;
		tNAIOperMsg.tSerdesHdr.ucPayloadLength = (uint8_t)(MIN(nPayloadLeftToRead, NAI_OPER_MAX_PAYLOAD_IN_WORDS)); /* No Payload for making the request!...but we must tell Keith how much data we are expecting! */
		tNAIOperMsg.tSerdesHdr.ucBlockAddrIncrVal = (ucStride/4); 

		tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
		tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;		

		if (ucStride == 0)
			tFIFOVal.unValue = unModuleOffsetStart;
		else
			tFIFOVal.unValue = (unModuleOffsetStart + (usChunkIndex * NAI_OPER_MAX_PAYLOAD_IN_WORDS * ucStride));

		tNAIOperMsg.tSerdesHdr.usAddressLo = tFIFOVal.usLoWord;
		tNAIOperMsg.tSerdesHdr.usAddressHi = tFIFOVal.usHiWord;	

		/* Send request for data */
		nStatus = nai_send_serdes_oper_msg(&tNAIOperMsg);

		if (nStatus == NAI__SUCCESS)
		{
			/* Prepare for response! */
			memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

			/* Wait for packet! */
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

			if (nStatus != NAI__SUCCESS)
				break;
				
			tNAIOperMsg.tSerdesHdr.ucPayloadLength = (uint8_t)(MIN(nPayloadLeftToRead, NAI_OPER_MAX_PAYLOAD_IN_WORDS)); /* We must tell Keith how much data we are expecting! */
			tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
			tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

			/* Now receive the packet */
			nStatus = nai_receive_serdes_oper_msg(&tNAIOperMsg);
		
			if (nStatus == NAI__SUCCESS)
			{
				for (i=0; i < tNAIOperMsg.tSerdesHdr.ucPayloadLength; i++)
					pusDataBuf[usDataIndex++] = tNAIOperMsg.tSerdesPayLd.usData[i];
					
				nPayloadLeftToRead -= tNAIOperMsg.tSerdesHdr.ucPayloadLength;
			}
			
			usChunkIndex++;
		}
	}
		
	if (unlockSERDES_Mutex(ucSlotID) != NAI__SUCCESS) {
		if (nStatus == NAI__SUCCESS)
			nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
	}

	return nStatus;
}

s32 nai_write_block16_by_slot_request(u8 ucSlotID, u32 unModuleOffsetStart,
				      u32 unCount, u8 ucStride,
				      u16 *pusDataBuf) {
	s32 nStatus = NAI__SUCCESS;
	u16 i;
	FIFOValue tFIFOVal;
	s32 nPayloadWordsLeftToWrite = (s32)(unCount * 2);
	u16 usDataIndex = 0;
	u8 ucCompleterID = ucSlotID;
	u8 ucRequesterID = nai_get_global_slot_id();
	u16 usChunkIndex = 0;
		
	if (lockSERDES_Mutex(ucSlotID) != NAI__SUCCESS)
		return NAI_UNABLE_TO_LOCK_MUTEX;

	/* Must be 32 bit aligned */
	if ((unModuleOffsetStart & 0x0003) != 0)
		return NAI_MIS_ALIGNED_BYTE_ENABLE;
		
    /* Make sure Stide is a multiple of 4 - 32 bit addressing */
    if ((ucStride % 4) != 0)
		return NAI_STRIDE_CAUSES_MISALIGNMENT;
		
	while (nStatus == NAI__SUCCESS && nPayloadWordsLeftToWrite > 0)
	{
		NAIOperMsg tNAIOperMsg;
		memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

		tNAIOperMsg.tSerdesHdr.ucType = NAI_SERDES_WRITEREG;
		tNAIOperMsg.tSerdesHdr.ucToHPS = 0;
		tNAIOperMsg.tSerdesHdr.ucPayloadLength = (uint8_t)MIN(nPayloadWordsLeftToWrite, NAI_OPER_MAX_PAYLOAD_IN_WORDS);
		tNAIOperMsg.tSerdesHdr.ucBlockAddrIncrVal = (ucStride/4); 

		if (ucStride == 0)
			tFIFOVal.unValue = unModuleOffsetStart;
		else
			tFIFOVal.unValue = (unModuleOffsetStart + (usChunkIndex * NAI_OPER_MAX_PAYLOAD_IN_WORDS * ucStride));
					
		tNAIOperMsg.tSerdesHdr.usAddressLo = tFIFOVal.usLoWord;
		tNAIOperMsg.tSerdesHdr.usAddressHi = tFIFOVal.usHiWord;	
	
		tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
		tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;
		
		/* Use Packing! */
		tNAIOperMsg.tSerdesHdr.ucDataMode = 1;
		tNAIOperMsg.tSerdesHdr.ucByteEnable = NAI_SERDES_32BITDATA;

		/* Assign Payload */
		for (i=0; i < tNAIOperMsg.tSerdesHdr.ucPayloadLength; i++)
			tNAIOperMsg.tSerdesPayLd.usData[i] = pusDataBuf[usDataIndex++];

		nStatus = nai_send_serdes_oper_msg(&tNAIOperMsg);

		nPayloadWordsLeftToWrite -= tNAIOperMsg.tSerdesHdr.ucPayloadLength;
		usChunkIndex++;
	}
	
	if (unlockSERDES_Mutex(ucSlotID) != NAI__SUCCESS) {
		if (nStatus == NAI__SUCCESS)
			nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
	}

	return nStatus;
}

s32 nai_read_block32_by_slot_request(u8 ucSlotID, u32 unModuleOffsetStart,
				     u32 unCount, u8 ucStride,
				     u32 *punDataBuf) {
	s32 nStatus = NAI__SUCCESS;
	s32 nPayloadLeftToRead = (s32)(unCount * 2);  /*Number of Words (not 32 bit values)!*/
	u16 i;
	u8 ucCompleterID = ucSlotID;
	u8 ucRequesterID = nai_get_global_slot_id();
	u32 ulTimer;
	u16 usChunkIndex = 0;
	u16 usDataIndex = 0;	
	FIFOValue tFIFOVal;
	NAIOperMsg tNAIOperMsg;
	
	/* Must be 32 bit aligned */
	if ((unModuleOffsetStart & 0x0003) != 0)
		return NAI_MIS_ALIGNED_BYTE_ENABLE;
			
    /* SERDES uses 32 bit addressing so make sure Stride is a multiple of 4 */
    if ((ucStride % 4) != 0)
		return NAI_STRIDE_CAUSES_MISALIGNMENT;	
				
	/* SERDES HEADER */	
	while (nPayloadLeftToRead > 0 && nStatus == NAI__SUCCESS) {
		memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));
		tNAIOperMsg.tSerdesHdr.ucType = NAI_SERDES_READREG;
		tNAIOperMsg.tSerdesHdr.ucToHPS = 0;
		tNAIOperMsg.tSerdesHdr.ucByteEnable = NAI_SERDES_32BITDATA;
		tNAIOperMsg.tSerdesHdr.ucBlockAddrIncrVal = (ucStride/4); 

		tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
		tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;			
		tNAIOperMsg.tSerdesHdr.ucPayloadLength = (u8)(MIN(nPayloadLeftToRead, NAI_OPER_MAX_PAYLOAD_IN_WORDS)); /* No Payload for making the request!...but we must tell Keith how much data we are expecting! */

		if (ucStride == 0)
			tFIFOVal.unValue = unModuleOffsetStart;
		else
			tFIFOVal.unValue = (unModuleOffsetStart + (usChunkIndex * (NAI_OPER_MAX_PAYLOAD_IN_WORDS/2) * ucStride));
			
		tNAIOperMsg.tSerdesHdr.usAddressLo = tFIFOVal.usLoWord;
		tNAIOperMsg.tSerdesHdr.usAddressHi = tFIFOVal.usHiWord;	

		/* Send request for data */
		nStatus = nai_send_serdes_oper_msg(&tNAIOperMsg);		
			
		if (nStatus == NAI__SUCCESS) {			
			/* Prepare for response! */
			memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

			/* Wait for packet! */
			ulTimer = jiffies;
			while (!nai_rx_fifo_pkt_ready(ucRequesterID, ucCompleterID)) {
				/* If FIFO did not get serviced within a reasonable amount of time..get out */
				if (((s32)jiffies - (s32)ulTimer) > COMPLETION_TIMEOUT) {
					nStatus = NAI_RX_FIFO_PKT_NOT_READY_TIMEOUT;				
					break;
				}
			}

			if (nStatus != NAI__SUCCESS)
				break;

			tNAIOperMsg.tSerdesHdr.ucPayloadLength = (u8)(MIN(nPayloadLeftToRead, NAI_OPER_MAX_PAYLOAD_IN_WORDS)); /* We must tell Keith how much data we are expecting! */
			tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
			tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

			/* Now receive the packet */
			nStatus = nai_receive_serdes_oper_msg(&tNAIOperMsg);
			
			if (nStatus == NAI__SUCCESS) {
				for (i=0; i < tNAIOperMsg.tSerdesHdr.ucPayloadLength; i++) {
					tFIFOVal.usLoWord = tNAIOperMsg.tSerdesPayLd.usData[i++];
					tFIFOVal.usHiWord = tNAIOperMsg.tSerdesPayLd.usData[i];
					punDataBuf[usDataIndex++] = (u32)tFIFOVal.unValue;
				}

				nPayloadLeftToRead -= tNAIOperMsg.tSerdesHdr.ucPayloadLength;
			}
			
			usChunkIndex++;
		}
	}		

	if (unlockSERDES_Mutex(ucSlotID) != NAI__SUCCESS) {
		if (nStatus == NAI__SUCCESS)
			nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
	}

	return nStatus;
}

s32 nai_write_block32_by_slot_request(u8 ucSlotID, u32 unModuleOffsetStart,
				      u32 unCount, u8 ucStride,
				      u32 *punDataBuf) {
	s32 nStatus = NAI__SUCCESS;
	u16 i;
	FIFOValue tFIFOVal;
	s32 nPayloadWordsLeftToWrite = (s32)(unCount * 2); /*Number of Words (not 32 bit values)!*/
	u16 usDataIndex = 0;
	u8 ucCompleterID = ucSlotID;
	u8 ucRequesterID = nai_get_global_slot_id();
	u16 usChunkIndex = 0;
	
	if (lockSERDES_Mutex(ucSlotID) != NAI__SUCCESS)
		return NAI_UNABLE_TO_LOCK_MUTEX;

	/* Must be 32 bit aligned */
	if ((unModuleOffsetStart & 0x0003) != 0)
		return NAI_MIS_ALIGNED_BYTE_ENABLE;
		
    /* SERDES uses 32 bit addressing so make sure Stide is a multiple of 4 */
    if ((ucStride % 4) != 0)
		return NAI_STRIDE_CAUSES_MISALIGNMENT;

	while (nStatus == NAI__SUCCESS && nPayloadWordsLeftToWrite > 0) {
		NAIOperMsg tNAIOperMsg;
		memset(&tNAIOperMsg.msg, 0, sizeof(tNAIOperMsg.msg));

		/* NOTE: We initialized the entire tNAIOperMsg to zero above..so here we just flesh out those items that need to be set for this write command */
		tNAIOperMsg.tSerdesHdr.ucType = NAI_SERDES_WRITEREG;
        tNAIOperMsg.tSerdesHdr.ucToHPS = 0;
		tNAIOperMsg.tSerdesHdr.ucByteEnable = NAI_SERDES_32BITDATA;
		tNAIOperMsg.tSerdesHdr.ucPayloadLength = (u8)MIN(nPayloadWordsLeftToWrite, NAI_OPER_MAX_PAYLOAD_IN_WORDS);
		tNAIOperMsg.tSerdesHdr.ucBlockAddrIncrVal = (ucStride/4); 

		if (ucStride == 0)
			tFIFOVal.unValue = unModuleOffsetStart;
		else
			tFIFOVal.unValue = (unModuleOffsetStart + (usChunkIndex * (NAI_OPER_MAX_PAYLOAD_IN_WORDS/2) * ucStride));
			
		tNAIOperMsg.tSerdesHdr.usAddressLo = tFIFOVal.usLoWord;
		tNAIOperMsg.tSerdesHdr.usAddressHi = tFIFOVal.usHiWord;	
	
		tNAIOperMsg.tSerdesHdr.ucRequesterID = ucRequesterID;
		tNAIOperMsg.tSerdesHdr.ucCompleterID = ucCompleterID;

		/* Assign Payload */
		for (i=0; i < tNAIOperMsg.tSerdesHdr.ucPayloadLength; i++) {
			tFIFOVal.unValue = (u32)punDataBuf[usDataIndex++];
			tNAIOperMsg.tSerdesPayLd.usData[i++] = tFIFOVal.usLoWord;
			tNAIOperMsg.tSerdesPayLd.usData[i] = tFIFOVal.usHiWord;
		}

		nStatus = nai_send_serdes_oper_msg(&tNAIOperMsg);

		nPayloadWordsLeftToWrite -= tNAIOperMsg.tSerdesHdr.ucPayloadLength;
		usChunkIndex++;
	}

	if (unlockSERDES_Mutex(ucSlotID) != NAI__SUCCESS) {
		if (nStatus == NAI__SUCCESS)
			nStatus = NAI_UNABLE_TO_UNLOCK_MUTEX;
	}

	return nStatus;
}

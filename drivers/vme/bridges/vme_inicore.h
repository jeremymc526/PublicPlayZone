/*
 * vme_inicore.h
 *
 * Support for the Inicore VME SystemController 
 *
 * Author: 

 * Copyright
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef VME_INCORE_H
#define VME_INCORE_H

/*
 *  Define the number of each that the Tsi148 supports.
 */
#define INICORE_MAX_MASTER		22	/* Max Master Windows */
#define INICORE_MAX_SLAVE		1	/* Max Slave Windows */


/* Structure used to hold driver specific information */
struct inicore_driver {
	void __iomem	*port;
	void __iomem	*core_cfg_base;
	void __iomem 	*master_win_base;
	wait_queue_head_t iack_queue;
	struct vme_master_resource *flush_image;
	struct resource* win_resource;
	int				irq;
	u8				berr_time;		/* Bus error timeout*/
	struct mutex vme_rmw;		/* Only one RMW cycle at a time */
	struct mutex vme_int;		
					/*
					 * Only one VME interrupt can be
					 * generated at a time, provide locking
					 */
};

/*
 *
 *      Note:   INICORE Register Group (CRG) consists of the following
 *              combination of registers:
 *                      LCSR    - Local Control and Status Registers
 *                      GCSR    - Global Control and Status Registers
 *                      CR/CSR  - Subset of Configuration ROM /
 *                                Control and Status Registers
 */


/* FPGA VME Port Base address 0x80000000 */
/* FPGA VME BASE ADDRESS 0x43C8 0000 */
#define VME_BUS_BASE_ADDR							0x80000000
#define VNME_BUS_SIZE                     			0x20000000 /*512 MB*/
#define VME_CORE_REG_OFFSET							0x4
#define	VME_MS_WIN1_ST_ADDR_OFFSET 					0x0
#define VME_MS_WIN2_ST_ADDR_OFFSET					0x4
#define VME_MS_WIN3_ST_ADDR_OFFSET					0x8
#define VME_MS_WIN4_ST_ADDR_OFFSET					0xC
#define VME_MS_WIN5_ST_ADDR_OFFSET					0x10
#define VME_MS_WIN6_ST_ADDR_OFFSET					0x14
#define VME_MS_WIN7_ST_ADDR_OFFSET					0x18
#define VME_MS_WIN8_ST_ADDR_OFFSET					0x1C
#define VME_MS_WIN9_ST_ADDR_OFFSET					0x20
#define VME_MS_WIN10_ST_ADDR_OFFSET					0x24
#define VME_MS_WIN11_ST_ADDR_OFFSET					0x28
#define VME_MS_WIN12_ST_ADDR_OFFSET					0x2C
#define VME_MS_WIN13_ST_ADDR_OFFSET					0x30
#define VME_MS_WIN14_ST_ADDR_OFFSET					0x34
#define VME_MS_WIN15_ST_ADDR_OFFSET					0x38
#define VME_MS_WIN16_ST_ADDR_OFFSET					0x3C
#define VME_MS_WIN17_ST_ADDR_OFFSET					0x40
#define VME_MS_WIN18_ST_ADDR_OFFSET					0x44
#define VME_MS_WIN19_ST_ADDR_OFFSET					0x48
#define VME_MS_WIN20_ST_ADDR_OFFSET					0x4C
#define VME_MS_WIN21_ST_ADDR_OFFSET					0x50

#define VME_MS_WIN1_ED_ADDR_OFFSET					0x54
#define VME_MS_WIN2_ED_ADDR_OFFSET					0x58
#define	VME_MS_WIN3_ED_ADDR_OFFSET 					0x5C
#define	VME_MS_WIN4_ED_ADDR_OFFSET 					0x60
#define	VME_MS_WIN5_ED_ADDR_OFFSET 					0x64
#define	VME_MS_WIN6_ED_ADDR_OFFSET 					0x68
#define	VME_MS_WIN7_ED_ADDR_OFFSET 					0x6C
#define	VME_MS_WIN8_ED_ADDR_OFFSET 					0x70
#define	VME_MS_WIN9_ED_ADDR_OFFSET 					0x74
#define	VME_MS_WIN10_ED_ADDR_OFFSET 				0x78
#define	VME_MS_WIN11_ED_ADDR_OFFSET 				0x7C
#define	VME_MS_WIN12_ED_ADDR_OFFSET 				0x80
#define	VME_MS_WIN13_ED_ADDR_OFFSET 				0x84
#define	VME_MS_WIN14_ED_ADDR_OFFSET 				0x88
#define	VME_MS_WIN15_ED_ADDR_OFFSET 				0x8C
#define	VME_MS_WIN16_ED_ADDR_OFFSET 				0x90
#define	VME_MS_WIN17_ED_ADDR_OFFSET 				0x94
#define	VME_MS_WIN18_ED_ADDR_OFFSET 				0x98
#define	VME_MS_WIN19_ED_ADDR_OFFSET 				0x9C
#define	VME_MS_WIN20_ED_ADDR_OFFSET 				0xA0
#define	VME_MS_WIN21_ED_ADDR_OFFSET 				0xA4

#define	VME_MS_WINn_ST_AM_M 						(0x3F << 0) /*Address Modifer Shift*/
#define	VME_MS_WINn_ST_AM_S(x) 						(x << 0) /*Address Modifer Shift*/
#define	VME_MS_WINn_ST_DW_M 						(0x3 << 6) /*Data Width MSK*/
#define	VME_MS_WINn_ST_DW_S(x) 						(x << 6) /*Data Width Shift*/
#define	VME_MS_WINn_XX_ADDR_M 						(0xFFFFFF << 8) /*VME Address Mask*/
#define	VME_MS_WINn_XX_ADDR_S(x) 					(x << 8) /*VME Address Shift*/


#define VME_MIN_IOMEM 								0x4000
#define VME_MAX_IOMEM								0x20000

static const int VME_MS_WIN_ST[21] = { VME_MS_WIN1_ST_ADDR_OFFSET,
				VME_MS_WIN2_ST_ADDR_OFFSET, VME_MS_WIN3_ST_ADDR_OFFSET,
				VME_MS_WIN4_ST_ADDR_OFFSET, VME_MS_WIN5_ST_ADDR_OFFSET,
				VME_MS_WIN6_ST_ADDR_OFFSET, VME_MS_WIN7_ST_ADDR_OFFSET,
				VME_MS_WIN8_ST_ADDR_OFFSET, VME_MS_WIN9_ST_ADDR_OFFSET,
				VME_MS_WIN10_ST_ADDR_OFFSET, VME_MS_WIN11_ST_ADDR_OFFSET,
				VME_MS_WIN12_ST_ADDR_OFFSET, VME_MS_WIN13_ST_ADDR_OFFSET,
				VME_MS_WIN14_ST_ADDR_OFFSET, VME_MS_WIN15_ST_ADDR_OFFSET,
				VME_MS_WIN16_ST_ADDR_OFFSET, VME_MS_WIN17_ST_ADDR_OFFSET,
				VME_MS_WIN18_ST_ADDR_OFFSET, VME_MS_WIN19_ST_ADDR_OFFSET,
				VME_MS_WIN20_ST_ADDR_OFFSET, VME_MS_WIN21_ST_ADDR_OFFSET };

static const int VME_MS_WIN_ED[21] = { VME_MS_WIN1_ED_ADDR_OFFSET,
				VME_MS_WIN2_ED_ADDR_OFFSET, VME_MS_WIN3_ED_ADDR_OFFSET,
				VME_MS_WIN4_ED_ADDR_OFFSET, VME_MS_WIN5_ED_ADDR_OFFSET,
				VME_MS_WIN6_ED_ADDR_OFFSET, VME_MS_WIN7_ED_ADDR_OFFSET,
				VME_MS_WIN8_ED_ADDR_OFFSET, VME_MS_WIN9_ED_ADDR_OFFSET,
				VME_MS_WIN10_ED_ADDR_OFFSET, VME_MS_WIN11_ED_ADDR_OFFSET,
				VME_MS_WIN12_ED_ADDR_OFFSET, VME_MS_WIN13_ED_ADDR_OFFSET,
				VME_MS_WIN14_ED_ADDR_OFFSET, VME_MS_WIN15_ED_ADDR_OFFSET,
				VME_MS_WIN16_ED_ADDR_OFFSET, VME_MS_WIN17_ED_ADDR_OFFSET,
				VME_MS_WIN18_ED_ADDR_OFFSET, VME_MS_WIN19_ED_ADDR_OFFSET,
				VME_MS_WIN20_ED_ADDR_OFFSET, VME_MS_WIN21_ED_ADDR_OFFSET  };			

/*FPGA VME CORE CFG MODE */
#define CORE_CFG_BUSLOCK_MODE 						0x10
#define CORE_CFG_LOCAL_CSR_MODE 					0x20
#define CORE_CFG_EXT_CSR_MODE						0x40
#define CORE_CFG_VME_BUS_MODE						0x50
#define CORE_CFG_CLEAR_MODE							0x60
#define CORE_CFG_UPPER_3BIT_VMEADDR_MODE  			0x80
#define CORE_CFG_MODE_MASK  						0xF0
#define CORE_CFG_DSIZE_8BIT_MODE 					0x01
#define CORE_CFG_DSIZE_16BIT_MODE	 				0x02
#define CORE_CFG_DSIZE_32BIT_MODE	 				0x03
#define CORE_CFG_CORE_VAL_MASK  					0x0F
#define CORE_CFG_CLEAR								0x0
#define CORE_CFG_EN_VME_BUS							0x0
#define CORE_CFG_EN_BUSLOCK          				(1 << 8)
#define CORE_CFG_EN_LOCAL_CSR          				(1 << 9)
#define CORE_CFG_EN_EXT_CSR          				(1 << 10)
#define CORE_CFG_SET_DSIZE_8BIT         			(1 << 20)
#define CORE_CFG_SET_DSIZE_16BIT         			(2 << 20)
#define CORE_CFG_SET_DSIZE_32BIT         			(3 << 20)
#define CORE_CFG_SET_DSIZE(x)	         			(x << 20)
#define CORE_CFG_SET_UPPER_3BIT_VMEADDR_MASK        (3 << 29)
#define CORE_CFG_SET_UPPER_3BIT_VMEADDR(x)      	(x << 29)


#define INICORE_AM_A32					0x00		/* A32 Address Space */
#define INICORE_AM_A24					0x30		/* A24 Address Space */
#define INICORE_AM_A16					0x20		/* A16 Address Space */
#define INICORE_AM_CRCSR				0x2F		/* CRCSR Address Space */
#define INICORE_AM_MBLT					0x00		/* MBLT data cycle */
#define INICORE_AM_DAT					0x01		/* DATA Access */
#define INICORE_AM_PGM					0x02		/* Program Access */
#define INICORE_AM_BLT					0x03		/* BLT data cycle */
#define INICORE_AM_NPRIV				0x08		/* Non-Priv (User) Access */
#define INICORE_AM_SURP					0x0C		/* Supervisor Access */

#define INICORE_DW32					0x03		/* 32 Data Width */
#define INICORE_DW16					0x02		/* 16 Data Width */
#define INICORE_DW08					0x01		/* 8 Data Width */

/*
 * LCSR definitions
 */

#define INICORE_LCSR_DEV_CTRL			0x0F0
#define INICORE_LCSR_DEV_VER			0x0FC
#define INICORE_LCSR_SYS_CTRL			0x0D0
#define INICORE_LCSR_VME_MSTR			0x0B0

#define INICORE_LCSR_SLV_ACC_DEC1		0x0A8
#define INICORE_LCSR_SLV_ACC_CMP1		0x0A4
#define INICORE_LCSR_SLV_ACC_MSK1		0x0A0
#define INICORE_LCSR_SLV_ACC_DEC2		0x098
#define INICORE_LCSR_SLV_ACC_CMP2		0x094
#define INICORE_LCSR_SLV_ACC_MSK2		0x090
#define INICORE_LCSR_SLV_ACC_DEC3		0x088
#define INICORE_LCSR_SLV_ACC_CMP3		0x084
#define INICORE_LCSR_SLV_ACC_MSK3		0x080
#define INICORE_LCSR_SLV_ACC_DEC4		0x078
#define INICORE_LCSR_SLV_ACC_CMP4		0x074
#define INICORE_LCSR_SLV_ACC_MSK4		0x070

#define INICORE_LCSR_DMA_STAT			0x06C 
#define INICORE_LCSR_DMA_CMD			0x068
#define INICORE_LCSR_DMA_LADDR			0x064
#define INICORE_LCSR_DMA_VADDR			0x060

#define INICORE_LCSR_MAILBOX1			0x05C
#define INICORE_LCSR_MAILBOX2			0x058
#define INICORE_LCSR_MAILBOX3			0x054
#define INICORE_LCSR_MAILBOX4			0x050
#define INICORE_LCSR_SEMAPHORE			0x040

#define INICORE_LCSR_VINT_STAT_SW		0x03C
#define INICORE_LCSR_VINT_MAP			0x038
#define INICORE_LCSR_VINT_STAT			0x034
#define INICORE_LCSR_VINT				0x030
#define INICORE_LCSR_VME_IRQ1_STAT		0x02C
#define INICORE_LCSR_VME_IRQ2_STAT		0x028
#define INICORE_LCSR_VME_IRQ3_STAT		0x024
#define INICORE_LCSR_VME_IRQ4_STAT		0x020
#define INICORE_LCSR_VME_IRQ5_STAT		0x01C
#define INICORE_LCSR_VME_IRQ6_STAT		0x018
#define INICORE_LCSR_VME_IRQ7_STAT		0x014

static const int INICORE_LCSR_VME_IRQn_STAT[8] = { 0, INICORE_LCSR_VME_IRQ1_STAT,
				INICORE_LCSR_VME_IRQ2_STAT, INICORE_LCSR_VME_IRQ3_STAT,
				INICORE_LCSR_VME_IRQ4_STAT, INICORE_LCSR_VME_IRQ5_STAT,
				INICORE_LCSR_VME_IRQ6_STAT, INICORE_LCSR_VME_IRQ7_STAT };

#define INICORE_LCSR_VME_IRQn_STAT_D08_VINTHn_ERR   0x100
#define INICORE_LCSR_VME_IRQn_STAT_D16_VINTHn_ERR   0x10000

#define INICORE_LCSR_VINT_IRQH_CMD		0x010
#define INICORE_LCSR_VINT_STATUS		0x00C
#define INICORE_LCSR_VINT_EBL			0x008
#define INICORE_LCSR_INT_STATUS			0x004
#define INICORE_LCSR_INT_EBL			0x000



/*
 * CR/CSR
 */

/*
 *        CR/CSR   LCR/LCSR
 * offset  7FFF4   FF4 - CSRBCR
 * offset  7FFF8   FF8 - CSRBSR
 * offset  7FFFC   FFC - CBAR
 */
#define INICORE_CSRBAR				0x7FC /* CR/CSR BAR */
#define INICORE_CSRBSR				0x7F8 /* Bit Set Regiseter */
#define INICORE_CSRBCR				0x7F4 /* Bit Clear Register */
#define INICORE_CSRCRAM				0x7F0 /* Config RAM Onwer Register */
#define INICORE_CSRUBSR				0x7EC /* User Bit-set register */
#define INICORE_CSRUBCR				0x7E8 /* User Bit-Clear register */
#define INICORE_CSRADER3			0x790 /* Function 3 ADER */
#define INICORE_CSRADER2			0x780 /* Function 2 ADER */
#define INICORE_CSRADER1			0x770 /* Function 1 ADER */
#define INICORE_CSRADER0			0x760 /* Function 0 ADER */
#define INICORE_CSRADERn_0_OFFSET 	0x000 /*[31:24]offset of CSRADERn register*/			
#define INICORE_CSRADERn_4_OFFSET 	0x004 /*[23:16]offset of CSRADERn register*/				
#define INICORE_CSRADERn_8_OFFSET 	0x008 /*[15:8]offset of CSRADERn register*/			
#define INICORE_CSRADERn_C_OFFSET 	0x00C /*[7:0]offset of CSRADERn register*/			
	
/*
 *  Inicore CSR Register Bit Definitions
 * 
 */

/*
 *  CSR ADER Window[1~4]
 * 	CRG +  0x760
 * 	CRG +  0x770
 * 	CRG +  0x780
 * 	CRG +  0x790
 */
#define INICORE_CSRADERn_ADDRCB_M				(0xFF<<24)	/* Address bus compare bit*/
#define INICORE_CSRADERn_AM_M					(0x3F<<2)	/* Address Modifer Mask */
#define INICORE_CSRADERn_AM_S(x)				(x<<2)		/* Address Modifer Shift */

#define INICORE_CSRADERn_AM_AS_M				(7<<5)	/* Address Space Mask */
#define INICORE_CSRADERn_AM_A24					(7<<5)	/* A24 Address Space */
#define INICORE_CSRADERn_AM_A32					(1<<5)	/* A32 Address Space */
#define INICORE_CSRADERn_AM_AC_M				(7<<2)	/* Access Mask */
#define INICORE_CSRADERn_AM_SUPR				(4<<2)	/* Supervisor Access */
#define INICORE_CSRADERn_AM_BLT					(3<<2)	/* BLT Cycle */
#define INICORE_CSRADERn_AM_PGM					(2<<2)	/* Program Access */
#define INICORE_CSRADERn_AM_DAT					(1<<2)	/* Data Access */
#define INICORE_CSRADERn_AM_NPRIV				(0<<2)	/* Non-Priv (User) Access */

/*
 *  Inicore Register Bit Definitions
 */

/*
 *  Device Control DEV_CTRL + 0F0
 */ 
#define INICORE_LCSR_DEV_CTRL_LENDIAN	(0x1<<0)	/* little endian local bus*/

/*
 *  System Contoller SYS_CTRL + 0D0
 */ 

#define INICORE_LCSR_SYSC_BERRTIMER(x)	(x << 16) /* Bus Error Timeout*/
#define INICORE_LCSR_SYSC_ACFALEN		(1<<12)	/* Sys reset upon ACFAIL */
#define INICORE_LCSR_SYSC_LRESET		(1<<11)	/* Local Reset */
#define INICORE_LCSR_SYSC_SRESET		(1<<10)	/* VME System reset */
#define INICORE_LCSR_SYSC_BUSARB		(1<<9)	/* Bus Arbiter*/
#define INICORE_LCSR_SYSC_SCONEN		(1<<8)	/* Enable System Cont*/
#define INICORE_LCSR_SYSC_SCONS			(1<<6)	/* System Cont Status */
#define INICORE_LCSR_SYSC_GAP			(1<<5)	/* Geographic Addr Parity */
#define INICORE_LCSR_SYSC_GA_M			(0x1F<<0)  /* Geographic Addr Mask */


/*
 *  Slave Access Address Decoder Mask Window[1~4]
 * 	CRG +  0x0A0
 * 	CRG +  0x0A4
 * 	CRG +  0x0A8
 * 	CRG +  0x0AC
 */
#define INICORE_LCSR_SLVW_ADEM_M		(0xFFFFFF << 8) /*Address Decoder Mask Mask*/
#define INICORE_LCSR_SLVW_ADEM_S(x)		(x << 8) 		/*Address Decoder Mask Shift*/

/*
 *  Slave Access Decoding Window[1~4] 
 * 	CRG +  0x0A8
 * 	CRG +  0x098
 * 	CRG +  0x088
 * 	CRG +  0x078
 */
#define INICORE_LCSR_SLVW_EBL			(1 << 31) 		/*Slave Window Enable*/
#define INICORE_LCSR_SLVW_OFFSET_M		(0xFFFF << 8) 	/*Address Offset*/

/*
 *  VME Interrupt Map CRG + 038
 */
#define INICORE_LCSR_VINT_MAP_D32			(1<<9) /* VMEbus SW IRQ D32 */
#define INICORE_LCSR_VINT_MAP_D16			(1<<8) /* VMEbus SW IRQ D16 */
#define INICORE_LCSR_VINT_MAP_D8			(0<<8) /* VMEbus SW IRQ D08 */
#define INICORE_LCSR_VINT_MAP_USRIRQL_M    	(7<<4)	/* VMEbus USR IRQ Level Mask */
#define INICORE_LCSR_VINT_MAP_USRIRQL_1		(1<<4)	/* VMEbus USR IRQ Level 1 */
#define INICORE_LCSR_VINT_MAP_USRIRQL_2		(2<<4)	/* VMEbus USR IRQ Level 2 */
#define INICORE_LCSR_VINT_MAP_USRIRQL_3		(3<<4)	/* VMEbus USR IRQ Level 3 */
#define INICORE_LCSR_VINT_MAP_USRIRQL_4		(4<<4)	/* VMEbus USR IRQ Level 4 */
#define INICORE_LCSR_VINT_MAP_USRIRQL_5		(5<<4)	/* VMEbus USR IRQ Level 5 */
#define INICORE_LCSR_VINT_MAP_USRIRQL_6		(6<<4)	/* VMEbus USR IRQ Level 6 */
#define INICORE_LCSR_VINT_MAP_USRIRQL_7		(7<<4)	/* VMEbus USR IRQ Level 7 */
#define INICORE_LCSR_VINT_MAP_SWIRQL_M    	(7<<0)	/* VMEbus SW IRQ Level Mask */
#define INICORE_LCSR_VINT_MAP_SWIRQL_1		(1<<0)	/* VMEbus SW IRQ Level 1 */
#define INICORE_LCSR_VINT_MAP_SWIRQL_2		(2<<0)	/* VMEbus SW IRQ Level 2 */
#define INICORE_LCSR_VINT_MAP_SWIRQL_3		(3<<0)	/* VMEbus SW IRQ Level 3 */
#define INICORE_LCSR_VINT_MAP_SWIRQL_4		(4<<0)	/* VMEbus SW IRQ Level 4 */
#define INICORE_LCSR_VINT_MAP_SWIRQL_5		(5<<0)	/* VMEbus SW IRQ Level 5 */
#define INICORE_LCSR_VINT_MAP_SWIRQL_6		(6<<0)	/* VMEbus SW IRQ Level 6 */
#define INICORE_LCSR_VINT_MAP_SWIRQL_7		(7<<0)	/* VMEbus SW IRQ Level 7 */

static const int INICORE_LCSR_VINT_MAP_SWIRQL[8] = { 0, INICORE_LCSR_VINT_MAP_SWIRQL_1,
			INICORE_LCSR_VINT_MAP_SWIRQL_2, INICORE_LCSR_VINT_MAP_SWIRQL_3,
			INICORE_LCSR_VINT_MAP_SWIRQL_4, INICORE_LCSR_VINT_MAP_SWIRQL_5,
			INICORE_LCSR_VINT_MAP_SWIRQL_6, INICORE_LCSR_VINT_MAP_SWIRQL_7 };

static const int INICORE_LCSR_VINT_MAP_USRIRQL[8] = { 0, INICORE_LCSR_VINT_MAP_USRIRQL_1,
			INICORE_LCSR_VINT_MAP_USRIRQL_2, INICORE_LCSR_VINT_MAP_USRIRQL_3,
			INICORE_LCSR_VINT_MAP_USRIRQL_4, INICORE_LCSR_VINT_MAP_USRIRQL_5,
			INICORE_LCSR_VINT_MAP_USRIRQL_6, INICORE_LCSR_VINT_MAP_USRIRQL_7 };
/*
 *  VME Interrupt Status/ID CRG + 0014
 */

/*
 *  VME Interrupt Handler Command CRG + 0010
 */
#define INICORE_LCSR_VINT_IRQH_INTx_TYP_D32			(1<<9) /* interrupt status/id D32 */
#define INICORE_LCSR_VINT_IRQH_INTx_TYP_D16			(1<<8) /* interrupt status/id D16 */
#define INICORE_LCSR_VINT_IRQH_INTx_TYP_D8			(0<<8) /* interrupt status/id D8 */
#define INICORE_LCSR_VINT_IRQH_INT1_ERR	(1<<6)
#define INICORE_LCSR_VINT_IRQH_INT2_ERR	(1<<5)
#define INICORE_LCSR_VINT_IRQH_INT3_ERR	(1<<4)
#define INICORE_LCSR_VINT_IRQH_INT4_ERR	(1<<3)
#define INICORE_LCSR_VINT_IRQH_INT5_ERR	(1<<2)
#define INICORE_LCSR_VINT_IRQH_INT6_ERR	(1<<1)
#define INICORE_LCSR_VINT_IRQH_INT7_ERR	(1<<0)
/*
 *  VME Interrupt Status CRG + 00C
 */

#define INICORE_LCSR_VINT_STATUS_UIRQ	(1<<1)
#define INICORE_LCSR_VINT_STATUS_SWIRQ	(1<<0)

/*
 *  Interrupt Status INT_STATUS CRG + 004
 */
#define INICORE_LCSR_IS_MBOX3			(1<<17)	/* Mail Box 3 */
#define INICORE_LCSR_IS_MBOX2       	(1<<16)	/* Mail Box 2 */
#define INICORE_LCSR_IS_MBOX1       	(1<<15)	/* Mail Box 1 */
#define INICORE_LCSR_IS_MBOX0       	(1<<14)	/* Mail Box 0 */
#define INICORE_LCSR_IS_VTIERR        	(1<<13)	/* VME Arbiter temer error */
#define INICORE_LCSR_IS_VBERR			(1<<12)	/* VMEbus Error */
#define INICORE_LCSR_IS_DMAERR			(1<<11)	/* DMA error */
#define INICORE_LCSR_IS_DMADONE			(1<<10)	/* DMA Done */
#define INICORE_LCSR_IS_SWIACK     		(1<<9)	/* SW Interrupt Ack (IACK) */
#define INICORE_LCSR_IS_IRQ1       		(1<<8)	/* IRQ1 */
#define INICORE_LCSR_IS_IRQ2       		(1<<7)	/* IRQ2 */
#define INICORE_LCSR_IS_IRQ3       		(1<<6)	/* IRQ3 */
#define INICORE_LCSR_IS_IRQ4       		(1<<5)	/* IRQ4 */
#define INICORE_LCSR_IS_IRQ5       		(1<<4)	/* IRQ5 */
#define INICORE_LCSR_IS_IRQ6       		(1<<3)	/* IRQ6 */
#define INICORE_LCSR_IS_IRQ7       		(1<<2)	/* IRQ7 */
#define INICORE_LCSR_IS_SYSFAL     		(1<<1)	/* System Fail */
#define INICORE_LCSR_IS_ACFAL		 	(1<<0)	/* AC Fail */

static const int INICORE_LCSR_IS_IRQn[8] = { 0, INICORE_LCSR_IS_IRQ1,
					INICORE_LCSR_IS_IRQ2,
					INICORE_LCSR_IS_IRQ3,
					INICORE_LCSR_IS_IRQ4,
					INICORE_LCSR_IS_IRQ5,
					INICORE_LCSR_IS_IRQ6,
					INICORE_LCSR_IS_IRQ7 };

/*
 *  Interrupt Enable INT_ENBL CRG + 000
 */
#define INICORE_LCSR_IE_MBOX3		(1<<17)	/* Mail Box 3 */
#define INICORE_LCSR_IE_MBOX2       (1<<16)	/* Mail Box 2 */
#define INICORE_LCSR_IE_MBOX1       (1<<15)	/* Mail Box 1 */
#define INICORE_LCSR_IE_MBOX0       (1<<14)	/* Mail Box 0 */
#define INICORE_LCSR_IE_VTIERR		(1<<13)	/* VME Arbiter temer error */
#define INICORE_LCSR_IE_VBERR		(1<<12)	/* VMEbus Error */
#define INICORE_LCSR_IE_DMAERR		(1<<11)	/* DMA error */
#define INICORE_LCSR_IE_DMADONE		(1<<10)	/* DMA Done */
#define INICORE_LCSR_IE_SWIACK     	(1<<9)	/* SW Interrupt Ack (IACK) */
#define INICORE_LCSR_IE_IRQ1       	(1<<8)	/* IRQ1 */
#define INICORE_LCSR_IE_IRQ2       	(1<<7)	/* IRQ2 */
#define INICORE_LCSR_IE_IRQ3       	(1<<6)	/* IRQ3 */
#define INICORE_LCSR_IE_IRQ4       	(1<<5)	/* IRQ4 */
#define INICORE_LCSR_IE_IRQ5       	(1<<4)	/* IRQ5 */
#define INICORE_LCSR_IE_IRQ6       	(1<<3)	/* IRQ6 */
#define INICORE_LCSR_IE_IRQ7       	(1<<2)	/* IRQ7 */
#define INICORE_LCSR_IE_SYSFAL     	(1<<1)	/* System Fail */
#define INICORE_LCSR_IE_ACFAL		 	(1<<0)	/* AC Fail */

static const int INICORE_LCSR_IE_IRQn[8] = { 0, INICORE_LCSR_IE_IRQ1,
					INICORE_LCSR_IE_IRQ2,
					INICORE_LCSR_IE_IRQ3,
					INICORE_LCSR_IE_IRQ4,
					INICORE_LCSR_IE_IRQ5,
					INICORE_LCSR_IE_IRQ6,
					INICORE_LCSR_IE_IRQ7 };



#endif				/* TSI148_H */

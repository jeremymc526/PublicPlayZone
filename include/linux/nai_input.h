#ifndef _nai_input_H
#define _nai_input_H

#define NAI_EVENT_CODE_0 0
#define NAI_EVENT_CODE_1 1	
#define NAI_EVENT_CODE_2 2
#define NAI_EVENT_CODE_3 3
/* IRQ Register Offset
 * IRQ Base Address 0xFF21_0070 
*/
#define HPS_IRQ_0_VECTOR 	0x0
#define HPS_IRQ_1_VECTOR 	0x04 	
#define HPS_IRQ_2_VECTOR 	0x08
#define HPS_IRQ_3_VECTOR 	0x10
#define HPS_CLEAR_IRQ		0x0C
struct device;

struct nai_input_event {
	/* Configuration parameters */
	unsigned int code;	/* input event code */
	void __iomem *vector_regs; /* IRQ Vector Register */
	void __iomem *clear_irq_reg; /* IRQ Clear Register */
	const char *desc;
	unsigned int type;	/* input event type (EV_MSC) */
	bool can_disable;
	u32 value;		/*  vector value for EV_MSC */
	unsigned int irq;	/* Irq number in case of interrupt keys */
};

struct nai_input_platform_data {
	struct nai_input_event *inputevents;
	int ninputevents;
	int (*enable)(struct device *dev);
	void (*disable)(struct device *dev);
	const char *name;		/* input device name */
};

#endif

#ifndef __DEVICE_INFO_H__
#define __DEVICE_INFO_H__

#include "device_type.h"

_bsp_tree_ifo bsp_device_d[] =   
{
	/* current 0 */
  	{
	    .bsp_device_name     		=   "spi_max6675",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=   MP_BSP_SPI_T , 
				.is_active 	 		=   true , 
				.is_simulate 		=   true , 
				.bsp_device_ptr		=   0 ,
                .sconfig            =   0 ,
			},
			{
			  	/* spi clock */
				.portb1	            = 	{GPIOC,GPIO_PIN_13},   
			},
			{
			  	/* spi sda */
				.portb2	            = 	{GPIOC,GPIO_PIN_14},   
			},
			{
			  	/* spi cs1 */
				.portb3	            = 	{GPIOC,GPIO_PIN_15},   
			},
			{
			  	/* spi cs2*/
				.portb4	            = 	{GPIOC,GPIO_PIN_0},   
			},
		},
	},
  /* current 0 */
  	{
	    .bsp_device_name     		=   "current_0",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=   MP_BSP_ADC_T , 
				.is_active 	 		=   true , 
				.is_simulate 		=   false , 
				.bsp_device_ptr		=   ADC_CHANNEL_5 ,
                .sconfig            =   0 ,
			},
			{
			  	/* A pin */
				.portb1	            = 	{GPIOA,GPIO_PIN_5},   
			},
		},
	},
	/* current 1 */
  	{
	    .bsp_device_name     		=   "current_1",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=   MP_BSP_ADC_T , 
				.is_active 	 		=   true , 
				.is_simulate 		=   false , 
				.bsp_device_ptr		=   ADC_CHANNEL_6 ,
                .sconfig            =   0 ,
			},
			{
			  	/* A pin */
				.portb1	            = 	{GPIOA,GPIO_PIN_6},   
			},
		},
	},
	/* voltage */
  	{
	    .bsp_device_name     		=   "voltage_48v",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=   MP_BSP_ADC_T , 
				.is_active 	 		=   true , 
				.is_simulate 		=   false , 
				.bsp_device_ptr		=   ADC_CHANNEL_4 ,
                .sconfig            =   0 ,
			},
			{
			  	/* A pin */
				.portb1	            = 	{GPIOA,GPIO_PIN_4},   
			},
		},
	},
	/* temp */
	{
	    .bsp_device_name     		=   "ntc_temp",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=   MP_BSP_ADC_T , 
				.is_active 	 		=   true , 
				.is_simulate 		=   false , 
				.bsp_device_ptr		=   ADC_CHANNEL_7 ,
                .sconfig            =   0 ,
			},
			{
			  	/* A pin */
				.portb1	            = 	{GPIOA,GPIO_PIN_7},   
			},
		},
	},
  	{
		.bsp_device_name     		=   "error_led",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=       MP_BSP_GPIO_T ,  
				.is_active 	 		=       true , 
				.is_simulate 		=       false , 
				.bsp_device_ptr		=		0 ,
                .sconfig            =       0 ,
			},
			{
				.portb1			= 	{GPIOA,GPIO_PIN_15},
			},
		},
		.bsp_para					=
		{
			{
				.port_para 			= 	{MP_PORT_OUTPUT_PP,MP_PORT_PULLUP},	
			} 
		}
	},
  	{
		.bsp_device_name     		=   "buzz",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=       MP_BSP_GPIO_T ,  
				.is_active 	 		=       true , 
				.is_simulate 		=       false , 
				.bsp_device_ptr		=		0 ,
                .sconfig            =       0 ,
			},
			{
				.portb1			= 	{GPIOB,GPIO_PIN_7},
			},
		},
		.bsp_para					=
		{
			{
				.port_para 			= 	{MP_PORT_OUTPUT_PP,MP_PORT_PULLUP},	
			} 
		}
	},
  	{
		.bsp_device_name     		=   "rj45_led",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=       MP_BSP_GPIO_T ,  
				.is_active 	 		=       true , 
				.is_simulate 		=       false , 
				.bsp_device_ptr		=		0 ,
                .sconfig            =       0 ,
			},
			{
				.portb1			= 	{GPIOC,GPIO_PIN_10},
			},
		},
		.bsp_para					=
		{
			{
				.port_para 			= 	{MP_PORT_OUTPUT_PP,MP_PORT_PULLUP},	
			} 
		}
	},
  	{
		.bsp_device_name     		=   "rs485_led",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=       MP_BSP_GPIO_T ,  
				.is_active 	 		=       true , 
				.is_simulate 		=       false , 
				.bsp_device_ptr		=		0 ,
                .sconfig            =       0 ,
			},
			{
				.portb1			= 	{GPIOC,GPIO_PIN_10},
			},
		},
		.bsp_para					=
		{
			{
				.port_para 			= 	{MP_PORT_OUTPUT_PP,MP_PORT_PULLUP},	
			} 
		}
	},
  	{
		.bsp_device_name     		=   "run_led",
		.basic_ifo         			=	
		{
			.baseifo 	 			=  	
			{
				.bsp_type 	 		=       MP_BSP_GPIO_T ,  
				.is_active 	 		=       true , 
				.is_simulate 		=       false , 
				.bsp_device_ptr		=		0 ,
                .sconfig            =       0 ,
			},
			{
				.portb1			= 	{GPIOA,GPIO_PIN_11},
			},
		},
		.bsp_para					=
		{
			{
				.port_para 			= 	{MP_PORT_OUTPUT_PP,MP_PORT_PULLUP},	
			} 
		}
	},
};




#endif

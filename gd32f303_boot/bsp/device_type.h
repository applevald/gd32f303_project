#ifndef _GDHARDWAREMAP_H_
#define _GDHARDWAREMAP_H_

#include <stdint.h>
#include <string.h>
#include "gd32f30x_gpio.h"

#define USING_BSP_TREE_NAME 
#define ARM_MCU

/* take care how the mcu support!!!! */
enum BSP_UART_PTR
{
	MP_BSP_UART0 = 0,
    MP_BSP_UART1,
	MP_BSP_UART2,
	MP_BSP_UART3,
	MP_BSP_UART4,
	MP_BSP_UART5,
	MP_BSP_UART6,
	MP_BSP_MAX_UART_N,
};
enum BSP_I2C_PTR
{
	MP_BSP_I2C1 = 0,
	MP_BSP_I2C2,
	MP_BSP_I2C3,
	MP_BSP_MAX_I2C_N,
};

enum BSP_SPI_MODE_MASK
{
    SPI_CPOL_POS    = 0,
    SPI_CPHA_POS    = 1, 
    SPI_CS_MODOE_POS= 2,   
};
enum BSP_SPI_CS_MODE
{
	MP_SPI_CS_SOFT = 0,
	MP_SPI_CS_HARD,
};

enum BSP_SPI_PTR
{
	MP_BSP_SPI1 = 0,
	MP_BSP_SPI2,
	MP_BSP_SPI3,
};
enum BSP_TIMER_PTR
{
	MP_BSP_TIMER0 = 0,
	MP_BSP_TIMER1 ,
	MP_BSP_TIMER2 ,
	MP_BSP_TIMER3 ,
	MP_BSP_TIMER4 ,
	MP_BSP_TIMER5 ,
	MP_BSP_TIMER6 ,
	MP_BSP_MAX_TIMER_NUM ,
};
enum BSP_PWM_CHN
{
	MP_BSP_PWMCH0 = 0,
	MP_BSP_PWMCH1,
	MP_BSP_PWMCH2,
	MP_BSP_PWMCH3,
};
enum BSP_TREE_TYPE
{
	MP_BSP_GPIO_T = 0,
	MP_BSP_UART_T ,
	MP_BSP_I2C_T ,
	MP_BSP_SPI_T ,
	MP_BSP_ADC_T ,
	MP_BSP_UART_RS485_T ,
	MP_BSP_CAN_T ,
	MP_BSP_PWM_T ,
	MP_BSP_CAPTURE_T ,
	MP_BSP_TIMER_T,
};
enum BSP_DMA_TYPE
{
	MP_NO_DMA = 0,
	MP_UART_RX_DMA ,
	MP_UART_TX_DMA ,
	MP_UART_TXRX_DMA,
	MP_SPI_DMA ,
	MP_ADC_DMA ,
	MP_I2C_DMA ,
};
enum PORT_MODE
{
	MP_PORT_OUTPUT_PP = 0,
	MP_PORT_OUTPUT_OD ,
	MP_PORT_INPUT_NORMAL ,
	MP_PORT_INPUT_FALLING_IT ,
	MP_PORT_INPUT_RASING_IT ,
	MP_PORT_INPUT_HIGH_IT ,
	MP_PORT_INPUT_LOW_IT ,
	MP_PORT_INPUT_FRING_IT ,
};
enum PORT_STATE
{
	MP_PORT_NOPULL = 0,
	MP_PORT_PULLDOWN  ,
	MP_PORT_PULLUP ,
	MP_PORT_FLOATING ,
	MP_PROT_AF_PP, 
};
enum UART_BAUD_RARE
{
	MP_BAUD_9600  = 0,
	MP_BAUD_19200  ,
	MP_BAUD_57600  ,
	MP_BAUD_115200 ,
	MP_BAUD_230400 ,
	MP_BAUD_MAX_N,
};
enum UART_WORDLENGTH
{
	UART_WORDLENGTH_8B = 0,
	UART_WORDLENGTH_9B ,
};
enum UART_STOPBIT
{
	UART_STOPBITS_1 = 0,
	UART_STOPBITS_2 ,
};
enum UART_PARITY_M
{
	UART_PARITY_NONE  = 0,
	UART_PARITY_EVEN ,
	UART_PARITY_ODD ,
};
enum _GPIO_V
{
	GPIO_PULL_DOWN = 0,
	GPIO_PULL_UP ,
};
typedef struct _PWM_PARA
{
	uint16_t pwm_period ;
	uint16_t  pwm_duty_cycle ;
	uint8_t pwm_chx_on;
	uint8_t pwm_polarity_low;	
}_pwm_para;
typedef struct _UART_PARA
{
	uint8_t baud_rate_ptr : 3;
	uint8_t word_length   : 1;
	uint8_t stop_bit	  : 1;
	uint8_t parity_m	  : 1;
}_uart_para;
typedef struct _PORT_PARA
{
	uint8_t portmode  	: 4;
	uint8_t portstate   : 2;
	uint8_t reserved    : 2;
}_port_para;
typedef struct _I2C_PARA
{
	uint8_t clock_speed : 8;
	uint8_t dev_addr  	: 8;
	uint8_t writemode : 4;
	uint8_t readmode  : 4;
	uint8_t is_mass_storage;
}_i2c_para;
typedef struct _TIMER_PARA
{
	uint16_t clock_frz  ;
	uint16_t triger_frz ;
}_timer_para;

typedef struct _PORT_BRANCH
{
#ifdef ARM_MCU
	uint32_t port  ;
	uint16_t pin   ;
	uint8_t  af_type;
#else
	uint8_t port  :4;
	uint8_t pin   :4;
#endif
}_port_b;

typedef struct _BASIC_IFO_
{
	struct
	{
		uint8_t bsp_type  		: 5;
		uint8_t is_active 		: 1;
		uint8_t is_simulate 	        : 1;
                uint8_t is_initialized 	        : 1;
		
		uint8_t bsp_device_ptr          : 4;
                uint8_t sconfig                 : 4;
//		union 
//		{
//			uint8_t dma_type	    : 4;
//			uint8_t pwm_chn         : 4;
//			uint8_t i2c_dev_addr    : 4;
//			uint8_t sconfig         : 4;
//			uint8_t is_initialized  : 4;
//			uint8_t reserved        : 4;
//		};		
	}baseifo;

	union{
		_port_b portb1;
		_port_b tx_port ;
	    _port_b i2c_clk_port ;
		_port_b adc_port;
		_port_b spi_clk_port ;
		_port_b pwm_port ;
	};
	union{
		_port_b portb2;
		_port_b rx_port ;
		_port_b i2c_da_port;
		_port_b spi_mosi_port ;
		_port_b pwm_ctrl_port ;
	};
	union{
		_port_b portb3;
		_port_b rs485_port ;
		_port_b spi_miso_port ;
                _port_b rs485_rw_port ;
	};
	union{
	  _port_b portb4;
	  _port_b spi_cs_port ;
	};
}_basic_ifo;
typedef struct _BSP_PARAMETER1
{
	union{
		_port_para port_para;
		_uart_para uart_para;
		_pwm_para  pwm_para;
		_i2c_para  i2c_para;
		_timer_para timer_para;
		uint8_t bsppara_val[8];
	};
}_bsp_para;
typedef struct _COMPATIBLE_ATTR
{
	const char *compatible_name;
	union
	{
		_port_b cs_port;
	};
}_compatible_attr;
typedef struct _BSP_TREE_
{
	uint8_t bsp_index;
#ifdef USING_BSP_TREE_NAME
	char *bsp_device_name;
#endif	
	_basic_ifo basic_ifo;
	_bsp_para bsp_para;
	void *extend_ifo;
}_bsp_tree_ifo;
enum _GPIO_PIN_PTR
{	
	MP_R24V_CTRL_PTR = 0 ,
	MP_L24V_CTRL_PTR ,
	MP_SERVO_L_CTRL_PTR,
	MP_SERVO_CTRL_PTR ,
	MP_MAGNET1_CTRL_PTR ,
	MP_MAGNET2_CTRL_PTR ,
	MP_RFAN_CTRL_PTR ,
	MP_LFAN_CTRL_PTR ,
	
	MP_FILAMENT_R_IN_PTR ,
	MP_FILAMENT_L_IN_PTR ,
	MP_CUT_R_IN_PTR ,
	MP_CUT_L_IN_PTR ,
	
	MP_LEXTRUDER_IN_PTR ,
	MP_REXTRUDER_IN_PTR ,
	MP_LEVEL_IN_PTR ,
	
	MP_SERVO_L_PWM_PTR,
	MP_SERVO_PWM_PTR ,
	
	MP_UART2_PTR ,
	MP_I2C_E2PROM_L_PTR ,
	MP_I2C_E2PROM_R_PTR ,
	MP_SPI_MAX6675_L_PTR ,
	MP_SPI_MAX6675_R_PTR ,
	MP_I2C_ET6037_PTR,
	MP_SYS_TIMER_PTR,
	MP_GPIO_POLLING_TIMER_PTR,
	
	MP_MAX_BSP_DEV,
};

enum _UART_CMD
{
	UART_START_REC_CMD = 0,
	UART_GET_ACHE_BUFFER_CMD,
	UART_REGISTER_REV_QUEUE_CMD,
	UART_CLOSE_DEV_CMD,
};
enum _UART_EVENT
{
	UART_DAT_REC_EVENT = 0,
	UART_ICR_ERR_EVENT,
	

	UART_DAT_REC_ONE_EVENT ,
	UART_SEND_IVALID_DAT_ERR_EVENT,
};
enum _UART_STATUS
{
	UART_SEND_IDLE = 0,
	UART_SEND_BUSY,
	UART_SEND_BUF_NOT_EMPTY,
	UART_SEND_BUF_EMPTY,
};



#endif

#ifndef _DEVICE_EVENT_H_
#define _DEVICE_EVENT_H_

#include "myport.h"
#include "stdint.h"

/* key related */
enum KEY_EVENT
{
    KEY_EVENT_NONE = 0,
    KEY_EVENT_TRIGGER,              // key trigger event
    KEY_EVENT_PRESSED,
    KEY_EVENT_SHORT_RELEASE,
    KEY_EVENT_LONG_PRESS,
    KEY_EVENT_LONG_RELEASE,
    KEY_EVENT_DOUBLE_CLICK,
};
enum KEY_PTR
{
    KEY_UP = 0,
    KEY_DOWN,
    KEY_OK,
    KEY_ESC,
    KEY_MAX_PTR,
};

typedef struct _KEY_EVENT_
{
    uint8_t key_ptr; // key pointer
    uint8_t key_event; // key event
}_key_event_t;
/*KEY END**/

/* oled related */
enum _LED_DEV_EVENT_CMD_
{
	LCD_DEV_INIT_CMD = 0,
	LCD_CLEAR_DISPLAY_CMD,
	LCD_RERESH_CMD,
	LCD_SLEEP_MODE_CMD,
	LCD_NORMAL_MODE_CMD,
    LCD_LIGHT_MODE_CMD,
    LCD_DARK_MODE_CMD,
};
/* oled end */

/*hc595 */
enum _HC595_CMD_
{
	HC595_HC4051_CHN_SET_CMD = 0,
    HC595_LED_G_SET_CMD,
    HC595_LED_R_SET_CMD,
    HC595_LED_Y_SET_CMD,
	HC595_HC374_DO_SET_CMD,	
};

/* hc589 */
enum _HC589_CMD_
{
    HC589_START_QUERY_CMD = 0,
    HC589_STOP_QUERY_CMD,
};

/* load control */
enum _LOAD_CONTROL_PTR_
{
    LOAD_FIRST_LEVEL_POWER_CONTROL = 0,
    LOAD_SECOND_LEVEL_POWER_CONTROL,
    LOAD_THIRD_LEVEL_POWER_CONTROL,
};

/* w25qxx related */
enum _W25QXX_CMD_
{
    W25QXX_ERASE_SECTOR_CMD = 0,
    W25QXX_ERASE_BLOCK_CMD,
    W25QXX_ERASE_CHIP_CMD,
};

/* ina226 */
enum _INA226_CMD_
{
    INA226_READ_BUS_VOLTAGE_CMD = 0,
    INA226_READ_SHUNT_VOLTAGE_CMD,
    INA226_READ_CURRENT_CMD,
    INA226_READ_POWER_CMD,
    INA226_READ_CALIBRATION_CMD,
    INA226_SET_CALIBRATION_CMD,
};

#endif
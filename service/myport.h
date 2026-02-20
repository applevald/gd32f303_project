#ifndef _MYPORT_H_
#define _MYPORT_H_
#include <math.h>							/* 数学相关 */
#include <stdlib.h>							/* 标准库头相关 */
#include <rtthread.h>
//#include <board.h>
#define SET_ARGV(SUBDATA4,SUBDATA3,SUBDATA2,SUBDATA1)               ((unsigned int)(SUBDATA4&0xff) <<24)|((unsigned int)(SUBDATA3&0xff) << 16)|((unsigned int)(SUBDATA2&0xff) << 8)|((unsigned int)SUBDATA1&0xff)
#define GET_SUBDATA_L(ARGV)            (unsigned int)ARGV&0xff
#define GET_SUBDATA_MR(ARGV)           ((unsigned int)ARGV >> 8)&0xff
#define GET_SUBDATA_ML(ARGV)           ((unsigned int)ARGV >> 16)&0xff
#define GET_SUBDATA_H(ARGV)            ((unsigned int)ARGV >> 24)&0xff

// 双字节拼凑成16bit，带符号
#define SET_ARGV16(SUBDATA2,SUBDATA1) ((unsigned int)(SUBDATA2&0xff) << 8)|((unsigned int)SUBDATA1&0xff)
#define GET_SUBDATA16_L(ARGV)          (unsigned int)ARGV&0xff
#define GET_SUBDATA16_H(ARGV)          ((unsigned int)ARGV >> 8)&0xff

uint8_t asc_to_hex(char *dat);
uint16_t asc_to_int(char *dat);

// 4字节转浮点数
#define SET_ARGV_FLOAT(SUBDATA4,SUBDATA3,SUBDATA2,SUBDATA1) ((float)(*(float *)&SET_ARGV(SUBDATA4,SUBDATA3,SUBDATA2,SUBDATA1)))

#define PI              	3.1415926 / 180
#define Points           	0.3516
#define	BSP_BIT(a)			((0 == (a)) ? 0 : 1)
#define	PA					3.9083E-3
#define	PB					-5.775E-7
#define	PC					-4.183E-12
#define ratios				pow(2, 21)

#ifndef AT_MAX
#define	AT_MAX				20
#endif

#ifndef MIN
#define MIN(a,b)		 	((a<b)?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b)		 	((a>b)?(a):(b))
#endif

#define Hex2Ascii(data)  	((data<10)?('0'+data):('A'+data-10))
#define Ascii2Hex(data)  	((data>='0'&&data<='9')?(data-'0'):((data>='A'&&data<='F')?(data-'A'+10):((data>='a'&& data<='f')?(data-'a'+10):0)))
#define BCD_TO_HEX(val)  	(((val)&15) + ((val)>>4)*10)
#define HEX_TO_BCD(val)  	((((val)/10)<<4) + (val)%10)
#ifndef isprint
#define in_range(c, lo, up)  ((uint8_t)c >= lo && (uint8_t)c <= up)
#define isprint(c)           in_range(c, 0x20, 0x7f)
#define isdigit(c)           in_range(c, '0', '9')
#define isxdigit(c)          (isdigit(c) || in_range(c, 'a', 'f') || in_range(c, 'A', 'F'))
#define islower(c)           in_range(c, 'a', 'z')
#define isspace(c)           (c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v')
#define xchar(i)             ((i) < 10 ? '0' + (i) : 'A' + (i) - 10)
#endif
/* 函数声明 ------------------------------------------------------------------*/
void HEX_to_ASC(volatile uint8_t *q, volatile rt_uint8_t *p, rt_uint16_t n);
void ASC_to_HEX(volatile rt_uint8_t *q, volatile rt_uint8_t *p, rt_uint16_t n);

rt_uint16_t LCHKSUM(rt_uint8_t *p, rt_uint16_t n);
rt_uint16_t LENGTH( rt_uint16_t num);

rt_uint8_t  CheckSum8(rt_uint8_t *p,  rt_uint16_t n);
rt_uint16_t CheckSum16(rt_uint8_t *p, rt_uint16_t n);
typedef enum
{
	NTC_GND = 0,
	REF_GND = 1,
}RefMode;
void DealBCDToHex(rt_uint8_t *s, rt_uint8_t *p, rt_uint16_t n);
void DealBCDToStr(char *p, rt_uint8_t bcd);
void DealHEXToStr(char *p, rt_uint8_t data);
rt_uint16_t Int_To_UInt(rt_uint16_t dat, rt_uint8_t point);
rt_uint16_t UInt_To_Int(rt_uint16_t dat, rt_uint8_t point);

rt_uint8_t  CRC8_SUM(rt_uint8_t *p, rt_uint16_t len);
rt_uint16_t CRC16_MODBUS(volatile rt_uint8_t *ptr, rt_uint16_t len);
rt_uint16_t CRC16_XMODEM(volatile rt_uint8_t *ptr, rt_uint16_t len);

rt_uint16_t filter_Umid(rt_uint16_t *p, rt_uint16_t n);
rt_uint16_t filter_Umin(rt_uint16_t *p, rt_uint16_t n);
rt_uint16_t filter_Umax(rt_uint16_t *p, rt_uint16_t n);
rt_uint16_t filter_Usum(rt_uint16_t *p, rt_uint16_t n);
float filter_Fmid(float *p, rt_uint16_t n);
float filter_Fmin(float *p, rt_uint16_t n);
float filter_Fsum(float *p, rt_uint16_t n);
	
void Float2Char(float FloatNum, rt_uint8_t *pArray);
void Short2Char(rt_uint16_t ShortNum, rt_uint8_t *pArray);
void Int2Char(rt_uint32_t longNum, rt_uint8_t *pArray);
float long_to_float(long *p);
long float_to_long(float dat);

rt_uint16_t bsp_filter_u8(rt_uint8_t *p, rt_uint16_t len);
rt_uint16_t bsp_filter_u16(rt_uint16_t *p, rt_uint16_t len);

rt_uint16_t Square_Root(rt_uint16_t *p, rt_uint8_t len, double dat);
float Get_NTC(float vin, float vcc, float B, float BRef, rt_uint16_t dat, float Ref, RefMode mode);
double GetPT1000(double R0);

float DealHexToFloat(rt_uint16_t a1, rt_uint16_t n);
double DealLongToFloat(rt_uint32_t a1, rt_uint16_t n);
float BspHexToFloat(rt_uint16_t a1, rt_uint16_t n);
double BspLongToFloat(rt_uint32_t a1, rt_uint16_t n);

rt_uint16_t DealFloatToHex(float a1, rt_uint16_t n);
rt_uint32_t DealFloatToLong(double a1, rt_uint16_t n);
rt_uint16_t BspFloatToHex(float a1, rt_uint16_t n);
rt_uint32_t BspFloatToLong(double a1, rt_uint16_t n);
float DealLow(rt_uint16_t a1);
rt_uint16_t DealPow(float a1);

void BSP_Write_Eeeprom(rt_uint16_t *ptr, rt_uint8_t len);
void BSP_dump_hex(const rt_uint8_t *ptr, rt_size_t buflen, rt_uint8_t len);

float Side_length(float A, float B, float a);
float lzr_cos(float A,  float a);
float lzr_cosh(float A, float a);
float lzr_sin(float A,  float a);
float lzr_sinh(float A, float a);
float lzr_tan(float A,  float a);

#endif /* _PORT_H_ */

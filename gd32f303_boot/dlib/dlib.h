#ifndef _DLIB_H_
#define _DLIB_H_

#include <stdint.h>

typedef void (*_set_byte_val)(uint8_t val);
typedef uint8_t (*_get_byte_val)(void);

typedef void (*_set_word_val)(uint16_t val);
typedef uint16_t (*_get_word_val)(void);

typedef void (*_set_dword_val)(uint32_t val);
typedef uint32_t (*_get_dword_val)(void);

typedef void (*_set_float_val)(float val);
typedef float (*_get_float_val)(void);

typedef void (*_set_byte_val_by_ptr)(uint16_t ptr,uint8_t val);
typedef uint8_t (*_get_byte_val_by_ptr)(uint16_t ptr);

typedef void (*_set_word_val_by_ptr)(uint16_t ptr,uint16_t val);
typedef uint16_t (*_get_word_val_by_ptr)(uint16_t ptr);

typedef void (*_set_dword_val_by_ptr)(uint16_t ptr,uint32_t val);
typedef uint32_t (*_get_dword_val_by_ptr)(uint16_t ptr);

typedef void (*_set_float_val_by_ptr)(uint16_t ptr,float val);
typedef float (*_get_float_val_by_ptr)(uint16_t ptr);

typedef void (*_set_str_val_by_ptr)(uint16_t ptr,const char *val);
typedef const char *(*_get_str_val_by_ptr)(uint16_t ptr);

typedef struct  _DLIB_T
{
	float temperature;
}_dlib_t;

typedef struct _DLIB_OP
{
    _set_float_val set_temperature;
    _get_float_val get_temperature;
	
}_dlib_op;

_dlib_op *get_dlib_op(void);

char *get_my_version();

#endif



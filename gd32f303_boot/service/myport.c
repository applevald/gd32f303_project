#include "myport.h"

uint8_t asc_to_hex(char *dat)
{
    uint8_t value = 0;
    if (dat == NULL)
        return 0; // Return 0 if the input is NULL
    if (rt_strlen(dat) == 1) {
        if(dat[0] >= '0'&&dat[0] <= '9')
        {
            value = value * 16 + (dat[0] - '0'); // Convert ASCII digit to integer
        }
        else if(dat[0] >= 'A' && dat[0] <= 'F')
        {
            value = value * 16 + (dat[0] - 'A' + 10); // Convert ASCII hex digit to integer
        }
        else if(dat[0] >= 'a' && dat[0] <= 'f')
        {
            value = value * 16 + (dat[0] - 'a' + 10); // Convert ASCII hex digit to integer
        }
        else
        {
            rt_kprintf("Invalid character in data: %c\n", dat[0]);
            return 0; // Invalid character, return 0
        }
        return value;
    }
    for(uint8_t i = 0; i < 2; i++)
    {
        if(dat[i] >= '0'&&dat[i] <= '9')
        {
            value = value * 16 + (dat[i] - '0'); // Convert ASCII digit to integer
        }
        else if(dat[i] >= 'A' && dat[i] <= 'F')
        {
            value = value * 16 + (dat[i] - 'A' + 10); // Convert ASCII hex digit to integer
        }
        else if(dat[i] >= 'a' && dat[i] <= 'f')
        {
            value = value * 16 + (dat[i] - 'a' + 10); // Convert ASCII hex digit to integer
        }
        else
        {
            rt_kprintf("Invalid character in data: %c\n", dat[i]);
            return 0; // Invalid character, return 0
        }
    }
    return value;
}

uint16_t asc_to_int(char *dat)
{
    uint16_t value = 0;
    uint8_t len = rt_strlen(dat);
    if (dat == NULL)
        return 0; // Return 0 if the input is NULL
    for(uint8_t i = 0; i < len; i++)
    {
        if(dat[i] >= '0'&&dat[i] <= '9')
        {
            value = value * 10 + (dat[i] - '0'); // Convert ASCII digit to integer
        }
        else
        {
            rt_kprintf("Invalid character in data: %c\n", dat[i]);
            return 0; // Invalid character, return 0
        }
    }
    return value;
}


#include "device_info.h"
#include "device_type.h"

_bsp_tree_ifo *of_find_info_by_name(char name[])
{
  	uint8_t i;
	uint16_t size = sizeof(bsp_device_d)/sizeof(_bsp_tree_ifo);
  	for(i = 0; i < size ; i++)
	{
		if(strcmp(name,bsp_device_d[i].bsp_device_name) == 0)
			return &bsp_device_d[i];
	}
	return NULL;
}
_compatible_attr *of_find_compatible_dev_by_name(_bsp_tree_ifo *bsp_ifo,const char name[])
{
	_compatible_attr *table = bsp_ifo->extend_ifo;
	uint8_t i = 0;
	for(i = 0; i < 5;i++)
	{
		if(table[i].compatible_name != NULL)
		{
			if(strcmp(table[i].compatible_name,name) == 0)
			{
				return &table[i];
			}
		}
		else
		{
			return NULL;
		}
	}
	return NULL;
}

#include "includes.h"
uint16_t lock_counrt = 0;

static int init_start(void)
{
        return 0;
}
INIT_EXPORT_S(init_start, "0");

static int bsp_start(void)
{
	return 0;
}
INIT_EXPORT_S(bsp_start, "0.end");

static int bsp_end(void)
{
	return 0;
}
INIT_EXPORT_S(bsp_end, "1.end");

static int system_end(void)
{
	return 0;
}
INIT_EXPORT_S(system_end, "2.end");

static int task_end(void)
{
	return 0;
}
INIT_EXPORT_S(task_end, "3.end");

static int module_end(void)
{
	return 0;
}
INIT_EXPORT_S(module_end, "4.end");

static int init_end(void)
{
    return 0;
}
INIT_EXPORT_S(init_end,"7");

void bsp_init(void)
{
#ifdef SYS_AUTO_INIT_FUC
	const init_fn_t *fn_ptr;
	for (fn_ptr = &__mp_init_bsp_start; fn_ptr < &__mp_init_bsp_end; fn_ptr++)
    {
		if(fn_ptr != NULL)
        (*fn_ptr)();
    }
#else

#endif
	
}
void system_init(void)
{
#ifdef SYS_AUTO_INIT_FUC
	const init_fn_t *fn_ptr;
	for (fn_ptr = &__mp_init_bsp_end; fn_ptr < &__mp_init_system_end; fn_ptr++)
    {
		if(fn_ptr != NULL)
        (*fn_ptr)();
    }
#else

#endif
}
void task_init(void)
{
#ifdef SYS_AUTO_INIT_FUC
	const init_fn_t *fn_ptr;
	for (fn_ptr = &__mp_init_system_end; fn_ptr < &__mp_init_task_end; fn_ptr++)
    {
		if(fn_ptr != NULL)
        (*fn_ptr)();
    }
#else
	//init_app_task();
#endif	
}
void module_init(void)
{
#ifdef SYS_AUTO_INIT_FUC
	const init_fn_t *fn_ptr;
	for (fn_ptr = &__mp_init_task_end; fn_ptr < &__mp_init_module_end; fn_ptr++)
    {
		if(fn_ptr != NULL)
        (*fn_ptr)();
    }
#else
	//init_app_m_module();
#endif	
}


#ifndef __INIT_H__
#define __INIT_H__

#define SYS_AUTO_INIT_FUC

/* warning !! dependent on complier */
#define SECTION(x)           __attribute__((used,__section__(x)))


typedef int (*init_fn_t)(void);
#define INIT_EXPORT_S(fn, level)  \
		__root const init_fn_t __mp_init_##fn SECTION(".mp_fn."level) = fn


#define INIT_DRIVER_EXPORT(fn)                INIT_EXPORT_S(fn, "1")
#define INIT_SYSTEM_EXPORT(fn)               INIT_EXPORT_S(fn, "2")
#define INIT_TASK_EXPORT(fn)                 INIT_EXPORT_S(fn, "3")		
#define INIT_MODULE_EXPORT(fn)               INIT_EXPORT_S(fn, "4")		
		
void bsp_init(void);
void system_init(void);
void task_init(void);
void module_init(void);


#endif

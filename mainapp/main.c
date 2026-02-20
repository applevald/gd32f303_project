
#include <rtthread.h>
#include "board.h"
#include "includes.h"

/* Function declarations */
void rt_system_timer_init(void);
void rt_system_scheduler_init(void);
void rt_thread_idle_init(void);
void rt_system_timer_thread_init(void);

int main(void)
{
    /* Option 1: Use standard RT-Thread startup (recommended) */
    // rtthread_startup();
    
    /* Option 2: Manual initialization with proper thread setup */
    /* board initialization */
    rt_hw_board_init();

    bsp_init();
    system_init();
    task_init();
    module_init();
    
    /* show RT-Thread version */
    rt_show_version();
    
    /* initialize RT-Thread components */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_init();
#endif

    /* RT-Thread kernel initialization (missing in original code) */
    /* timer system initialization */
    rt_system_timer_init();
    
    /* scheduler system initialization */
    rt_system_scheduler_init();
    
    /* Initialize timer thread */
    rt_system_timer_thread_init();
    
    /* idle thread initialization - CRITICAL: creates the lowest priority thread */
    rt_thread_idle_init();
    
    /* start scheduler */
    rt_system_scheduler_start();
    /* should never reach here */
    while (1);
}

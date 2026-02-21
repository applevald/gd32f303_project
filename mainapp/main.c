
#include <rtthread.h>
#include "board.h"
#include "includes.h"

/* Function declarations */
int rtthread_startup(void);

int main(void)
{
    /* should never reach here */
    while (1)
    {
            rt_thread_mdelay(1000);
            rt_kprintf("Error: main function should not be called directly!\n");
    }
}

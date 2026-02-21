
#include <rtthread.h>
#include "board.h"
#include "includes.h"

#include <rtdbg.h>

/* Function declarations */
int rtthread_startup(void);

int main(void)
{
    while (1)
    {
        rt_thread_mdelay(1000);
        LOG_E("test....\n");
    }
}

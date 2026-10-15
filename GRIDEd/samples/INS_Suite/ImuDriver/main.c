/* main.c - ImuDriver */
#include "imu.h"

int main(void)
{
    ImuSample s;
    imu_init();
    for (;;)
    {
        imu_read(&s);
    }
}

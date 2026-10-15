/* imu.c - IMU driver for the C6747 (CCS 7.4 project, read-only in GRIDE) */
#include "imu.h"

void imu_init(void)
{
    /* configure McASP / SPI here */
}

int imu_read(ImuSample *s)
{
    s->gx = 0; s->gy = 0; s->gz = 0;
    s->ax = 0; s->ay = 0; s->az = 0;
    return 0;
}

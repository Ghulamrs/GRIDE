#ifndef IMU_H
#define IMU_H

typedef struct
{
    short gx, gy, gz;   /* gyro raw counts  */
    short ax, ay, az;   /* accel raw counts */
} ImuSample;

void imu_init(void);
int  imu_read(ImuSample *s);

#endif

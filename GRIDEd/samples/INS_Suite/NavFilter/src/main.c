/* main.c - NavFilter (C90) */
#include "kalman.h"

int main(void)
{
    KF_State s;
    int i;
    float z;

    kf_init(&s);
    for (i = 0; i < 100; i++)
    {
        z = 1.0f;                       /* measurement */
        kf_predict(&s, 0.0f, 0.01f);
        kf_update(&s, z);
    }
    return 0;
}

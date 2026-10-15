/* kalman.c - 1-axis INS error-state filter (C90) */
#include "kalman.h"

static float Q = 0.001f;
static float R = 0.03f;

void kf_init(KF_State *s)
{
    s->x = 0.0f;
    s->P = 1.0f;
}

void kf_predict(KF_State *s, float u, float dt)
{
    s->x = s->x + u * dt;
    s->P = s->P + Q;
}

void kf_update(KF_State *s, float z)
{
    float K = s->P / (s->P + R);
    s->x = s->x + K * (z - s->x);
    s->P = (1.0f - K) * s->P;
}

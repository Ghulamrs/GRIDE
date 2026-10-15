#ifndef KALMAN_H
#define KALMAN_H

/* one-axis error-state Kalman filter (C90) */
typedef struct
{
    float x;    /* state estimate   */
    float P;    /* error covariance */
} KF_State;

void kf_init(KF_State *s);
void kf_predict(KF_State *s, float u, float dt);
void kf_update(KF_State *s, float z);

#endif /* KALMAN_H */

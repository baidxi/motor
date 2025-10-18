#pragma once

#include <stdint.h>
#include <stdbool.h>

struct motor_adc;

/* BEMF检测状态 */
enum bemf_state {
    BEMF_STATE_RISING,
    BEMF_STATE_FALLING,
    BEMF_STATE_UNKNOWN
};

/* 速度检测状态 */
struct speed {
    uint16_t dir;                    /* 电机方向 */
    uint32_t rpm;                    /* 转速 (RPM) */
    uint32_t electrical_freq;        /* 电频率 (Hz) */
    uint32_t pole_pairs;             /* 电机极对数 */
    uint32_t bemf_u;                 /* U相反电动势 */
    uint32_t bemf_v;                 /* V相反电动势 */
    uint32_t bemf_w;                 /* W相反电动势 */
    uint32_t zero_cross_timestamp;   /* 过零点时间戳 */
    uint32_t last_zero_cross_time;   /* 上次过零点时间 */
    uint32_t zero_cross_interval;    /* 过零点间隔 */
    enum bemf_state bemf_state_u;    /* U相BEMF状态 */
    enum bemf_state bemf_state_v;    /* V相BEMF状态 */
    enum bemf_state bemf_state_w;    /* W相BEMF状态 */
    uint16_t current_phase;           /* 当前相位 */
    bool speed_valid;                /* 速度值是否有效 */
    uint32_t filter_alpha;           /* 滤波系数 */
    uint32_t filtered_speed;         /* 滤波后的速度值 */
    uint32_t target_speed_raw;       /* 目标速度原始ADC值 */
    uint32_t target_rpm;             /* 目标转速(RPM) */
    double bus_vol;
    void *parent;                    /* 指向父结构体 */
};

struct speed *speed_init(struct motor_adc *adc, void *parent);

/* 获取当前速度(RPM) */
uint32_t speed_get_rpm(struct speed *speed);

/* 获取当前电频率(Hz) */
uint32_t speed_get_frequency(struct speed *speed);

/* 获取电机方向 */
uint16_t speed_get_direction(struct speed *speed);

/* 检查速度值是否有效 */
bool speed_is_valid(struct speed *speed);

/* 获取BEMF值 */
void speed_get_bemf(struct speed *speed, uint32_t *bemf_u, uint32_t *bemf_v, uint32_t *bemf_w);

/* 设置电机极对数 */
void speed_set_pole_pairs(struct speed *speed, uint32_t pole_pairs);

/* 设置滤波系数 */
void speed_set_filter_alpha(struct speed *speed, uint32_t alpha);

/* 重置速度检测 */
void speed_reset(struct speed *speed);

/* 速度控制反馈函数 */
int speed_control_feedback(struct speed *speed, uint32_t target_rpm);

/* 获取速度控制状态 */
void speed_get_control_status(struct speed *speed, uint32_t *current_rpm,
                             uint32_t *frequency, uint16_t *direction, bool *valid);

/* 获取电机当前相位 */
uint8_t speed_get_current_phase(struct speed *speed);

/* 获取过零点间隔时间 */
uint32_t speed_get_zero_cross_interval(struct speed *speed);

/* 设置BEMF阈值 */
void speed_set_bemf_threshold(struct speed *speed, uint32_t threshold);

/* 获取速度检测统计信息 */
void speed_get_statistics(struct speed *speed, uint32_t *raw_rpm, uint32_t *filtered_rpm,
                         uint32_t *bemf_u, uint32_t *bemf_v, uint32_t *bemf_w);
#pragma once

#include <zephyr/drivers/gpio.h>

#include <stdint.h>

struct svpwm;
struct device;

struct pwm_channel_info {
    uint8_t channel_id;
    struct gpio_dt_spec en;
};

struct pwm_info {
    const struct device *dev;
    const struct pwm_channel_info *channels;
    uint8_t nb_channels;
};

struct svpwm_ops {
    void (*enable)(struct svpwm *pwm, uint8_t ch);
    void (*disable)(struct svpwm *pwm, uint8_t ch);
    void (*update)(struct svpwm *pwm, uint8_t ch, float duty_cycle);
};

struct svpwm {
    const struct svpwm_ops *ops;
};

struct svpwm *svpwm_init(const struct pwm_info *info, uint16_t freq, uint16_t cycle, uint32_t system_clock_freq);

/* 更新SVPWM输出 */
void svpwm_update_output(struct svpwm *pwm, float alpha, float beta);

/* 获取当前扇区 */
uint8_t svpwm_get_sector(struct svpwm *pwm);

/* 获取当前占空比 */
void svpwm_get_duty_cycles(struct svpwm *pwm, float *duty_u, float *duty_v, float *duty_w);

/* 设置PWM频率 */
int svpwm_set_frequency(struct svpwm *pwm, uint16_t freq);

/* 获取PWM频率 */
uint16_t svpwm_get_frequency(struct svpwm *pwm);

/* 获取PWM周期计数值(cycle) */
uint16_t svpwm_get_cycle(struct svpwm *pwm);

/* 设置PWM周期计数值(cycle) */
int svpwm_set_cycle(struct svpwm *pwm, uint16_t cycle);
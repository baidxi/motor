#pragma once

#include <stdint.h>

struct motor_ctrl;
struct pwm_info;
struct adc_info;

struct motor_ctrl_ops {
    void (*start)(struct motor_ctrl *ctrl);
};

struct motor_ctrl {
    const struct motor_ctrl_ops *ops;
    uint32_t system_clock_freq;
};


struct motor_ctrl *motor_ctrl_init(const struct pwm_info *pwm_info, const struct adc_info *adc_info, uint32_t freq);

void ctrl_event_post(struct motor_ctrl *ctrl, uint32_t event);


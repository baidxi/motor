#pragma once

#include "menu.h"
#include "motor/adc.h"
#include "motor/controller.h"
#include <stdint.h>

struct speed;

struct motor;
struct svpwm;
struct motor_ctrl;
struct menu_t;

struct motor_ops {
    void (*update_state)(struct motor *motor, uint8_t state);
};
struct motor {
    const struct motor_ops *ops;
};

enum motor_type {
    MOTOR_TYPE_BLDC,
    MOTOR_TYPE_FOC,
};

enum motor_state {
    MOTOR_STATE_IDENTIFY,
    MOTOR_STATE_STOP,
    MOTOR_STATE_RUN,
    MOTOR_STATE_FAULT,
};

enum motor_event {
    MOTOR_EVENT_READY = 1 << 0,
};

struct motor *motor_init(struct speed *speed, uint8_t type, struct svpwm *svpwm);

/* 旋转电机到指定角度 */
int motor_rotate(struct motor *motor, float angle_deg);
void motor_setup_menu_bind(struct motor_ctrl *ctrl, struct menu_t *menu);
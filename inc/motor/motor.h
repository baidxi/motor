#pragma once

#include <stdint.h>

struct motor_t;
struct svpwm_info;
struct menu_item_t;

enum motor_type {
    MOTOR_TYPE_BLDC,
    MOTOR_TYPE_FOC,
};

struct motor_t *motor_init(uint8_t type, uint8_t id);
int motor_svpwm_init(struct motor_t *motor, const struct svpwm_info *info);
void motor_type_change_cb(struct menu_item_t *item, uint8_t type);
void motor_svpwm_freq_set_range(struct motor_t *motor, uint16_t min, uint16_t max);
void motor_svpwm_freq_set_cb(struct menu_item_t *item, int32_t min, int32_t max);
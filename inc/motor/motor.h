#pragma once

#include <stdint.h>

struct motor_t;
struct svpwm_info;
struct menu_item_t;
struct mc_t;
struct mc_adc_info;

enum motor_type {
    MOTOR_TYPE_BLDC,
    MOTOR_TYPE_FOC,
};

enum motor_event_t {
    MOTOR_EVENT_READY = 1,
    MOTOR_EVENT_IDLE,
};

struct motor_t *motor_init(struct mc_t *mc, struct mc_adc_info *, uint8_t type, uint8_t id);
int motor_svpwm_init(struct motor_t *motor, const struct svpwm_info *info);
void motor_type_change_cb(struct menu_item_t *item, uint8_t type);
void motor_svpwm_freq_set_range(struct motor_t *motor, uint16_t min, uint16_t max);
void motor_svpwm_freq_set_cb(struct menu_item_t *item, int32_t min, int32_t max);
void motor_ready(struct motor_t *motor);
void motor_idle(struct motor_t *motor);
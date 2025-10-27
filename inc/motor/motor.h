#pragma once

#include <stdint.h>

struct motor_t;
struct svpwm_info;

struct motor_t *motor_init(uint8_t type, uint8_t id);
int motor_svpwm_init(struct motor_t *motor, const struct svpwm_info *info);
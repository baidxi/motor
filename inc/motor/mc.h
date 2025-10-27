#pragma once

#include <stdint.h>

struct motor_t;
struct svpwm_info;
struct adc_info;
struct menu_t;
struct adc_callback_t;

enum motor_type {
    MOTOR_TYPE_BLDC,
    MOTOR_TYPE_FOC,
};

struct mc_t;

struct mc_t *mc_init(uint8_t type, int nb_motor);
int mc_svpwm_init(struct mc_t *mc, const struct svpwm_info *info, int motor_id);
int mc_adc_init(struct mc_t *mc, const struct adc_info *info);
void mc_setup_menu_bind(struct mc_t *mc, struct menu_t *menu);
int mc_adc_event_register(struct mc_t *mc, struct adc_callback_t *cb);
void mc_adc_start(struct mc_t *mc);
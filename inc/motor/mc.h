#pragma once

#include <stdint.h>
#include <stdbool.h>

struct motor_t;
struct svpwm_info;
struct adc_info;
struct menu_t;
struct adc_callback_t;
struct mc_t;

struct mc_t *mc_init(uint8_t type, int nb_motor);
int mc_svpwm_init(struct mc_t *mc, const struct svpwm_info *info, int motor_id);
int mc_adc_init(struct mc_t *mc, const struct adc_info *info);
void mc_setup_menu_bind(struct mc_t *mc, struct menu_t *menu);
int mc_adc_event_register(struct mc_t *mc, struct adc_callback_t *cb);
void mc_adc_start(struct mc_t *mc);
struct motor_t *mc_motor_get(struct mc_t *mc, uint8_t id);
int mc_motor_count(struct mc_t *mc);
bool mc_motor_ready(struct mc_t *mc, bool is_ready);
void mc_motor_voltage_range_set(struct mc_t *mc, int min, int max);
double mc_vbus_get(struct mc_t *mc);
void mc_menu_bind(struct menu_t *menu, struct mc_t *mc);


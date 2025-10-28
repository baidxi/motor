#pragma once

#include <stdint.h>
#include <zephyr/drivers/gpio.h>

struct svpwm_t;

struct svpwm_channel_info {
    uint8_t id;
    const struct gpio_dt_spec en;
};

struct svpwm_info {
    const struct device *dev;
    const struct svpwm_channel_info *channels;
    uint8_t nb_channels;
};

struct svpwm_t *svpwm_init(const struct svpwm_info *info);
void svpwm_freq_set(struct svpwm_t *pwm, uint16_t min, uint16_t max);
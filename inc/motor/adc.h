#pragma once

#include <zephyr/drivers/adc.h>

struct motor_adc;

typedef void (*adc_callback_func)(uint16_t *values, size_t count, uint8_t id, void *param);

struct adc_channel_info {
    const struct adc_channel_cfg cfg;
    uint8_t id;
};

struct adc_info {
    const struct adc_channel_info *channels;
    const struct device *dev;
    uint8_t nb_channels;
};

enum channel_id {
    BEMF_A,
    BEMF_B,
    BEMF_C,
    VOLAGE_BUS,
    SPEED_CTRL,
    CURR_A,
    CURR_C,
};

struct motor_adc *adc_init(const struct adc_info *info);
int adc_register_callback(struct motor_adc *adc, adc_callback_func func, uint8_t id, void *param);
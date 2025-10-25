#pragma once

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>

struct motor_adc;

typedef void (*adc_callback_func)(uint16_t *values, size_t count, uint8_t id, void *param);

struct adc_channel_info {
    const struct adc_channel_cfg cfg;
    uint8_t id;
};

struct adc_callback_t {
    void (*func)(struct adc_callback_t *self, uint16_t *values, size_t count, void *param);
    void *param;
    uint8_t id;
    struct k_work work;
    struct adc_callback_t *next;
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
    VOLTAGE_BUS,
    SPEED_CTRL,
    CURR_A,
    CURR_C,
};

struct motor_adc *adc_init(const struct adc_info *info);
int adc_register_callback(struct motor_adc *adc, struct adc_callback_t *cb);
void adc_start(struct motor_adc *adc);

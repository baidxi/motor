#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/time_units.h>

#include <motor/adc.h>
#include <motor/mc.h>
#include <motor/svpwm.h>

#include <menu/menu.h>


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct svpwm_channel_info svpwm_channels[] = {
    {
        .id = 1,
        .en = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), gpios, 0)
    },
    {
        .id = 2,
        .en = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), gpios, 1)
    },
    {
        .id = 3,
        .en = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), gpios, 2)
    }
};

static const struct adc_channel_info adc_channels[] = {
    {
        .cfg = ADC_CHANNEL_CFG_DT(DT_CHILD(DT_ALIAS(adc2), channel_5)),
        .id = BEMF_A,
    },
    {
        .cfg = ADC_CHANNEL_CFG_DT(DT_CHILD(DT_ALIAS(adc2), channel_4)),
        .id = BEMF_B,
    },
    {
        .cfg = ADC_CHANNEL_CFG_DT(DT_CHILD(DT_ALIAS(adc2), channel_13)),
        .id = BEMF_C,
    },
    {
        .cfg = ADC_CHANNEL_CFG_DT(DT_CHILD(DT_ALIAS(adc2), channel_11)),
        .id = VOLTAGE_BUS,
    },
    {
        .cfg = ADC_CHANNEL_CFG_DT(DT_CHILD(DT_ALIAS(adc2), channel_12)),
        .id = SPEED_VALUE,
    },
    {
        .cfg = ADC_CHANNEL_CFG_DT(DT_CHILD(DT_ALIAS(adc2), channel_3)),
        .id = CURR_A,
    },
    {
        .cfg = ADC_CHANNEL_CFG_DT(DT_CHILD(DT_ALIAS(adc2), channel_17)),
        .id = CURR_C,
    }
};

static const struct svpwm_info svpwm_info = {
    .channels = svpwm_channels,
    .nb_channels = ARRAY_SIZE(svpwm_channels),
    .dev = DEVICE_DT_GET(DT_ALIAS(pwm1)),
};

static const struct adc_info adc_info = {
    .channels = adc_channels,
    .nb_channels = ARRAY_SIZE(adc_channels),
    .dev = DEVICE_DT_GET(DT_ALIAS(adc2)),
};


static uint32_t get_system_clock(void)
{
    uint32_t clk_rate = 0;

    clk_rate = sys_clock_hw_cycles_per_sec();
    if (clk_rate > 0) {
        LOG_INF("System clock frequency from sys_clock: %d MHz", clk_rate / 1000000 );
        return clk_rate;
    }
    
    LOG_ERR("Failed to get system clock frequency");
    return 0;
}

int main(void)
{
    const struct device *disp_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    struct mc_t *mc;
    struct menu_t *menu;
    uint32_t system_clock;

    if (!device_is_ready(disp_dev))
    {
        LOG_ERR("Device %s not found, Abort.", disp_dev->name);
        return 0;
    }

    if (menu_init(disp_dev, &menu))
    {
        LOG_ERR("menu init err");
        return 0;
    }

    menu_sensor_bind(menu, DEVICE_DT_GET(DT_ALIAS(qdec0)));

    system_clock = get_system_clock();
    if (system_clock == 0) {
        LOG_ERR("Failed to get system clock, using default 170MHz");
        system_clock = 170000000; /* 默认值 */
    }

    mc = mc_init(MOTOR_TYPE_BLDC, 1);

    if (mc_svpwm_init(mc, &svpwm_info, 0))
    {
        return -1;
    }

    if (mc_adc_init(mc, &adc_info))
    {
        return -1;
    }
    
    menu_driver_bind(menu, mc);

    mc_setup_menu_bind(mc, menu);

    mc_adc_start(mc);

    menu_render_start(menu);

    while(1)
    {
        k_sleep(K_MSEC(10));
    }

    return 0;
}

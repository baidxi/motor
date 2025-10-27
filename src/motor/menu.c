#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <motor/adc.h>
#include <menu/menu.h>
#include <motor/mc.h>

#include <stdint.h>

LOG_MODULE_REGISTER(motor_menu, CONFIG_LOG_DEFAULT_LEVEL);

struct menu_item_t setup_motor_item = {
    .name = "Motor",
    .id = 3,
    .style = MENU_STYLE_NORMAL,
};

struct menu_item_t motor_speed_item = {
    .name = "Speed",
    .id = 4,
    .style = MENU_STYLE_NORMAL | MENU_STYLE_VALUE_LABEL,
    .type = MENU_ITEM_TYPE_INPUT,
    .input = {
        .min = 0,
        .max = 5000,
        .step = 100,
        .dev = DEVICE_DT_GET(DT_ALIAS(adc2)),
        .cb = NULL,
    },
    .visible = true,
};

// static void motor_enable_switch_cb(struct menu_item_t *item, bool is_on)
// {
//     LOG_INF("Motor Enable Switch is now %s", is_on ? "ON" : "OFF");
// }

// struct menu_item_t motor_enable_item = {
//     .name = "Enable",
//     .id = 5,
//     .style = MENU_STYLE_NORMAL,
//     .type = MENU_ITEM_TYPE_SWITCH,
//     .switch_ctrl = {
//         .is_on = false,
//         .cb = motor_enable_switch_cb,
//         .text_on = "Enabled",
//         .text_off = "Disabled",
//     },
//     .visible = true,
// };

// static const char *motor_mode_options[] = {"Speed", "Torque"};

// static void motor_mode_list_cb(struct menu_item_t *item, uint8_t selected_index)
// {
//     LOG_INF("Motor mode changed to %s", motor_mode_options[selected_index]);
// }

// struct menu_item_t motor_mode_item = {
//     .name = "Mode",
//     .id = 7,
//     .style = MENU_STYLE_NORMAL,
//     .type = MENU_ITEM_TYPE_LIST,
//     .list = {
//         .options = motor_mode_options,
//         .num_options = ARRAY_SIZE(motor_mode_options),
//         .selected_index = 0,
//         .cb = motor_mode_list_cb,
//         .layout = MENU_LAYOUT_VERTICAL,
//         .title = "Select Mode",
//     },
//     .visible = true,
// };

static void speed_item_value_change_work(struct k_work *work)
{
    struct item_input_t *input = CONTAINER_OF(work, struct item_input_t, work);
    uint32_t batch_avg = 0;
    size_t count = input->values_count / sizeof(uint16_t);

    if (count > 0) {
        for (int i = 0; i < count; i++) {
            batch_avg += input->values[i];
        }
        batch_avg /= count;

        input->filter_window[input->filter_index] = batch_avg;
        input->filter_index = (input->filter_index + 1) % ADC_FILTER_WINDOW_SIZE;

        uint32_t filtered_avg = 0;
        for (int i = 0; i < ADC_FILTER_WINDOW_SIZE; i++) {
            filtered_avg += input->filter_window[i];
        }
        filtered_avg /= ADC_FILTER_WINDOW_SIZE;

        uint32_t new_rpm = (filtered_avg * input->max) / 4095;

        input->live_value = new_rpm;
    }
}

static void speed_item_value_change_func(struct adc_callback_t *self, uint16_t *values, size_t count, void *param)
{
    struct menu_item_t *item = param;
    item->input.values = values;
    item->input.values_count = count;

    k_work_submit(&item->input.work);
}

void mc_setup_menu_bind(struct mc_t *mc, struct menu_t *menu)
{
    struct menu_group_t *motor_group;
    static struct adc_callback_t speed_event_callback = {
        .func = speed_item_value_change_func,
        .id = SPEED_VALUE,
    };

    motor_group = menu_group_create(menu, "Motor", 0, 5, 120, 75, COLOR_WHITE, MENU_LAYOUT_VERTICAL | MENU_ALIGN_V_CENTER, 0);

    k_work_init(&motor_speed_item.input.work, speed_item_value_change_work);


    menu_group_add_item(motor_group, &motor_speed_item);
    // menu_group_add_item(motor_group, &motor_enable_item);
    // menu_group_add_item(motor_group, &motor_mode_item);
    menu_group_bind_item(motor_group, &setup_motor_item);

    speed_event_callback.param = &motor_speed_item;

    mc_adc_event_register(mc, &speed_event_callback);
}

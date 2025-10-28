#include <zephyr/kernel.h>

#include <motor/motor.h>
#include <motor/svpwm.h>
#include <menu/menu.h>
#include <motor/mc.h>

struct motor_t {
    uint8_t type;
    struct svpwm_t *svpwm;
    uint8_t id;
    uint8_t state;
    uint16_t freq;
};

enum motor_state_t {
    MOTOR_STATE_IDLE,
    MOTOR_STATE_IDENTIFICATION,
    MOTOR_STATE_ALIGNMENT,
    MOTOR_STATE_STARTUP,
    MOTOR_STATE_RUN,
    MOTOR_STATE_STOPPING,
    MOTOR_STATE_FAULT,
};

struct motor_t *motor_init(uint8_t type, uint8_t id)
{
    struct motor_t *motor = k_malloc(sizeof(*motor));
    motor->type = type;
    motor->id = id;
    motor->state = MOTOR_STATE_IDLE;

    return motor;
}

int motor_svpwm_init(struct motor_t *motor, const struct svpwm_info *info)
{
    motor->svpwm = svpwm_init(info);

    return motor->svpwm ? 0 : -ENODEV;
}

void motor_type_change_cb(struct menu_item_t *item, uint8_t type)
{
    struct mc_t *mc = menu_driver_get(item->menu);
    int n = mc_motor_count(mc);
    struct motor_t *motor;

    for (int i = 0; i < n; i++)
    {
        motor = mc_motor_get(mc, i);
        if (motor && motor->state == MOTOR_STATE_IDLE)
            motor->type = type;
    }
}

void motor_svpwm_freq_set_range(struct motor_t *motor, uint16_t min, uint16_t max)
{
    svpwm_freq_set_range(motor->svpwm, min, max);
}

int motor_freq_set(struct motor_t *motor, uint16_t freq)
{
    if (motor && motor->state == MOTOR_STATE_IDLE && freq)
    {
        motor->freq = freq;
    } else {
        return -EBUSY;
    }

    svpwm_freq_set(motor->svpwm, freq);

    return 0;
}

int motor_update_freq_and_pulse(struct motor_t *motor, uint16_t freq, uint16_t pulse)
{
    return 0;
}

void motor_svpwm_freq_set_cb(struct menu_item_t *item, int32_t min, int32_t max)
{
    struct mc_t *mc = menu_driver_get(item->menu);
    int n = mc_motor_count(mc);
    struct motor_t *motor;

    for (int i = 0; i < n; i++)
    {
        motor = mc_motor_get(mc, i);
        if (motor && motor->state == MOTOR_STATE_IDLE)
        {
            motor_svpwm_freq_set_range(motor, min, max);
        }
    }
}
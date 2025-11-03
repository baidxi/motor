#include <zephyr/kernel.h>

#include <motor/motor.h>
#include <motor/svpwm.h>
#include <menu/menu.h>
#include <motor/mc.h>

#define MOTOR_THREAD_STACK_SIZE 512

struct motor_t {
    uint8_t type;
    struct svpwm_t *svpwm;
    uint8_t id;
    uint8_t state;
    uint16_t freq;
    k_tid_t tid;
    struct k_thread thread;
    K_KERNEL_STACK_MEMBER(stack, MOTOR_THREAD_STACK_SIZE);
    struct k_event event;
    struct mc_t *mc;
    struct mc_adc_info *adc;
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

static void motor_thread_func(void *v1, void *v2, void *v3)
{
    struct motor_t *motor = v1;
    int ret;

    while(true)
    {
        switch(motor->state)
        {
            case MOTOR_STATE_IDLE:
                ret = k_event_wait(&motor->event, MOTOR_EVENT_READY, false, K_FOREVER);
                if (ret == 0)
                {

                }
                break;
            case MOTOR_STATE_IDENTIFICATION:
                break;
            case MOTOR_STATE_ALIGNMENT:
                break;
            case MOTOR_STATE_STARTUP:
                break;
            case MOTOR_STATE_RUN:
                break;
            case MOTOR_STATE_STOPPING:
                break;
            case MOTOR_STATE_FAULT:
                break;
        }
    }
}

struct motor_t *motor_init(struct mc_t *mc, struct mc_adc_info *adc, uint8_t type, uint8_t id)
{
    struct motor_t *motor = k_malloc(sizeof(*motor));
    motor->type = type;
    motor->id = id;
    motor->state = MOTOR_STATE_IDLE;
    motor->mc = mc;
    motor->adc = adc;

    k_event_init(&motor->event);

    motor->tid = k_thread_create(&motor->thread, motor->stack, MOTOR_THREAD_STACK_SIZE, motor_thread_func, motor, NULL, NULL, 50, 0,K_NO_WAIT);

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

void motor_ready(struct motor_t *motor)
{
    k_event_post(&motor->event, MOTOR_EVENT_READY);
}

void motor_idle(struct motor_t *motor)
{
    k_event_post(&motor->event, MOTOR_EVENT_IDLE);
}
#include <zephyr/kernel.h>

#include <motor/motor.h>
#include <motor/svpwm.h>

struct motor_t {
    uint8_t type;
    struct svpwm_t *svpwm;
    uint8_t id;
};

struct motor_t *motor_init(uint8_t type, uint8_t id)
{
    struct motor_t *motor = k_malloc(sizeof(*motor));
    motor->type = type;
    motor->id = id;

    return motor;
}

int motor_svpwm_init(struct motor_t *motor, const struct svpwm_info *info)
{
    motor->svpwm = svpwm_init(info);

    return motor->svpwm ? 0 : -ENODEV;
}
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <motor/mc.h>
#include <motor/svpwm.h>
#include <motor/adc.h>
#include <motor/motor.h>

struct mc_t {
    struct motor_t **motors;
    int nb_motor;
    struct adc_t *adc;
};

LOG_MODULE_REGISTER(mc, LOG_LEVEL_INF);

struct mc_t *mc_init(uint8_t type, int nb_motor)
{
    int i;
    struct mc_t *mc = k_malloc(sizeof(*mc));

    mc->motors = k_malloc(sizeof(void *) * nb_motor);

    for (i = 0; i < nb_motor; i++)
    {
        mc->motors[i] = motor_init(type, i);
    }

    mc->nb_motor = nb_motor;

    return mc;
}

int mc_svpwm_init(struct mc_t *mc, const struct svpwm_info *info, int motor_id)
{
    int ret;

    if (motor_id > mc->nb_motor)
        return -EINVAL;

    ret = motor_svpwm_init(mc->motors[motor_id], info);

    return ret;
}

int mc_adc_init(struct mc_t *mc, const struct adc_info *info)
{
    mc->adc = adc_init(info);
    return mc->adc ? 0 : -ENODEV;
}

int mc_adc_event_register(struct mc_t *mc, struct adc_callback_t *cb)
{
    return adc_register_callback(mc->adc, cb);
}

void mc_adc_start(struct mc_t *mc)
{
    adc_start(mc->adc);
}

struct motor_t *mc_motor_get(struct mc_t *mc, uint8_t id)
{
    if (id > mc->nb_motor)
        return NULL;

    return mc->motors[id];
}

int mc_motor_count(struct mc_t *mc)
{
    return mc->nb_motor;
}
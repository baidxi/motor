#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <motor/mc.h>
#include <motor/svpwm.h>
#include <motor/adc.h>
#include <motor/motor.h>

struct mc_t {
    struct motor_t **motors;
    int nb_motor;
    uint16_t vbus_raw;
    double vbus;
    struct adc_t *adc;
    struct adc_callback_t callback;
    struct {
        uint32_t voltage_min;
        uint32_t voltage_max;
    } motor;
};

LOG_MODULE_REGISTER(mc, LOG_LEVEL_INF);

bool mc_motor_voltage_check(struct mc_t *mc)
{
    if (mc->vbus > mc->motor.voltage_max || mc->vbus < mc->motor.voltage_min)
    {
        return false;
    }

    return true;
}

static void vbus_adc_value_cb(struct adc_callback_t *self, uint16_t *values, size_t count, void *param)
{
    struct mc_t *mc = CONTAINER_OF(self, struct mc_t, callback);
    double value = 0.0;
    uint32_t sum = 0;
    count = count / sizeof(uint16_t);
    int i;

    for (i = 0; i < count; i++)
    {
        sum += values[i];
    }

    value = (double)(sum / count);

    mc->vbus_raw = value;

    mc->vbus = ((float)value / 4095.0f) * 3300.0f * (104.7f / 4.7f);
}

void mc_motor_voltage_range_set(struct mc_t *mc, int min, int max)
{
    mc->motor.voltage_min = min;
    mc->motor.voltage_max = max;
}

struct mc_t *mc_init(uint8_t type, int nb_motor)
{
    int i;
    struct mc_t *mc = k_malloc(sizeof(*mc));

    mc->callback.func = vbus_adc_value_cb,
    mc->callback.id = VOLTAGE_BUS,
    mc->callback.param = NULL;

    mc->motors = k_malloc(sizeof(void *) * nb_motor);

    for (i = 0; i < nb_motor; i++)
    {
        mc->motors[i] = motor_init(mc, type, i);
    }

    mc->vbus_raw = 0;

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

    if (mc->adc)
    {
        adc_register_callback(mc->adc, &mc->callback);
    }

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

bool mc_motor_ready(struct mc_t *mc, bool is_ready)
{
    int i;

    if (is_ready)
    {
        if (!mc_motor_voltage_check(mc))
        {
            return false;
        }
        for (i = 0; i < mc->nb_motor; i++)
        {
            motor_ready(mc->motors[i]);           
        }    
    } else {
        for (i = 0; i < mc->nb_motor; i++)
        {
            motor_idle(mc->motors[i]);
        }
    }

    return true;
}

double mc_vbus_get(struct mc_t *mc)
{
    return mc->vbus;
}

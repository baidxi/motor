#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <motor/mc.h>
#include <motor/svpwm.h>
#include <motor/adc.h>
#include <motor/motor.h>

#include <menu/menu.h>

struct mc_adc_info {
    uint16_t raw_value;
    double value;
    struct adc_callback_t cb;
};

struct mc_t {
    struct motor_t **motors;
    int nb_motor;
    struct adc_t *adc;
    struct {
        uint32_t voltage_min;
        uint32_t voltage_max;
    } motor;
    struct menu_t *menu;
    struct mc_adc_info ia, ic, va, vb, vc, vbus;
};

LOG_MODULE_REGISTER(mc, LOG_LEVEL_INF);

static bool mc_motor_voltage_check(struct mc_t *mc)
{
    if (mc->vbus.value > mc->motor.voltage_max || mc->vbus.value < mc->motor.voltage_min)
    {
        return false;
    }

    return true;
}

void mc_motor_voltage_range_set(struct mc_t *mc, int min, int max)
{
    mc->motor.voltage_min = min;
    mc->motor.voltage_max = max;
}

static void mc_adc_callback_entry(struct adc_callback_t *self, uint16_t *values, size_t count, void *param)
{
    struct mc_adc_info *info = CONTAINER_OF(self, struct mc_adc_info, cb);
    double value = 0.0;
    uint32_t sum = 0;
    count = count / sizeof(uint16_t);
    int i;

    for (i = 0; i < count; i++)
    {
        sum += values[i];
    }

    value = (double)(sum / count);

    info->raw_value =  value;

    switch(self->id)
    {
        case VOLTAGE_BUS:
            info->value = ((float)value / 4095.0f) * 3.3f * (104.7f / 4.7f);
            break;
    }
}

struct mc_t *mc_init(uint8_t type, int nb_motor)
{
    int i;
    struct mc_t *mc = k_malloc(sizeof(*mc));

    mc->motors = k_malloc(sizeof(void *) * nb_motor);

    for (i = 0; i < nb_motor; i++)
    {
        mc->motors[i] = motor_init(mc, type, i);
    }

    mc->vbus.raw_value = 0;

    mc->nb_motor = nb_motor;

    mc->vbus.cb.func = mc_adc_callback_entry,
    mc->vbus.cb.id = VOLTAGE_BUS,

    mc->ia.cb.id = CURR_A;
    mc->ia.cb.func = mc_adc_callback_entry;
    mc->ic.cb.id = CURR_C;
    mc->ic.cb.func = mc_adc_callback_entry;

    mc->va.cb.id = BEMF_A;
    mc->va.cb.func = mc_adc_callback_entry;

    mc->vb.cb.id = BEMF_B;
    mc->vb.cb.func = mc_adc_callback_entry;

    mc->vc.cb.id = BEMF_C;
    mc->vc.cb.func = mc_adc_callback_entry;

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
        adc_register_callback(mc->adc, &mc->vbus.cb);
        adc_register_callback(mc->adc, &mc->ia.cb);
        adc_register_callback(mc->adc, &mc->ic.cb);
        adc_register_callback(mc->adc, &mc->va.cb);
        adc_register_callback(mc->adc, &mc->vb.cb);
        adc_register_callback(mc->adc, &mc->vc.cb);
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
            menu_dialog_show(mc->menu, DIALOG_STYLE_ERR, "voltage err", NULL, "voltage %dV - %dV", mc->motor.voltage_min / 1000, mc->motor.voltage_max / 1000);
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
    return mc->vbus.value;
}

void mc_menu_bind(struct menu_t *menu, struct mc_t *mc)
{
    mc->menu = menu;
}
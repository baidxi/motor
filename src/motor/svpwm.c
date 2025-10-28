#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <motor/svpwm.h>

LOG_MODULE_REGISTER(svpwm, LOG_LEVEL_INF);

struct svpwm_t {
    const struct svpwm_info *info;
    uint16_t freq_max;
    uint16_t freq_min;
};

struct svpwm_t *svpwm_init(const struct svpwm_info *info)
{
    struct svpwm_t *svpwm;
    const struct svpwm_channel_info *ch;
    int i;

    if (!device_is_ready(info->dev))
    {
        LOG_ERR("pwm device not ready");
        return NULL;
    }
    
    svpwm = k_malloc(sizeof(*svpwm));
    svpwm->info = info;

    for (i = 0; i < info->nb_channels; i++)
    {
        ch = &info->channels[i];
        gpio_pin_set_dt(&ch->en, 0);
    }

    
    return svpwm;
}

void svpwm_freq_set(struct svpwm_t *pwm, uint16_t min, uint16_t max)
{
    pwm->freq_min = min;
    pwm->freq_max = max;
}
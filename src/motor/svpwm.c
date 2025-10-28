#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include <motor/svpwm.h>

#include <errno.h>

LOG_MODULE_REGISTER(svpwm, LOG_LEVEL_INF);

struct svpwm_t {
    const struct svpwm_info *info;
    uint32_t freq_min;
    uint32_t freq_max;
    uint32_t freq_curr;
    uint32_t pulse[4];
    uint64_t cycles_per_sec;
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
        svpwm->pulse[i] = 0;
    }

    pwm_get_cycles_per_sec(info->dev, 1, &svpwm->cycles_per_sec);
    
    return svpwm;
}

int svpwm_freq_set(struct svpwm_t *pwm, uint16_t freq)
{
    const struct svpwm_channel_info *ch;
    uint32_t period_cycles;
    int ret, i;

    if (freq > pwm->freq_max || freq < pwm->freq_min)
    {
        return -EINVAL;
    }

    for (i = 0; i < pwm->info->nb_channels; i++)
    {
        ch = &pwm->info->channels[i];

        period_cycles = (uint32_t)(pwm->cycles_per_sec / freq);
        ret = pwm_set(pwm->info->dev, ch->id, period_cycles, pwm->pulse[i], 0);
        if (ret)
        {
            LOG_ERR("%s set freq %d Hz err", pwm->info->dev->name, freq);
        }
    }

    pwm->freq_curr = period_cycles;

    return 0;
}

int svpwm_update_freq_and_pulse(struct svpwm_t *pwm, uint8_t channel, uint16_t freq, uint16_t pulse)
{
    const struct svpwm_channel_info *ch;
    uint32_t period_cycles = pwm->cycles_per_sec / freq;
    uint32_t pulse_cycles = pwm->cycles_per_sec / pulse;

    if (period_cycles > pwm->freq_max || period_cycles < pwm->freq_min)
    {
        return -ERANGE;
    }

    if (pulse_cycles > period_cycles)
    {
        return -ERANGE;
    }

    if (channel > (pwm->info->nb_channels - 1))
    {
        return -EINVAL;
    }

    ch = &pwm->info->channels[channel];

    pwm->freq_curr = period_cycles;
    pwm->pulse[channel] = pulse_cycles;

    return pwm_set(pwm->info->dev, ch->id, period_cycles, pulse_cycles, 0);

}

int svpwm_pulse_update(struct svpwm_t *pwm, uint8_t channel, uint16_t pulse)
{
    const struct svpwm_channel_info *ch;
    int ret;

    if (channel > (pwm->info->nb_channels - 1))
        return -EINVAL;


    if (pulse > pwm->freq_curr)
        return -ERANGE;

    ch = &pwm->info->channels[channel];

    ret = pwm_set(pwm->info->dev, ch->id, pwm->freq_curr, pulse, 0);
    if (ret)
    {
        LOG_ERR("update pulse err:%d", ret);
        return ret;
    }

    pwm->pulse[channel] = pulse;

    return ret;
}

void svpwm_freq_set_range(struct svpwm_t *pwm, uint16_t min, uint16_t max)
{
    uint32_t period_cycles;

    LOG_INF("Setting PWM frequency range to %u-%u Hz", min, max);

    period_cycles = (uint32_t)(pwm->cycles_per_sec / min);
    
    if (period_cycles > 65535) {
        uint32_t prescaler = (period_cycles / 65535) + 1;
        LOG_ERR("Calculated period (%u) for min frequency exceeds 16-bit timer limit.", period_cycles);
        LOG_ERR("Please set 'prescaler = <%u>;' in your DTS file for the PWM node.", prescaler);
        return;
    }

    pwm->freq_min = period_cycles;

    period_cycles = (uint32_t)(pwm->cycles_per_sec / max);
    if (period_cycles > 65535) {
        uint32_t prescaler = (period_cycles / 65535) + 1;
        LOG_ERR("Calculated period (%u) for max frequency exceeds 16-bit timer limit.", period_cycles);
        LOG_ERR("Please set 'prescaler = <%u>;' in your DTS file for the PWM node.", prescaler);
        return;
    }

    pwm->freq_max = period_cycles;
}
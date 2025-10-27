#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <motor/svpwm.h>

LOG_MODULE_REGISTER(svpwm, LOG_LEVEL_INF);

struct svpwm_t {
    const struct svpwm_info *info;
};

struct svpwm_t *svpwm_init(const struct svpwm_info *info)
{
    struct svpwm_t *svpwm;

    if (!device_is_ready(info->dev))
    {
        LOG_ERR("pwm device not ready");
        return NULL;
    }
    
    svpwm = k_malloc(sizeof(*svpwm));
    svpwm->info = info;
    
    return svpwm;
}
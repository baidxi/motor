#include <zephyr/kernel.h>
#include <motor/svpwm.h>

struct svpwm_t {
    const struct svpwm_info *info;
};

struct svpwm_t *svpwm_init(const struct svpwm_info *info)
{
    struct svpwm_t *svpwm = k_malloc(sizeof(*svpwm));
    svpwm->info = info;
    
    return svpwm;
}
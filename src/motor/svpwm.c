#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include <motor/svpwm.h>

#include <string.h>

LOG_MODULE_REGISTER(svpwm, LOG_LEVEL_INF);

struct svpwm_data {
    const struct pwm_info *info;
    struct svpwm pwm;
    uint16_t duty_cycle;
    uint16_t cycle;
    uint16_t freq;
    uint32_t pwm_period;      /* PWM周期(ns) - 改为32位避免溢出 */
    uint16_t dead_time;       /* 死区时间(ns) */
    float duty_u;             /* U相占空比 */
    float duty_v;             /* V相占空比 */
    float duty_w;             /* W相占空比 */
    uint8_t sector;           /* 当前扇区 */
    bool initialized;         /* 初始化标志 */
    uint32_t system_clock_freq; /* 系统时钟频率 */
    uint32_t pwm_clock_div;     /* PWM时钟分频系数 */
    uint8_t pwm_prescaler;      /* PWM预分频器值 */
    bool center_aligned;       /* 是否为中心对齐计数模式 */
};

/* SVPWM参数定义 */
#define PWM_DEAD_TIME         500   /* 死区时间(ns) */
#define PWM_MIN_DUTY         0.0f   /* 最小占空比 */
#define PWM_MAX_DUTY         1.0f   /* 最大占空比 */
#define SVPWM_SECTOR_NUM     6      /* 扇区数量 */

static void svpwm_enable(struct svpwm *pwm, uint8_t ch)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);

    if (ch > data->info->nb_channels)
        return;

    gpio_pin_set_dt(&data->info->channels[ch].en, 1);
}

static void svpwm_disable(struct svpwm *pwm, uint8_t ch)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);

    if (ch > data->info->nb_channels)
        return;

    gpio_pin_set_dt(&data->info->channels[ch].en, 0);
}

static void svpwm_duty_cycle_update(struct svpwm *pwm, uint8_t ch, float duty_cycle)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);
    int ret;
    uint32_t pulse_width;

    if (!data || !data->initialized) {
        LOG_ERR("SVPWM not initialized");
        return;
    }

    if (ch >= data->info->nb_channels) {
        LOG_ERR("Invalid channel number: %d", ch);
        return;
    }

    if (duty_cycle < PWM_MIN_DUTY) {
        duty_cycle = PWM_MIN_DUTY;
    } else if (duty_cycle > PWM_MAX_DUTY) {
        duty_cycle = PWM_MAX_DUTY;
    }

    pulse_width = (uint32_t)(data->pwm_period * duty_cycle);
    
    if (pulse_width > data->pwm_period) {
        pulse_width = data->pwm_period;
    }
    
    LOG_DBG("Using period %d ns for precise pulse width calculation: %d",
            data->pwm_period, pulse_width);
            
    ret = pwm_set(data->info->dev, data->info->channels[ch].channel_id,
                  data->pwm_period, pulse_width, PWM_POLARITY_NORMAL);
    
    if (ret != 0) {
        LOG_ERR("Failed to set PWM duty cycle for channel %d: %d", ch, ret);
        return;
    }

    switch (ch) {
        case 0:
            data->duty_u = duty_cycle;
            break;
        case 1:
            data->duty_v = duty_cycle;
            break;
        case 2:
            data->duty_w = duty_cycle;
            break;
        default:
            break;
    }

    LOG_DBG("PWM channel %d duty cycle set to %.2f (pulse width: %d)",
            ch, duty_cycle, pulse_width);
}

static uint8_t get_pwm_prescaler(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0;
}

static bool is_center_aligned_mode(const struct device *dev)
{
    ARG_UNUSED(dev);
    return true; 
}

static const struct svpwm_ops ops = {
    .enable = svpwm_enable,
    .disable = svpwm_disable,
    .update = svpwm_duty_cycle_update,
};


struct svpwm *svpwm_init(const struct pwm_info *info, uint16_t freq, uint16_t cycle, uint32_t system_clock_freq)
{
    struct svpwm_data *data;
    int ret;
    uint8_t i;

    if (!info || !info->dev || !info->channels || info->nb_channels == 0) {
        LOG_ERR("Invalid PWM info parameters");
        return NULL;
    }

    data = k_malloc(sizeof(*data));
    if (!data) {
        LOG_ERR("Failed to allocate memory for SVPWM data");
        return NULL;
    }

    memset(data, 0, sizeof(*data));
    data->info = info;
    data->freq = freq;
    data->dead_time = PWM_DEAD_TIME;
    data->duty_u = 0.0f;
    data->duty_v = 0.0f;
    data->duty_w = 0.0f;
    data->sector = 0;
    data->initialized = false;
    data->system_clock_freq = system_clock_freq;
    data->pwm_clock_div = 1; /* 默认不分频 */
    data->pwm_prescaler = get_pwm_prescaler(info->dev);
    data->center_aligned = is_center_aligned_mode(info->dev);

    if (freq > 0) {
        data->pwm_period = (uint32_t)1000000000 / freq; /* 将Hz转换为ns */

        if (cycle > 0) {
            data->cycle = cycle;
        } else {
            data->cycle = 0;
        }
        
        struct svpwm *pwm = &data->pwm;
        if (svpwm_set_frequency(pwm, freq) != 0) {
            LOG_ERR("Failed to set PWM frequency during initialization");
            k_free(data);
            return NULL;
        }
    } else {
        LOG_ERR("Invalid PWM frequency: %d Hz", freq);
        k_free(data);
        return NULL;
    }

    for (i = 0; i < info->nb_channels; i++) {
        if (!gpio_is_ready_dt(&info->channels[i].en)) {
            LOG_ERR("GPIO port %s not ready", info->channels[i].en.port->name);
            k_free(data);
            return NULL;
        }

        ret = gpio_pin_configure_dt(&info->channels[i].en, GPIO_OUTPUT_INACTIVE);
        if (ret != 0) {
            LOG_ERR("Failed to configure GPIO pin %d: %d", info->channels[i].en.pin, ret);
            k_free(data);
            return NULL;
        }

        ret = pwm_set(data->info->dev, data->info->channels[i].channel_id,
                      data->pwm_period, 0, PWM_POLARITY_NORMAL);
        if (ret != 0) {
            LOG_ERR("Failed to initialize PWM channel %d: %d", i, ret);
            k_free(data);
            return NULL;
        }

        LOG_DBG("PWM channel %d configured", info->channels[i].channel_id);
    }

    data->pwm.ops = &ops;
    data->initialized = true;

    LOG_INF("SVPWM initialized with %d channels, frequency %d Hz, period %d ns",
            info->nb_channels, freq, data->pwm_period);

    return &data->pwm;
}

static uint8_t svpwm_calculate_sector(float alpha, float beta)
{
    uint8_t sector = 0;
    float v1, v2, v3;
    
    v1 = beta;
    v2 = (-0.5f * beta + 0.866025f * alpha); /* 0.866025 ≈ sqrt(3)/2 */
    v3 = (-0.5f * beta - 0.866025f * alpha);
    
    if (v1 > 0) sector |= 1;
    if (v2 > 0) sector |= 2;
    if (v3 > 0) sector |= 4;
    
    switch (sector) {
        case 3: sector = 1; break;
        case 1: sector = 2; break;
        case 5: sector = 3; break;
        case 4: sector = 4; break;
        case 6: sector = 5; break;
        case 2: sector = 6; break;
        default: sector = 0; break;
    }
    
    return sector;
}

static void svpwm_calculate_duty_cycles(float alpha, float beta, float *duty_u, float *duty_v, float *duty_w)
{
    uint8_t sector;
    float x, y, z;
    float t1, t2;
    float t_a, t_b, t_c;
    
    sector = svpwm_calculate_sector(alpha, beta);
    
    x = beta;
    y = (0.5f * beta + 0.866025f * alpha);
    z = (0.5f * beta - 0.866025f * alpha);
    
    switch (sector) {
        case 1:
            t1 = z;
            t2 = y;
            break;
        case 2:
            t1 = -z;
            t2 = -x;
            break;
        case 3:
            t1 = x;
            t2 = z;
            break;
        case 4:
            t1 = -x;
            t2 = -y;
            break;
        case 5:
            t1 = y;
            t2 = -z;
            break;
        case 6:
            t1 = -y;
            t2 = x;
            break;
        default:
            t1 = 0;
            t2 = 0;
            break;
    }
    
    t_a = (1.0f - t1 - t2) / 2.0f;
    t_b = t_a + t1;
    t_c = t_b + t2;
    
    switch (sector) {
        case 1:
            *duty_u = t_b;
            *duty_v = t_c;
            *duty_w = t_a;
            break;
        case 2:
            *duty_u = t_a;
            *duty_v = t_b;
            *duty_w = t_c;
            break;
        case 3:
            *duty_u = t_c;
            *duty_v = t_a;
            *duty_w = t_b;
            break;
        case 4:
            *duty_u = t_c;
            *duty_v = t_b;
            *duty_w = t_a;
            break;
        case 5:
            *duty_u = t_b;
            *duty_v = t_a;
            *duty_w = t_c;
            break;
        case 6:
            *duty_u = t_a;
            *duty_v = t_c;
            *duty_w = t_b;
            break;
        default:
            *duty_u = 0.5f;
            *duty_v = 0.5f;
            *duty_w = 0.5f;
            break;
    }
    
    if (*duty_u < PWM_MIN_DUTY) *duty_u = PWM_MIN_DUTY;
    if (*duty_u > PWM_MAX_DUTY) *duty_u = PWM_MAX_DUTY;
    if (*duty_v < PWM_MIN_DUTY) *duty_v = PWM_MIN_DUTY;
    if (*duty_v > PWM_MAX_DUTY) *duty_v = PWM_MAX_DUTY;
    if (*duty_w < PWM_MIN_DUTY) *duty_w = PWM_MIN_DUTY;
    if (*duty_w > PWM_MAX_DUTY) *duty_w = PWM_MAX_DUTY;
}

void svpwm_update_output(struct svpwm *pwm, float alpha, float beta)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);
    float duty_u, duty_v, duty_w;
    
    if (!data || !data->initialized) {
        LOG_ERR("SVPWM not initialized");
        return;
    }
    
    svpwm_calculate_duty_cycles(alpha, beta, &duty_u, &duty_v, &duty_w);
    
    data->sector = svpwm_calculate_sector(alpha, beta);
    
    data->pwm.ops->update(pwm, 0, duty_u);  /* U相 */
    data->pwm.ops->update(pwm, 1, duty_v);  /* V相 */
    data->pwm.ops->update(pwm, 2, duty_w);  /* W相 */
    
    LOG_DBG("SVPWM updated: sector=%d, duty_u=%.2f, duty_v=%.2f, duty_w=%.2f",
            data->sector, duty_u, duty_v, duty_w);
}

uint8_t svpwm_get_sector(struct svpwm *pwm)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);
    
    if (!data || !data->initialized) {
        return 0;
    }
    
    return data->sector;
}

void svpwm_get_duty_cycles(struct svpwm *pwm, float *duty_u, float *duty_v, float *duty_w)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);
    
    if (!data || !data->initialized) {
        if (duty_u) *duty_u = 0.0f;
        if (duty_v) *duty_v = 0.0f;
        if (duty_w) *duty_w = 0.0f;
        return;
    }
    
    if (duty_u) *duty_u = data->duty_u;
    if (duty_v) *duty_v = data->duty_v;
    if (duty_w) *duty_w = data->duty_w;
}

int svpwm_set_frequency(struct svpwm *pwm, uint16_t freq)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);
    
    if (!data) {
        return -EINVAL;
    }
    
    if (freq == 0) {
        return -EINVAL;
    }
    
    data->freq = freq;

    uint32_t pwm_clock_freq = data->system_clock_freq / (data->pwm_prescaler + 1);
    uint32_t freq_divider = data->center_aligned ? 2 : 1;
    uint32_t calculated_cycle;
    if (data->cycle > 0) {
        calculated_cycle = data->cycle;
        LOG_DBG("Using existing cycle value: %d", calculated_cycle);
    } else {
        calculated_cycle = (pwm_clock_freq / (freq_divider * freq)) - 1;
        
        if (calculated_cycle < 100) {
            calculated_cycle = 100; /* 最小cycle值 */
            LOG_WRN("Calculated cycle value too small, using minimum value: %d", calculated_cycle);
        } else if (calculated_cycle > 65535) {
            calculated_cycle = 65535; /* 最大cycle值 */
            LOG_WRN("Calculated cycle value too large, using maximum value: %d", calculated_cycle);
        }
        
        /* 更新cycle值 */
        data->cycle = calculated_cycle;
    }
    
    uint32_t actual_freq = pwm_clock_freq / (freq_divider * (data->cycle + 1));
    uint32_t actual_period = (uint32_t)((uint64_t)(freq_divider * (data->cycle + 1)) * 1000000000 / pwm_clock_freq);
    
    data->pwm_period = actual_period;
    
    LOG_INF("SVPWM frequency: target=%d Hz, actual=%d Hz, period=%d ns, cycle=%d",
           freq, actual_freq, data->pwm_period, data->cycle);
    
    return 0;
}

uint16_t svpwm_get_frequency(struct svpwm *pwm)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);
    
    if (!data || !data->initialized) {
        return 0;
    }
    
    return data->freq;
}

uint16_t svpwm_get_cycle(struct svpwm *pwm)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);
    
    if (!data || !data->initialized) {
        return 0;
    }
    
    return data->cycle;
}

int svpwm_set_cycle(struct svpwm *pwm, uint16_t cycle)
{
    struct svpwm_data *data = CONTAINER_OF(pwm, struct svpwm_data, pwm);
    
    if (!data || !data->initialized) {
        return -EINVAL;
    }
    
    data->cycle = cycle;
    
    if (data->freq > 0) {
        uint32_t pwm_clock_freq = data->system_clock_freq / (data->pwm_prescaler + 1);
        
        uint32_t freq_divider = data->center_aligned ? 2 : 1;
        
        uint32_t actual_freq = pwm_clock_freq / (freq_divider * (cycle + 1));
        uint32_t actual_period = (uint32_t)((uint64_t)(freq_divider * (cycle + 1)) * 1000000000 / pwm_clock_freq);
        
        if (actual_freq < data->freq * 0.9 || actual_freq > data->freq * 1.1) {
            LOG_WRN("Actual PWM frequency (%d Hz) differs significantly from target (%d Hz)",
                   actual_freq, data->freq);
            LOG_WRN("Updating target frequency to match actual frequency");
            data->freq = actual_freq;
        }
        
        data->pwm_period = actual_period;
        
        LOG_INF("PWM cycle updated to %d, target freq=%d Hz, actual freq=%d Hz, period=%d ns",
               cycle, data->freq, actual_freq, data->pwm_period);
    } else {
        LOG_INF("PWM cycle set to %d, frequency not set yet", cycle);
    }
    
    return 0;
}
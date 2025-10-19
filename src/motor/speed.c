#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <motor/speed.h>
#include <motor/adc.h>
#include <motor/controller.h>
#include <menu/menu.h>

struct speed_data {
    struct speed speed;
    void *parent;
    uint32_t last_update_time;
    uint32_t system_clock_freq; /* 系统时钟频率 */
    struct k_timer vbus_refresh_timer;
    struct k_work vbus_refresh_work;
    struct adc_callback_t speed_change_cb;
    uint16_t *values;
    uint8_t values_count;
};

LOG_MODULE_REGISTER(speed, LOG_LEVEL_INF);

/* 电机参数配置 */
#define MOTOR_POLE_PAIRS          14     /* 电机极对数 */
#define ADC_REFERENCE_VOLTAGE     3300  /* ADC参考电压(mV) */
#define ADC_MAX_VALUE           4095   /* 12位ADC最大值 */
#define BEMF_THRESHOLD          100    /* BEMF过零检测阈值(mV) - 降低阈值以提高灵敏度 */
#define SPEED_FILTER_ALPHA       10    /* 速度滤波系数(0-100) */
#define MIN_ZERO_CROSS_INTERVAL  500   /* 最小过零点间隔(us) - 降低最小值以适应低速 */
#define MAX_ZERO_CROSS_INTERVAL  200000 /* 最大过零点间隔(us) - 增加最大值以适应更低速 */

static void bemf_detection_callback(uint16_t *values, size_t count, uint8_t id, void *param);
static void update_bemf_state(struct speed *speed, uint32_t bemf_u, uint32_t bemf_v, uint32_t bemf_w);
static void calculate_speed(struct speed *speed);
static uint32_t adc_to_mv(uint16_t adc_value);
static uint32_t filter_speed(uint32_t current_speed, uint32_t new_speed, uint32_t alpha);
static uint32_t advanced_filter_speed(struct speed *speed, uint32_t new_speed);
static void speed_value_change_cb_func(struct adc_callback_t *self, uint16_t *values, size_t count, void *param);
static int speed_get_vbus(struct menu_item_t *item, char *buf, size_t len);
static void speed_value_change_work_handler(struct k_work *work);
extern struct menu_item_t status_vbus_item;

struct menu_item_t status_vbus_item = {
    .name = "vbus",
    .id = 6,
    .style = MENU_STYLE_LABEL,
    .label_cb = speed_get_vbus,
    .visible = true,
};

static void vbus_refresh_work_handler(struct k_work *work)
{
    menu_item_refresh(&status_vbus_item);
}

static void vbus_refresh_timer_cb(struct k_timer *timer)
{
    struct speed_data *data = CONTAINER_OF(timer, struct speed_data, vbus_refresh_timer);
    k_work_submit(&data->vbus_refresh_work);
}

static void speed_ctrl_change_cb(uint16_t *values, size_t count, void *param)
{
    struct speed_data *data = (struct speed_data *)param;
    struct speed *speed = &data->speed;
    size_t val = 0;
    size_t i;

    for (i = 0; i < (count / sizeof(uint16_t)); i++)
    {
        val += values[i];
    }
    
    uint16_t avg_value = val / (count / sizeof(uint16_t));

    speed->target_speed_raw = avg_value;
    
    speed->target_rpm = (avg_value * 6000) / ADC_MAX_VALUE;
    
    LOG_DBG("Target speed - ADC:%d, RPM:%d", avg_value, speed->target_rpm);
}

#define R1  100.0f
#define R2  4.7f



static void bus_vol_change_cb(uint16_t *values, size_t size, uint8_t id, void *param)
{
    size_t i;
    uint32_t avg_value = 0;
    uint32_t voltage_mv;
    struct speed_data *data = (struct speed_data *)param;
    struct speed *speed = &data->speed;

    for (i = 0; i < (size / sizeof(uint16_t)); i++)
    {
        avg_value += values[i];
    }

    avg_value /= (size / sizeof(uint16_t));

    voltage_mv = adc_to_mv(avg_value);

    speed->bus_vol = voltage_mv * ((R1 + R2) / R2);

    LOG_DBG("bus Voltage ref raw:%d, adc volage:%d mV bus volage:%f", avg_value / (size / sizeof(uint16_t)), voltage_mv, speed->bus_vol);
}

static void bemf_detection_callback(uint16_t *values, size_t size, uint8_t id, void *param)
{
    struct speed_data *data = (struct speed_data *)param;
    struct speed *speed = &data->speed;
    uint32_t avg_value = 0;
    uint32_t voltage_mv;
    size_t i;
    
    for (i = 0; i < (size / sizeof(uint16_t)); i++) {
        avg_value += values[i];
    }
    avg_value /= (size / sizeof(uint16_t));
    
    voltage_mv = adc_to_mv(avg_value);

    switch(id) {
        case BEMF_A:
            speed->bemf_u = voltage_mv;
            break;
        case BEMF_B:
            speed->bemf_v = voltage_mv;
            break;
        case BEMF_C:
            speed->bemf_w = voltage_mv;
            break;
        default:
            break;
    }
    
    uint32_t current_time = k_cycle_get_32();
    
    update_bemf_state(speed, speed->bemf_u, speed->bemf_v, speed->bemf_w);
    calculate_speed(speed);
    
    data->last_update_time = current_time;
}

static uint32_t adc_to_mv(uint16_t adc_value)
{
    return (adc_value * ADC_REFERENCE_VOLTAGE) / ADC_MAX_VALUE;
}

static void update_bemf_state(struct speed *speed, uint32_t bemf_u, uint32_t bemf_v, uint32_t bemf_w)
{
    uint32_t neutral_voltage = 700;
    uint32_t current_time = k_cycle_get_32();
    bool zero_cross_detected = false;
    
    enum bemf_state prev_state_u = speed->bemf_state_u;
    enum bemf_state prev_state_v = speed->bemf_state_v;
    enum bemf_state prev_state_w = speed->bemf_state_w;

    if (speed->bus_vol)
         neutral_voltage = speed->bus_vol * ( 1.0f / (20.0f + 1.0f)); 
    
    LOG_DBG("BEMF values - U:%d mV, V:%d mV, W:%d mV, Neutral:%d mV, Threshold:%d mV",
            bemf_u, bemf_v, bemf_w, neutral_voltage, BEMF_THRESHOLD);
    
    if (prev_state_u == BEMF_STATE_UNKNOWN) {
        if (bemf_u > neutral_voltage) {
            speed->bemf_state_u = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_u = BEMF_STATE_FALLING;
        }
    }
    
    if (prev_state_v == BEMF_STATE_UNKNOWN) {
        if (bemf_v > neutral_voltage) {
            speed->bemf_state_v = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_v = BEMF_STATE_FALLING;
        }
    }
    
    if (prev_state_w == BEMF_STATE_UNKNOWN) {
        if (bemf_w > neutral_voltage) {
            speed->bemf_state_w = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_w = BEMF_STATE_FALLING;
        }
    }
    
    if (prev_state_u == BEMF_STATE_UNKNOWN) {
        if (bemf_u > neutral_voltage) {
            speed->bemf_state_u = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_u = BEMF_STATE_FALLING;
        }
    } else {
        if (bemf_u > speed->bemf_u) {
            speed->bemf_state_u = BEMF_STATE_RISING;
        } else if (bemf_u < speed->bemf_u) {
            speed->bemf_state_u = BEMF_STATE_FALLING;
        }
    }
    
    if (prev_state_v == BEMF_STATE_UNKNOWN) {
        if (bemf_v > neutral_voltage) {
            speed->bemf_state_v = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_v = BEMF_STATE_FALLING;
        }
    } else {
        if (bemf_v > speed->bemf_v) {
            speed->bemf_state_v = BEMF_STATE_RISING;
        } else if (bemf_v < speed->bemf_v) {
            speed->bemf_state_v = BEMF_STATE_FALLING;
        }
    }
    
    if (prev_state_w == BEMF_STATE_UNKNOWN) {
        if (bemf_w > neutral_voltage) {
            speed->bemf_state_w = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_w = BEMF_STATE_FALLING;
        }
    } else {
        if (bemf_w > speed->bemf_w) {
            speed->bemf_state_w = BEMF_STATE_RISING;
        } else if (bemf_w < speed->bemf_w) {
            speed->bemf_state_w = BEMF_STATE_FALLING;
        }
    }
    
    LOG_DBG("Phase U - Prev state: %d, Current state: %d, BEMF: %d mV",
            prev_state_u, speed->bemf_state_u, bemf_u);
    
    if ((prev_state_u == BEMF_STATE_RISING && bemf_u < neutral_voltage) ||
        (prev_state_u == BEMF_STATE_FALLING && bemf_u > neutral_voltage)) {
        if (speed->last_zero_cross_time == 0) {
            speed->last_zero_cross_time = current_time;
            LOG_INF("First zero cross detected for phase U at time %d", current_time);
        } else {
            speed->zero_cross_interval = current_time - speed->last_zero_cross_time;
            if (speed->zero_cross_interval > MIN_ZERO_CROSS_INTERVAL &&
                speed->zero_cross_interval < MAX_ZERO_CROSS_INTERVAL) {
                speed->zero_cross_timestamp = current_time;
                zero_cross_detected = true;
                
                if (speed->bemf_state_u == BEMF_STATE_RISING) {
                    speed->current_phase = 0; /* 0度 */
                } else {
                    speed->current_phase = 180; /* 180度 */
                }
                
                LOG_INF("Zero cross detected for phase U, interval: %d", speed->zero_cross_interval);
            } else {
                LOG_WRN("Zero cross interval out of range: %d", speed->zero_cross_interval);
            }
            speed->last_zero_cross_time = current_time;
        }
    }
    
    LOG_DBG("Phase V - Prev state: %d, Current state: %d, BEMF: %d mV",
            prev_state_v, speed->bemf_state_v, bemf_v);

    if ((prev_state_v == BEMF_STATE_RISING && bemf_v < neutral_voltage) ||
        (prev_state_v == BEMF_STATE_FALLING && bemf_v > neutral_voltage)) {
        if (speed->last_zero_cross_time == 0) {
            speed->last_zero_cross_time = current_time;
            LOG_INF("First zero cross detected for phase V at time %d", current_time);
        } else {
            speed->zero_cross_interval = current_time - speed->last_zero_cross_time;
            if (speed->zero_cross_interval > MIN_ZERO_CROSS_INTERVAL &&
                speed->zero_cross_interval < MAX_ZERO_CROSS_INTERVAL) {
                speed->zero_cross_timestamp = current_time;
                zero_cross_detected = true;
                
                if (speed->bemf_state_v == BEMF_STATE_RISING) {
                    speed->current_phase = 120; /* 120度 */
                } else {
                    speed->current_phase = 300; /* 300度 */
                }
                
                LOG_INF("Zero cross detected for phase V, interval: %d", speed->zero_cross_interval);
            } else {
                LOG_WRN("Zero cross interval out of range: %d", speed->zero_cross_interval);
            }
            speed->last_zero_cross_time = current_time;
        }
    }
    
    LOG_DBG("Phase W - Prev state: %d, Current state: %d, BEMF: %d mV",
            prev_state_w, speed->bemf_state_w, bemf_w);

    if ((prev_state_w == BEMF_STATE_RISING && bemf_w < neutral_voltage) ||
        (prev_state_w == BEMF_STATE_FALLING && bemf_w > neutral_voltage)) {
        if (speed->last_zero_cross_time == 0) {
            speed->last_zero_cross_time = current_time;
            LOG_INF("First zero cross detected for phase W at time %d", current_time);
        } else {
            speed->zero_cross_interval = current_time - speed->last_zero_cross_time;
            if (speed->zero_cross_interval > MIN_ZERO_CROSS_INTERVAL &&
                speed->zero_cross_interval < MAX_ZERO_CROSS_INTERVAL) {
                speed->zero_cross_timestamp = current_time;
                zero_cross_detected = true;
                
                if (speed->bemf_state_w == BEMF_STATE_RISING) {
                    speed->current_phase = 240; /* 240度 */
                } else {
                    speed->current_phase = 60; /* 60度 */
                }
                
                LOG_INF("Zero cross detected for phase W, interval: %d", speed->zero_cross_interval);
            } else {
                LOG_WRN("Zero cross interval out of range: %d", speed->zero_cross_interval);
            }
            speed->last_zero_cross_time = current_time;
        }
    }
    
    if (zero_cross_detected) {
        if (speed->bemf_state_u == BEMF_STATE_RISING &&
            speed->bemf_state_v == BEMF_STATE_FALLING &&
            speed->bemf_state_w == BEMF_STATE_FALLING) {
            speed->dir = 1; /* 正向 */
        } else if (speed->bemf_state_u == BEMF_STATE_FALLING &&
                   speed->bemf_state_v == BEMF_STATE_RISING &&
                   speed->bemf_state_w == BEMF_STATE_RISING) {
            speed->dir = 0; /* 反向 */
        }
        
        LOG_DBG("Zero cross detected, phase: %d, direction: %s",
                speed->current_phase, speed->dir ? "Forward" : "Reverse");
    }
}

static void calculate_speed(struct speed *speed)
{
    if (speed->zero_cross_interval == 0) {
        speed->speed_valid = false;
        LOG_DBG("Zero cross interval is 0, cannot calculate speed");
        return;
    }

    struct speed_data *data = CONTAINER_OF(speed, struct speed_data, speed);
    uint32_t sys_clock_freq = data->system_clock_freq;
    uint32_t period_us = (speed->zero_cross_interval * 1000000) / sys_clock_freq;
    
    LOG_DBG("Zero cross interval: %d, calculated period: %d us", speed->zero_cross_interval, period_us);
    
    if (period_us > 0) {
        speed->electrical_freq = 1000000 / (2 * period_us);
        
        uint32_t new_rpm = (speed->electrical_freq * 60) / speed->pole_pairs;
        
        LOG_DBG("Calculated electrical frequency: %d Hz, mechanical RPM: %d", speed->electrical_freq, new_rpm);
        
        if (new_rpm > 30000) { 
            LOG_WRN("Calculated RPM %d seems too high, ignoring", new_rpm);
            speed->speed_valid = false;
            return;
        }
        
        speed->rpm = new_rpm;
        
        if (speed->filtered_speed == 0) {
            speed->filtered_speed = new_rpm;
        } else {
            speed->filtered_speed = advanced_filter_speed(speed, new_rpm);
        }
        
        speed->speed_valid = true;
        
        LOG_DBG("Speed: %d RPM (filtered: %d), Freq: %d Hz, Direction: %s",
                speed->rpm, speed->filtered_speed, speed->electrical_freq,
                speed->dir ? "Forward" : "Reverse");

        static uint32_t last_info_time = 0;
        if (k_cycle_get_32() - last_info_time > sys_clock_freq) { /* 每秒输出一次 */
            LOG_INF("Motor Status - RPM: %d, Freq: %d Hz, Phase: %d, Dir: %s",
                    speed->filtered_speed, speed->electrical_freq,
                    speed->current_phase, speed->dir ? "Forward" : "Reverse");
            last_info_time = k_cycle_get_32();
        }
    } else {
        speed->speed_valid = false;
        LOG_DBG("Invalid period for speed calculation: %d us", period_us);
    }
}

static uint32_t filter_speed(uint32_t current_speed, uint32_t new_speed, uint32_t alpha)
{
    return (alpha * new_speed + (100 - alpha) * current_speed) / 100;
}

struct speed *speed_init(struct motor_adc *adc, void *parent)
{
    struct speed_data *data = k_malloc(sizeof(*data));
    
    if (!data) {
        LOG_ERR("Failed to allocate memory for speed data");
        return NULL;
    }

    memset(data, 0, sizeof(*data));
    data->parent = parent;

    if (parent) {
        struct motor_ctrl *ctrl = (struct motor_ctrl *)parent;
        data->system_clock_freq = ctrl->system_clock_freq;
    } else {
        data->system_clock_freq = 1000000; /* 默认值，如果parent为空 */
    }
    
    struct speed *speed = &data->speed;
    speed->dir = 0; /* 默认方向 */
    speed->rpm = 0;
    speed->electrical_freq = 0;
    speed->pole_pairs = MOTOR_POLE_PAIRS;
    speed->bemf_u = 0;
    speed->bemf_v = 0;
    speed->bemf_w = 0;
    speed->zero_cross_timestamp = 0;
    speed->last_zero_cross_time = 0;
    speed->zero_cross_interval = 0;
    speed->bemf_state_u = BEMF_STATE_UNKNOWN;
    speed->bemf_state_v = BEMF_STATE_UNKNOWN;
    speed->bemf_state_w = BEMF_STATE_UNKNOWN;
    speed->current_phase = 0;
    speed->speed_valid = false;
    speed->filter_alpha = SPEED_FILTER_ALPHA;
    speed->filtered_speed = 0;
    speed->target_speed_raw = 0;
    speed->target_rpm = 0;

    data->speed_change_cb.func = speed_value_change_cb_func;
    data->speed_change_cb.id = SPEED_CTRL;
    data->speed_change_cb.param = data;

    k_work_init(&data->speed_change_cb.work, speed_value_change_work_handler);
    
    adc_register_callback(adc, &data->speed_change_cb);

    // /* BEMF检测回调 */
    // adc_register_callback(adc, bemf_detection_callback, BEMF_A, data);
    // adc_register_callback(adc, bemf_detection_callback, BEMF_B, data);
    // adc_register_callback(adc, bemf_detection_callback, BEMF_C, data);

    // /* 总线电压回调 */
    // adc_register_callback(adc, bus_vol_change_cb, VOLAGE_BUS, data);

    status_vbus_item.priv_data = speed;

    k_work_init(&data->vbus_refresh_work, vbus_refresh_work_handler);
    k_timer_init(&data->vbus_refresh_timer, vbus_refresh_timer_cb, NULL);
    k_timer_start(&data->vbus_refresh_timer, K_MSEC(1000), K_MSEC(1000));

    LOG_INF("Speed detection initialized with %d pole pairs", MOTOR_POLE_PAIRS);

    return speed;
}

uint32_t speed_get_rpm(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->filtered_speed;
}

uint32_t speed_get_frequency(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->electrical_freq;
}

uint16_t speed_get_direction(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->dir;
}

bool speed_is_valid(struct speed *speed)
{
    if (!speed) {
        return false;
    }
    return speed->speed_valid;
}

void speed_get_bemf(struct speed *speed, uint32_t *bemf_u, uint32_t *bemf_v, uint32_t *bemf_w)
{
    if (!speed) {
        if (bemf_u) *bemf_u = 0;
        if (bemf_v) *bemf_v = 0;
        if (bemf_w) *bemf_w = 0;
        return;
    }
    
    if (bemf_u) *bemf_u = speed->bemf_u;
    if (bemf_v) *bemf_v = speed->bemf_v;
    if (bemf_w) *bemf_w = speed->bemf_w;
}

void speed_set_pole_pairs(struct speed *speed, uint32_t pole_pairs)
{
    if (!speed || pole_pairs == 0) {
        return;
    }
    speed->pole_pairs = pole_pairs;
    LOG_INF("Motor pole pairs set to %d", pole_pairs);
}

void speed_set_filter_alpha(struct speed *speed, uint32_t alpha)
{
    if (!speed || alpha > 100) {
        return;
    }
    speed->filter_alpha = alpha;
    LOG_INF("Speed filter alpha set to %d", alpha);
}

void speed_reset(struct speed *speed)
{
    if (!speed) {
        return;
    }
    
    speed->rpm = 0;
    speed->electrical_freq = 0;
    speed->zero_cross_timestamp = 0;
    speed->last_zero_cross_time = 0;
    speed->zero_cross_interval = 0;
    speed->bemf_state_u = BEMF_STATE_UNKNOWN;
    speed->bemf_state_v = BEMF_STATE_UNKNOWN;
    speed->bemf_state_w = BEMF_STATE_UNKNOWN;
    speed->current_phase = 0;
    speed->speed_valid = false;
    speed->filtered_speed = 0;
    speed->target_speed_raw = 0;
    speed->target_rpm = 0;
    
    LOG_INF("Speed detection reset");
}

static uint32_t advanced_filter_speed(struct speed *speed, uint32_t new_speed)
{
    uint32_t speed_diff = 0;
    if (speed->filtered_speed > new_speed) {
        speed_diff = speed->filtered_speed - new_speed;
    } else {
        speed_diff = new_speed - speed->filtered_speed;
    }

    uint32_t adaptive_alpha = speed->filter_alpha;
    if (speed_diff > 1000) { /* 速度变化超过1000 RPM */
        adaptive_alpha = speed->filter_alpha / 2; /* 降低滤波系数 */
        LOG_WRN("Large speed change detected: %d RPM, reducing filter alpha to %d",
                speed_diff, adaptive_alpha);
    }
    
    return filter_speed(speed->filtered_speed, new_speed, adaptive_alpha);
}

int speed_control_feedback(struct speed *speed, uint32_t target_rpm)
{
    if (!speed || !speed->speed_valid) {
        return -EINVAL;
    }

    int32_t error = target_rpm - speed->filtered_speed;

    int32_t adjustment = error / 10; /* 简单的比例系数 */

    if (adjustment > 100) adjustment = 100;
    if (adjustment < -100) adjustment = -100;
    
    LOG_DBG("Speed control: Target=%d RPM, Current=%d RPM, Error=%d, Adjustment=%d",
            target_rpm, speed->filtered_speed, error, adjustment);

    return adjustment;
}

void speed_get_control_status(struct speed *speed, uint32_t *current_rpm,
                             uint32_t *frequency, uint16_t *direction, bool *valid)
{
    if (!speed) {
        if (current_rpm) *current_rpm = 0;
        if (frequency) *frequency = 0;
        if (direction) *direction = 0;
        if (valid) *valid = false;
        return;
    }
    
    if (current_rpm) *current_rpm = speed->filtered_speed;
    if (frequency) *frequency = speed->electrical_freq;
    if (direction) *direction = speed->dir;
    if (valid) *valid = speed->speed_valid;
}

uint8_t speed_get_current_phase(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->current_phase;
}

uint32_t speed_get_zero_cross_interval(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->zero_cross_interval;
}

void speed_set_bemf_threshold(struct speed *speed, uint32_t threshold)
{
    if (!speed) {
        return;
    }
    LOG_INF("BEMF threshold set to %d mV", threshold);
}

void speed_get_statistics(struct speed *speed, uint32_t *raw_rpm, uint32_t *filtered_rpm,
                         uint32_t *bemf_u, uint32_t *bemf_v, uint32_t *bemf_w)
{
    if (!speed) {
        if (raw_rpm) *raw_rpm = 0;
        if (filtered_rpm) *filtered_rpm = 0;
        if (bemf_u) *bemf_u = 0;
        if (bemf_v) *bemf_v = 0;
        if (bemf_w) *bemf_w = 0;
        return;
    }
    
    if (raw_rpm) *raw_rpm = speed->rpm;
    if (filtered_rpm) *filtered_rpm = speed->filtered_speed;
    if (bemf_u) *bemf_u = speed->bemf_u;
    if (bemf_v) *bemf_v = speed->bemf_v;
    if (bemf_w) *bemf_w = speed->bemf_w;
}

static int speed_get_vbus(struct menu_item_t *item, char *buf, size_t len)
{
    struct speed *speed = item->priv_data;
    uint32_t integer_part = (uint32_t)speed->bus_vol;
    uint32_t fractional_part = (uint32_t)((speed->bus_vol - integer_part) * 100);

    snprintf(buf, len, ":%d.%02d", integer_part, fractional_part);

    return 0;
}

static void speed_value_change_cb_func(struct adc_callback_t *self, uint16_t *values, size_t count, void *param)
{
    struct speed_data *data = param;
    data->values = values;
    data->values_count = count;

    k_work_submit(&self->work);
}

static void speed_value_change_work_handler(struct k_work *work)
{
    struct adc_callback_t *cb = CONTAINER_OF(work, struct adc_callback_t, work);
    struct speed_data *data = cb->param;

    speed_ctrl_change_cb(data->values, data->values_count, data);
}
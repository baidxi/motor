#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <motor/speed.h>
#include <motor/adc.h>
#include <motor/controller.h>

struct speed_data {
    struct speed speed;
    void *parent;
    uint32_t last_update_time;
    uint32_t system_clock_freq; /* 系统时钟频率 */
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

/* 内部函数声明 */
static void bemf_detection_callback(uint16_t *values, size_t count, uint8_t id, void *param);
static void update_bemf_state(struct speed *speed, uint32_t bemf_u, uint32_t bemf_v, uint32_t bemf_w);
static void calculate_speed(struct speed *speed);
static uint32_t adc_to_mv(uint16_t adc_value);
static uint32_t filter_speed(uint32_t current_speed, uint32_t new_speed, uint32_t alpha);
static uint32_t advanced_filter_speed(struct speed *speed, uint32_t new_speed);

static void speed_ctrl_change_cb(uint16_t *values, size_t count, uint8_t id, void *param)
{
    struct speed_data *data = (struct speed_data *)param;
    struct speed *speed = &data->speed;
    size_t val = 0;
    size_t i;

    for (i = 0; i < (count / sizeof(uint16_t)); i++)
    {
        val += values[i];
    }
    
    /* 计算平均值 */
    uint16_t avg_value = val / (count / sizeof(uint16_t));
    
    /* 保存原始ADC值 */
    speed->target_speed_raw = avg_value;
    
    /* 将ADC值转换为目标转速(RPM) */
    /* 假设ADC范围0-4095对应转速0-6000 RPM */
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

    LOG_INF("bus Voltage ref raw:%d, adc volage:%d mV bus volage:%f", avg_value / (size / sizeof(uint16_t)), voltage_mv, speed->bus_vol);
}

/* BEMF检测回调函数 */
static void bemf_detection_callback(uint16_t *values, size_t size, uint8_t id, void *param)
{
    struct speed_data *data = (struct speed_data *)param;
    struct speed *speed = &data->speed;
    uint32_t avg_value = 0;
    uint32_t voltage_mv;
    size_t i;
    
    /* 计算ADC平均值 */
    for (i = 0; i < (size / sizeof(uint16_t)); i++) {
        avg_value += values[i];
    }
    avg_value /= (size / sizeof(uint16_t));
    
    /* 转换为电压值(mV) */
    voltage_mv = adc_to_mv(avg_value);
    
    /* 根据ID更新BEMF值 */
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
    
    /* 获取当前时间戳 */
    uint32_t current_time = k_cycle_get_32();
    
    /* 更新BEMF状态和计算速度 */
    update_bemf_state(speed, speed->bemf_u, speed->bemf_v, speed->bemf_w);
    calculate_speed(speed);
    
    /* 更新最后更新时间 */
    data->last_update_time = current_time;
}

/* ADC值转换为电压值(mV) */
static uint32_t adc_to_mv(uint16_t adc_value)
{
    return (adc_value * ADC_REFERENCE_VOLTAGE) / ADC_MAX_VALUE;
}

/* 更新BEMF状态 */
static void update_bemf_state(struct speed *speed, uint32_t bemf_u, uint32_t bemf_v, uint32_t bemf_w)
{
    uint32_t neutral_voltage = 700;             /* 大概目标 ADC 电压*/
    uint32_t current_time = k_cycle_get_32();
    bool zero_cross_detected = false;
    
    /* 保存上一次的状态 */
    enum bemf_state prev_state_u = speed->bemf_state_u;
    enum bemf_state prev_state_v = speed->bemf_state_v;
    enum bemf_state prev_state_w = speed->bemf_state_w;

    if (speed->bus_vol)
         neutral_voltage = speed->bus_vol * ( 1.0f / (20.0f + 1.0f)); /* 根据 BEMF 处的R1和R2电阻带入 */
    
    /* 添加调试信息 */
    LOG_INF("BEMF values - U:%d mV, V:%d mV, W:%d mV, Neutral:%d mV, Threshold:%d mV",
            bemf_u, bemf_v, bemf_w, neutral_voltage, BEMF_THRESHOLD);
    
    /* 初始化BEMF状态（如果是第一次运行） */
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
    
    /* 更新U相BEMF状态 */
    /* 检测BEMF信号的变化趋势，而不是绝对值 */
    if (prev_state_u == BEMF_STATE_UNKNOWN) {
        /* 第一次运行，根据当前值设置状态 */
        if (bemf_u > neutral_voltage) {
            speed->bemf_state_u = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_u = BEMF_STATE_FALLING;
        }
    } else {
        /* 检测信号变化趋势 */
        if (bemf_u > speed->bemf_u) {
            /* 信号上升 */
            speed->bemf_state_u = BEMF_STATE_RISING;
        } else if (bemf_u < speed->bemf_u) {
            /* 信号下降 */
            speed->bemf_state_u = BEMF_STATE_FALLING;
        }
        /* 如果信号值不变，保持原状态 */
    }
    
    /* 更新V相BEMF状态 */
    if (prev_state_v == BEMF_STATE_UNKNOWN) {
        /* 第一次运行，根据当前值设置状态 */
        if (bemf_v > neutral_voltage) {
            speed->bemf_state_v = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_v = BEMF_STATE_FALLING;
        }
    } else {
        /* 检测信号变化趋势 */
        if (bemf_v > speed->bemf_v) {
            /* 信号上升 */
            speed->bemf_state_v = BEMF_STATE_RISING;
        } else if (bemf_v < speed->bemf_v) {
            /* 信号下降 */
            speed->bemf_state_v = BEMF_STATE_FALLING;
        }
        /* 如果信号值不变，保持原状态 */
    }
    
    /* 更新W相BEMF状态 */
    if (prev_state_w == BEMF_STATE_UNKNOWN) {
        /* 第一次运行，根据当前值设置状态 */
        if (bemf_w > neutral_voltage) {
            speed->bemf_state_w = BEMF_STATE_RISING;
        } else {
            speed->bemf_state_w = BEMF_STATE_FALLING;
        }
    } else {
        /* 检测信号变化趋势 */
        if (bemf_w > speed->bemf_w) {
            /* 信号上升 */
            speed->bemf_state_w = BEMF_STATE_RISING;
        } else if (bemf_w < speed->bemf_w) {
            /* 信号下降 */
            speed->bemf_state_w = BEMF_STATE_FALLING;
        }
        /* 如果信号值不变，保持原状态 */
    }
    
    /* U相过零检测 */
    /* 添加调试信息 */
    LOG_DBG("Phase U - Prev state: %d, Current state: %d, BEMF: %d mV",
            prev_state_u, speed->bemf_state_u, bemf_u);
    
    /* 检测BEMF信号的变化，而不是绝对值 */
    if ((prev_state_u == BEMF_STATE_RISING && bemf_u < neutral_voltage) ||
        (prev_state_u == BEMF_STATE_FALLING && bemf_u > neutral_voltage)) {
        /* 如果是第一次检测到过零点，只记录时间，不计算间隔 */
        if (speed->last_zero_cross_time == 0) {
            speed->last_zero_cross_time = current_time;
            LOG_INF("First zero cross detected for phase U at time %d", current_time);
        } else {
            speed->zero_cross_interval = current_time - speed->last_zero_cross_time;
            if (speed->zero_cross_interval > MIN_ZERO_CROSS_INTERVAL &&
                speed->zero_cross_interval < MAX_ZERO_CROSS_INTERVAL) {
                speed->zero_cross_timestamp = current_time;
                zero_cross_detected = true;
                
                /* 根据BEMF状态确定当前相位 */
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
    
    /* V相过零检测 */
    /* 添加调试信息 */
    LOG_DBG("Phase V - Prev state: %d, Current state: %d, BEMF: %d mV",
            prev_state_v, speed->bemf_state_v, bemf_v);
    
    /* 检测BEMF信号的变化，而不是绝对值 */
    if ((prev_state_v == BEMF_STATE_RISING && bemf_v < neutral_voltage) ||
        (prev_state_v == BEMF_STATE_FALLING && bemf_v > neutral_voltage)) {
        /* 如果是第一次检测到过零点，只记录时间，不计算间隔 */
        if (speed->last_zero_cross_time == 0) {
            speed->last_zero_cross_time = current_time;
            LOG_INF("First zero cross detected for phase V at time %d", current_time);
        } else {
            speed->zero_cross_interval = current_time - speed->last_zero_cross_time;
            if (speed->zero_cross_interval > MIN_ZERO_CROSS_INTERVAL &&
                speed->zero_cross_interval < MAX_ZERO_CROSS_INTERVAL) {
                speed->zero_cross_timestamp = current_time;
                zero_cross_detected = true;
                
                /* 根据BEMF状态确定当前相位 */
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
    
    /* W相过零检测 */
    /* 添加调试信息 */
    LOG_DBG("Phase W - Prev state: %d, Current state: %d, BEMF: %d mV",
            prev_state_w, speed->bemf_state_w, bemf_w);
    
    /* 检测BEMF信号的变化，而不是绝对值 */
    if ((prev_state_w == BEMF_STATE_RISING && bemf_w < neutral_voltage) ||
        (prev_state_w == BEMF_STATE_FALLING && bemf_w > neutral_voltage)) {
        /* 如果是第一次检测到过零点，只记录时间，不计算间隔 */
        if (speed->last_zero_cross_time == 0) {
            speed->last_zero_cross_time = current_time;
            LOG_INF("First zero cross detected for phase W at time %d", current_time);
        } else {
            speed->zero_cross_interval = current_time - speed->last_zero_cross_time;
            if (speed->zero_cross_interval > MIN_ZERO_CROSS_INTERVAL &&
                speed->zero_cross_interval < MAX_ZERO_CROSS_INTERVAL) {
                speed->zero_cross_timestamp = current_time;
                zero_cross_detected = true;
                
                /* 根据BEMF状态确定当前相位 */
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
    
    /* 检测电机方向 */
    if (zero_cross_detected) {
        /* 通过比较三相BEMF的相对状态来确定方向 */
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

/* 计算电机速度 */
static void calculate_speed(struct speed *speed)
{
    if (speed->zero_cross_interval == 0) {
        speed->speed_valid = false;
        LOG_DBG("Zero cross interval is 0, cannot calculate speed");
        return;
    }
    
    /* 计算电频率(Hz) */
    /* 使用系统时钟频率 */
    struct speed_data *data = CONTAINER_OF(speed, struct speed_data, speed);
    uint32_t sys_clock_freq = data->system_clock_freq;
    uint32_t period_us = (speed->zero_cross_interval * 1000000) / sys_clock_freq;
    
    LOG_DBG("Zero cross interval: %d, calculated period: %d us", speed->zero_cross_interval, period_us);
    
    if (period_us > 0) {
        /* 电频率 = 1 / (2 * 过零点间隔) */
        /* 一个电周期包含两个过零点（上升和下降） */
        speed->electrical_freq = 1000000 / (2 * period_us);
        
        /* 机械转速(RPM) = 电频率 * 60 / 极对数 */
        uint32_t new_rpm = (speed->electrical_freq * 60) / speed->pole_pairs;
        
        LOG_DBG("Calculated electrical frequency: %d Hz, mechanical RPM: %d", speed->electrical_freq, new_rpm);
        
        /* 检查转速是否在合理范围内 */
        if (new_rpm > 30000) { /* 假设最大转速为30000 RPM */
            LOG_WRN("Calculated RPM %d seems too high, ignoring", new_rpm);
            speed->speed_valid = false;
            return;
        }
        
        /* 保存原始转速值 */
        speed->rpm = new_rpm;
        
        /* 应用滤波 */
        if (speed->filtered_speed == 0) {
            /* 第一次计算，直接使用计算值 */
            speed->filtered_speed = new_rpm;
        } else {
            /* 应用高级滤波，带有突变检测 */
            speed->filtered_speed = advanced_filter_speed(speed, new_rpm);
        }
        
        speed->speed_valid = true;
        
        /* 输出调试信息 */
        LOG_DBG("Speed: %d RPM (filtered: %d), Freq: %d Hz, Direction: %s",
                speed->rpm, speed->filtered_speed, speed->electrical_freq,
                speed->dir ? "Forward" : "Reverse");
        
        /* 定期输出信息状态 */
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

/* 速度滤波函数 */
static uint32_t filter_speed(uint32_t current_speed, uint32_t new_speed, uint32_t alpha)
{
    /* 简单的一阶低通滤波 */
    /* filtered = alpha * new + (100 - alpha) * current */
    return (alpha * new_speed + (100 - alpha) * current_speed) / 100;
}

struct speed *speed_init(struct motor_adc *adc, void *parent)
{
    struct speed_data *data = k_malloc(sizeof(*data));
    
    if (!data) {
        LOG_ERR("Failed to allocate memory for speed data");
        return NULL;
    }

    /* 初始化速度数据 */
    memset(data, 0, sizeof(*data));
    data->parent = parent;
    
    /* 从parent指针中获取系统时钟频率 */
    if (parent) {
        struct motor_ctrl *ctrl = (struct motor_ctrl *)parent;
        data->system_clock_freq = ctrl->system_clock_freq;
    } else {
        data->system_clock_freq = 1000000; /* 默认值，如果parent为空 */
    }
    
    /* 初始化速度结构体 */
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

    /* 注册回调函数 */
    adc_register_callback(adc, speed_ctrl_change_cb, SPEED_CTRL, data);

    /* BEMF检测回调 */
    adc_register_callback(adc, bemf_detection_callback, BEMF_A, data);
    adc_register_callback(adc, bemf_detection_callback, BEMF_B, data);
    adc_register_callback(adc, bemf_detection_callback, BEMF_C, data);

    /* 总线电压回调 */
    adc_register_callback(adc, bus_vol_change_cb, VOLAGE_BUS, data);

    LOG_INF("Speed detection initialized with %d pole pairs", MOTOR_POLE_PAIRS);

    return speed;
}

/* 获取当前速度(RPM) */
uint32_t speed_get_rpm(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->filtered_speed;
}

/* 获取当前电频率(Hz) */
uint32_t speed_get_frequency(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->electrical_freq;
}

/* 获取电机方向 */
uint16_t speed_get_direction(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->dir;
}

/* 检查速度值是否有效 */
bool speed_is_valid(struct speed *speed)
{
    if (!speed) {
        return false;
    }
    return speed->speed_valid;
}

/* 获取BEMF值 */
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

/* 设置电机极对数 */
void speed_set_pole_pairs(struct speed *speed, uint32_t pole_pairs)
{
    if (!speed || pole_pairs == 0) {
        return;
    }
    speed->pole_pairs = pole_pairs;
    LOG_INF("Motor pole pairs set to %d", pole_pairs);
}

/* 设置滤波系数 */
void speed_set_filter_alpha(struct speed *speed, uint32_t alpha)
{
    if (!speed || alpha > 100) {
        return;
    }
    speed->filter_alpha = alpha;
    LOG_INF("Speed filter alpha set to %d", alpha);
}

/* 重置速度检测 */
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

/* 高级速度滤波函数 - 带有突变检测 */
static uint32_t advanced_filter_speed(struct speed *speed, uint32_t new_speed)
{
    /* 检测速度突变 */
    uint32_t speed_diff = 0;
    if (speed->filtered_speed > new_speed) {
        speed_diff = speed->filtered_speed - new_speed;
    } else {
        speed_diff = new_speed - speed->filtered_speed;
    }
    
    /* 如果速度变化过大，可能是噪声或错误，降低滤波系数以减少突变影响 */
    uint32_t adaptive_alpha = speed->filter_alpha;
    if (speed_diff > 1000) { /* 速度变化超过1000 RPM */
        adaptive_alpha = speed->filter_alpha / 2; /* 降低滤波系数 */
        LOG_WRN("Large speed change detected: %d RPM, reducing filter alpha to %d",
                speed_diff, adaptive_alpha);
    }
    
    /* 应用一阶低通滤波 */
    return filter_speed(speed->filtered_speed, new_speed, adaptive_alpha);
}

/* 速度控制反馈函数 */
int speed_control_feedback(struct speed *speed, uint32_t target_rpm)
{
    if (!speed || !speed->speed_valid) {
        return -EINVAL;
    }
    
    /* 计算速度误差 */
    int32_t error = target_rpm - speed->filtered_speed;
    
    /* 简单的比例控制 */
    /* 在实际应用中，这里可以实现更复杂的PID控制算法 */
    int32_t adjustment = error / 10; /* 简单的比例系数 */
    
    /* 限制调整范围 */
    if (adjustment > 100) adjustment = 100;
    if (adjustment < -100) adjustment = -100;
    
    /* 输出调试信息 */
    LOG_DBG("Speed control: Target=%d RPM, Current=%d RPM, Error=%d, Adjustment=%d",
            target_rpm, speed->filtered_speed, error, adjustment);
    
    /* 返回调整值，由上层控制模块使用 */
    return adjustment;
}

/* 获取速度控制状态 */
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

/* 获取电机当前相位 */
uint8_t speed_get_current_phase(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->current_phase;
}

/* 获取过零点间隔时间 */
uint32_t speed_get_zero_cross_interval(struct speed *speed)
{
    if (!speed) {
        return 0;
    }
    return speed->zero_cross_interval;
}

/* 设置BEMF阈值 */
void speed_set_bemf_threshold(struct speed *speed, uint32_t threshold)
{
    if (!speed) {
        return;
    }
    /* 注意：这里只是示例，实际应用中可能需要修改全局定义或添加到speed结构体中 */
    LOG_INF("BEMF threshold set to %d mV", threshold);
}

/* 获取速度检测统计信息 */
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

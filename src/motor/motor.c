#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>

#include <motor/motor.h>
#include <motor/speed.h>
#include <motor/svpwm.h>
#include <motor/controller.h>

LOG_MODULE_REGISTER(motor, LOG_LEVEL_INF);

#define MOTOR_THREAD_STACK_SIZE 2048

struct motor_data {
    struct motor motor;
    uint8_t type;
    uint8_t state;
    uint8_t mode;
    struct speed *speed;
    void *parent;  /* 指向父结构体，即motor_ctrl */
    struct k_thread thread;
    k_tid_t tid;
    K_KERNEL_STACK_MEMBER(thread_stack, MOTOR_THREAD_STACK_SIZE);
    struct svpwm *svpwm;
};

static void motor_state_update(struct motor *motor, uint8_t state)
{
    struct motor_data *data = CONTAINER_OF(motor, struct motor_data, motor);
    uint8_t old_state = data->state;
    
    /* 验证状态转换是否合法 */
    switch (old_state) {
        case MOTOR_STATE_IDENTIFY:
            /* 识别状态可以转换到任何状态 */
            break;
            
        case MOTOR_STATE_STOP:
            /* 停止状态可以转换到运行状态或故障状态 */
            if (state != MOTOR_STATE_RUN && state != MOTOR_STATE_FAULT) {
                LOG_WRN("Invalid state transition from STOP to %d", state);
                return;
            }
            break;
            
        case MOTOR_STATE_RUN:
            /* 运行状态可以转换到停止状态或故障状态 */
            if (state != MOTOR_STATE_STOP && state != MOTOR_STATE_FAULT) {
                LOG_WRN("Invalid state transition from RUN to %d", state);
                return;
            }
            break;
            
        case MOTOR_STATE_FAULT:
            /* 故障状态只能转换到停止状态（复位） */
            if (state != MOTOR_STATE_STOP) {
                LOG_WRN("Invalid state transition from FAULT to %d", state);
                return;
            }
            break;
            
        default:
            LOG_WRN("Unknown current state: %d", old_state);
            return;
    }
    
    /* 状态转换前的处理 */
    switch (old_state) {
        case MOTOR_STATE_RUN:
            /* 从运行状态退出时，停止PWM输出 */
            if (state != MOTOR_STATE_RUN) {
                svpwm_update_output(data->svpwm, 0.0f, 0.0f);
                LOG_INF("Stopping PWM output");
            }
            break;
            
        case MOTOR_STATE_FAULT:
            /* 从故障状态退出时，复位相关参数 */
            if (state == MOTOR_STATE_STOP) {
                speed_reset(data->speed);
                LOG_INF("Motor fault cleared, parameters reset");
            }
            break;
            
        default:
            break;
    }
    
    /* 更新状态 */
    data->state = state;
    
    /* 状态转换后的处理 */
    switch (state) {
        case MOTOR_STATE_IDENTIFY:
            LOG_INF("Motor entering IDENTIFY state");
            break;
            
        case MOTOR_STATE_STOP:
            LOG_INF("Motor entering STOP state");
            break;
            
        case MOTOR_STATE_RUN:
            LOG_INF("Motor entering RUN state");
            break;
            
        case MOTOR_STATE_FAULT:
            LOG_ERR("Motor entering FAULT state");
            break;
            
        default:
            LOG_WRN("Motor entering unknown state: %d", state);
            break;
    }
    
    LOG_INF("Motor state changed from %d to %d", old_state, state);
}

static const struct motor_ops ops = {
    .update_state = motor_state_update,
};

static void motor_thread_entry(void *v1, void *v2, void *v3)
{
    struct motor_data *data = (struct motor_data *)v1;
    uint32_t target_rpm = 0;    /* 目标转速 */
    uint32_t current_rpm = 0;  /* 当前转速 */
    int speed_adjustment = 0;   /* 速度调整值 */
    float alpha = 0.0f;        /* SVPWM alpha分量 */
    float beta = 0.0f;         /* SVPWM beta分量 */
    uint32_t last_update_time = 0;
    uint32_t control_period_ms = 10; /* 控制周期10ms */
    
    LOG_INF("Motor thread started, type: %s",
            data->type == MOTOR_TYPE_BLDC ? "BLDC" : "FOC");
    
    while (1) {
        uint32_t current_time = k_cycle_get_32();
        
        /* 检查是否到达控制周期 */
        if (current_time - last_update_time >= control_period_ms * 1000) {
            last_update_time = current_time;
            
            /* 获取当前速度 */
            current_rpm = data->speed->filtered_speed;
            
            /* 根据电机状态执行不同的控制逻辑 */
            switch (data->state) {
                case MOTOR_STATE_IDENTIFY:
                    /* 电机识别状态 */
                    LOG_INF("Motor identification state");
                    
                    /* 检查速度是否有效 */
                    if (speed_is_valid(data->speed)) {
                        /* 速度有效，切换到停止状态 */
                        data->motor.ops->update_state(&data->motor, MOTOR_STATE_STOP);
                        
                        /* 发送电机就绪事件 */
                        if (data->parent) {
                            ctrl_event_post((struct motor_ctrl *)data->parent, MOTOR_EVENT_READY);
                            LOG_INF("Motor ready event posted");
                        }
                    } else {
                        /* 速度无效，保持识别状态 */
                        /* 旋转电机以产生BEMF信号 */
                        static uint32_t last_rotate_time = 0;
                        uint32_t current_time = k_cycle_get_32();
                        
                        /* 每5秒执行一次旋转 */
                        if (current_time - last_rotate_time >= 5 * 1000 * 1000) { /* 5秒 */
                            LOG_INF("Performing motor rotation for identification");
                            motor_rotate(&data->motor, 5.0f); /* 旋转5度 */
                            last_rotate_time = current_time;
                        } else {
                            /* 在旋转间隔期间，停止PWM输出 */
                            alpha = 0.0f;
                            beta = 0.0f;
                            svpwm_update_output(data->svpwm, alpha, beta);
                        }
                    }
                    break;
                    
                case MOTOR_STATE_STOP:
                    /* 电机停止状态 */
                    LOG_DBG("Motor stop state");
                    
                    /* 设置目标转速为0 */
                    target_rpm = 0;
                    
                    /* 执行速度控制 */
                    speed_adjustment = speed_control_feedback(data->speed, target_rpm);
                    
                    /* 如果速度接近0，可以切换到运行状态 */
                    if (current_rpm < 100) { /* 小于100RPM认为接近停止 */
                        /* 这里可以添加一些条件判断，比如外部命令，来切换到运行状态 */
                        /* 暂时保持停止状态 */
                        alpha = 0.0f;
                        beta = 0.0f;
                        svpwm_update_output(data->svpwm, alpha, beta);
                    }
                    break;
                    
                case MOTOR_STATE_RUN:
                    /* 电机运行状态 */
                    LOG_DBG("Motor run state, current RPM: %d", current_rpm);
                    
                    /* 从speed结构中获取目标转速 */
                    target_rpm = data->speed->target_rpm;
                    
                    /* 执行速度控制 */
                    speed_adjustment = speed_control_feedback(data->speed, target_rpm);
                    
                    /* 根据速度调整值计算SVPWM输出 */
                    /* 这里简化处理，实际应用中需要更复杂的控制算法 */
                    float magnitude = (float)(current_rpm + speed_adjustment) / 10000.0f; /* 归一化 */
                    if (magnitude > 1.0f) magnitude = 1.0f;
                    if (magnitude < 0.0f) magnitude = 0.0f;
                    
                    /* 获取当前相位 */
                    uint8_t phase = speed_get_current_phase(data->speed);
                    float phase_rad = (float)phase * 3.14159265f / 180.0f; /* 转换为弧度 */
                    
                    /* 计算alpha和beta分量 */
                    alpha = magnitude * cosf(phase_rad);
                    beta = magnitude * sinf(phase_rad);
                    
                    /* 更新SVPWM输出 */
                    svpwm_update_output(data->svpwm, alpha, beta);
                    
                    /* 检查是否需要切换到停止状态 */
                    if (target_rpm == 0 && current_rpm < 100) {
                        data->motor.ops->update_state(&data->motor, MOTOR_STATE_STOP);
                    }
                    break;
                    
                case MOTOR_STATE_FAULT:
                    /* 电机故障状态 */
                    LOG_ERR("Motor fault state");
                    
                    /* 停止PWM输出 */
                    alpha = 0.0f;
                    beta = 0.0f;
                    svpwm_update_output(data->svpwm, alpha, beta);
                    
                    /* 这里可以添加故障处理逻辑，比如尝试恢复或等待外部干预 */
                    break;
                    
                default:
                    /* 未知状态，切换到停止状态 */
                    LOG_WRN("Unknown motor state: %d, switching to STOP", data->state);
                    data->motor.ops->update_state(&data->motor, MOTOR_STATE_STOP);
                    break;
            }
            
            /* 输出调试信息 */
            LOG_DBG("Motor state: %d, Target RPM: %d, Current RPM: %d, Adjustment: %d",
                    data->state, target_rpm, current_rpm, speed_adjustment);
        }
        
        /* 短暂休眠，避免占用过多CPU */
        k_msleep(1);
    }
}

struct motor *motor_init(struct speed *speed, uint8_t type, struct svpwm *svpwm)
{
    struct motor_data *data = k_malloc(sizeof(*data));

    if (!data) {
        LOG_ERR("Failed to allocate memory for motor data");
        return NULL;
    }

    /* 初始化数据结构 */
    memset(data, 0, sizeof(*data));
    data->speed = speed;
    data->type = type;
    data->svpwm = svpwm;
    data->state = MOTOR_STATE_IDENTIFY; /* 初始状态 */
    data->mode = 0; /* 默认模式 */
    
    /* 获取父结构体指针 */
    data->parent = speed->parent;
    
    data->motor.ops = &ops;

    /* 创建电机控制线程 */
    data->tid = k_thread_create(&data->thread,
        data->thread_stack, MOTOR_THREAD_STACK_SIZE,
        motor_thread_entry, data, NULL, NULL, 5, 0, K_MSEC(0));

    LOG_INF("Motor initialized, type: %s",
            type == MOTOR_TYPE_BLDC ? "BLDC" : "FOC");

    return &data->motor;
}

/* 旋转电机到指定角度 */
int motor_rotate(struct motor *motor, float angle_deg)
{
    struct motor_data *data = CONTAINER_OF(motor, struct motor_data, motor);
    float start_angle;
    float end_angle;
    float current_angle;
    float angle_increment;
    uint32_t step_ms = 10; /* 每10ms更新一次 */
    uint32_t steps;
    uint32_t duration_ms;
    float magnitude = 0.2f; /* 固定幅度为20% */
    
    if (!motor || !data->svpwm) {
        LOG_ERR("Invalid motor or SVPWM parameters");
        return -EINVAL;
    }
    
    /* 限制角度范围 */
    if (angle_deg < -360.0f) angle_deg = -360.0f;
    if (angle_deg > 360.0f) angle_deg = 360.0f;
    
    /* 获取当前角度 */
    /* 这里假设我们无法直接获取电机当前角度，所以从0开始 */
    start_angle = 0.0f;
    end_angle = start_angle + angle_deg;
    
    /* 计算旋转步数和持续时间 */
    /* 根据角度大小决定持续时间，每90度需要100ms */
    duration_ms = (uint32_t)(fabsf(angle_deg) / 90.0f * 100.0f);
    if (duration_ms < 50) duration_ms = 50; /* 最小50ms */
    if (duration_ms > 500) duration_ms = 500; /* 最大500ms */
    
    steps = duration_ms / step_ms;
    angle_increment = angle_deg / steps;
    
    LOG_INF("Starting motor rotation - angle: %.2f degrees, duration: %d ms, steps: %d",
            angle_deg, duration_ms, steps);
    
    /* 逐步旋转电机 */
    current_angle = start_angle;
    for (uint32_t i = 0; i < steps; i++) {
        /* 将角度转换为弧度 */
        float angle_rad = current_angle * 3.14159265f / 180.0f;
        
        /* 计算alpha和beta分量 */
        float alpha = magnitude * cosf(angle_rad);
        float beta = magnitude * sinf(angle_rad);
        
        /* 更新SVPWM输出 */
        svpwm_update_output(data->svpwm, alpha, beta);
        
        /* 更新角度 */
        current_angle += angle_increment;
        
        /* 等待下一步 */
        k_msleep(step_ms);
    }
    
    /* 确保最终角度正确 */
    float final_angle_rad = end_angle * 3.14159265f / 180.0f;
    float alpha = magnitude * cosf(final_angle_rad);
    float beta = magnitude * sinf(final_angle_rad);
    svpwm_update_output(data->svpwm, alpha, beta);
    
    /* 短暂延迟以确保到达目标角度 */
    k_msleep(20);
    
    /* 停止电机 */
    svpwm_update_output(data->svpwm, 0.0f, 0.0f);
    
    LOG_INF("Motor rotation completed - final angle: %.2f degrees", end_angle);
    
    return 0;
}
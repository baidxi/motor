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
    void *parent;
    struct k_thread thread;
    k_tid_t tid;
    K_KERNEL_STACK_MEMBER(thread_stack, MOTOR_THREAD_STACK_SIZE);
    struct svpwm *svpwm;
};

static void motor_state_update(struct motor *motor, uint8_t state)
{
    struct motor_data *data = CONTAINER_OF(motor, struct motor_data, motor);
    uint8_t old_state = data->state;

    switch (old_state) {
        case MOTOR_STATE_IDENTIFY:
            break;
            
        case MOTOR_STATE_STOP:
            if (state != MOTOR_STATE_RUN && state != MOTOR_STATE_FAULT) {
                LOG_WRN("Invalid state transition from STOP to %d", state);
                return;
            }
            break;
            
        case MOTOR_STATE_RUN:
            if (state != MOTOR_STATE_STOP && state != MOTOR_STATE_FAULT) {
                LOG_WRN("Invalid state transition from RUN to %d", state);
                return;
            }
            break;
            
        case MOTOR_STATE_FAULT:
            if (state != MOTOR_STATE_STOP) {
                LOG_WRN("Invalid state transition from FAULT to %d", state);
                return;
            }
            break;
            
        default:
            LOG_WRN("Unknown current state: %d", old_state);
            return;
    }
    
    switch (old_state) {
        case MOTOR_STATE_RUN:
            if (state != MOTOR_STATE_RUN) {
                svpwm_update_output(data->svpwm, 0.0f, 0.0f);
                LOG_INF("Stopping PWM output");
            }
            break;
            
        case MOTOR_STATE_FAULT:
            if (state == MOTOR_STATE_STOP) {
                speed_reset(data->speed);
                LOG_INF("Motor fault cleared, parameters reset");
            }
            break;
            
        default:
            break;
    }
    
    data->state = state;
    
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

        if (current_time - last_update_time >= control_period_ms * 1000) {
            last_update_time = current_time;
            
            current_rpm = data->speed->filtered_speed;
            
            switch (data->state) {
                case MOTOR_STATE_IDENTIFY:
                    LOG_DBG("Motor identification state");
                    if (speed_is_valid(data->speed)) {
                        data->motor.ops->update_state(&data->motor, MOTOR_STATE_STOP);
                        
                        if (data->parent) {
                            ctrl_event_post((struct motor_ctrl *)data->parent, MOTOR_EVENT_READY);
                            LOG_INF("Motor ready event posted");
                        }
                    } else {
                        static uint32_t last_rotate_time = 0;
                        uint32_t current_time = k_cycle_get_32();
                        
                        if (current_time - last_rotate_time >= 5 * 1000 * 1000) { /* 5秒 */
                            LOG_DBG("Performing motor rotation for identification");
                            motor_rotate(&data->motor, 5.0f); /* 旋转5度 */
                            last_rotate_time = current_time;
                        } else {
                            alpha = 0.0f;
                            beta = 0.0f;
                            svpwm_update_output(data->svpwm, alpha, beta);
                        }
                    }
                    break;
                    
                case MOTOR_STATE_STOP:
                    LOG_DBG("Motor stop state");

                    target_rpm = 0;
                    
                    speed_adjustment = speed_control_feedback(data->speed, target_rpm);
                    
                    if (current_rpm < 100) { /* 小于100RPM认为接近停止 */
                        alpha = 0.0f;
                        beta = 0.0f;
                        svpwm_update_output(data->svpwm, alpha, beta);
                    }
                    break;
                    
                case MOTOR_STATE_RUN:
                    LOG_DBG("Motor run state, current RPM: %d", current_rpm);
                    
                    target_rpm = data->speed->target_rpm;
                    
                    speed_adjustment = speed_control_feedback(data->speed, target_rpm);
                    
                    float magnitude = (float)(current_rpm + speed_adjustment) / 10000.0f; /* 归一化 */
                    if (magnitude > 1.0f) magnitude = 1.0f;
                    if (magnitude < 0.0f) magnitude = 0.0f;
                    
                    uint8_t phase = speed_get_current_phase(data->speed);
                    float phase_rad = (float)phase * 3.14159265f / 180.0f; /* 转换为弧度 */
                    
                    alpha = magnitude * cosf(phase_rad);
                    beta = magnitude * sinf(phase_rad);
                
                    svpwm_update_output(data->svpwm, alpha, beta);
                    
                    if (target_rpm == 0 && current_rpm < 100) {
                        data->motor.ops->update_state(&data->motor, MOTOR_STATE_STOP);
                    }
                    break;
                    
                case MOTOR_STATE_FAULT:
                    LOG_ERR("Motor fault state");
                    
                    alpha = 0.0f;
                    beta = 0.0f;
                    svpwm_update_output(data->svpwm, alpha, beta);
                    
                    break;
                    
                default:
                    LOG_WRN("Unknown motor state: %d, switching to STOP", data->state);
                    data->motor.ops->update_state(&data->motor, MOTOR_STATE_STOP);
                    break;
            }
            
            LOG_DBG("Motor state: %d, Target RPM: %d, Current RPM: %d, Adjustment: %d",
                    data->state, target_rpm, current_rpm, speed_adjustment);
        }
        
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

    memset(data, 0, sizeof(*data));
    data->speed = speed;
    data->type = type;
    data->svpwm = svpwm;
    data->state = MOTOR_STATE_IDENTIFY; /* 初始状态 */
    data->mode = 0; /* 默认模式 */

    data->parent = speed->parent;
    
    data->motor.ops = &ops;

    data->tid = k_thread_create(&data->thread,
        data->thread_stack, MOTOR_THREAD_STACK_SIZE,
        motor_thread_entry, data, NULL, NULL, 5, 0, K_MSEC(0));

    LOG_INF("Motor initialized, type: %s",
            type == MOTOR_TYPE_BLDC ? "BLDC" : "FOC");

    return &data->motor;
}

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
    
    if (angle_deg < -360.0f) angle_deg = -360.0f;
    if (angle_deg > 360.0f) angle_deg = 360.0f;
    
    start_angle = 0.0f;
    end_angle = start_angle + angle_deg;
    
    duration_ms = (uint32_t)(fabsf(angle_deg) / 90.0f * 100.0f);
    if (duration_ms < 50) duration_ms = 50; /* 最小50ms */
    if (duration_ms > 500) duration_ms = 500; /* 最大500ms */
    
    steps = duration_ms / step_ms;
    angle_increment = angle_deg / steps;
    
    LOG_DBG("Starting motor rotation - angle: %.2f degrees, duration: %d ms, steps: %d",
            angle_deg, duration_ms, steps);
    
    current_angle = start_angle;
    for (uint32_t i = 0; i < steps; i++) {
        float angle_rad = current_angle * 3.14159265f / 180.0f;
        
        float alpha = magnitude * cosf(angle_rad);
        float beta = magnitude * sinf(angle_rad);
        
        svpwm_update_output(data->svpwm, alpha, beta);
        
        current_angle += angle_increment;
        
        k_msleep(step_ms);
    }
    
    float final_angle_rad = end_angle * 3.14159265f / 180.0f;
    float alpha = magnitude * cosf(final_angle_rad);
    float beta = magnitude * sinf(final_angle_rad);
    svpwm_update_output(data->svpwm, alpha, beta);
    
    k_msleep(20);
    
    svpwm_update_output(data->svpwm, 0.0f, 0.0f);
    
    LOG_DBG("Motor rotation completed - final angle: %.2f degrees", end_angle);
    
    return 0;
}
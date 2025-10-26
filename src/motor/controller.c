#include <motor/motor.h>
#include <motor/controller.h>
#include <motor/svpwm.h>
#include <motor/adc.h>
#include <motor/speed.h>
#include <motor/motor.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(controller, LOG_LEVEL_INF);

struct motor_ctrl_data {
    struct motor_ctrl ctrl;
    struct motor_adc *adc;
    struct svpwm *svpwm;
    struct speed *speed;
    struct motor *motor;
    uint8_t nb_channels;
    struct k_event event;
    struct k_work work;
    bool start;
    struct k_work_q work_q;
};

static K_KERNEL_STACK_DEFINE(motor_ctrl_work_q_stack, 256);

static void motor_ctrl_work(struct k_work *work)
{
    struct motor_ctrl_data *data = CONTAINER_OF(work, struct motor_ctrl_data, work);
    int ret = 0;
    /* 检查参数有效性 */
    if (!data || !data->svpwm || !data->motor) {
        LOG_ERR("Invalid motor control data");
        return;
    }
    
    adc_start(data->adc);
    
    /* 开启所有PWM通道 */
    for (uint8_t i = 0; i < data->nb_channels; i++) {
        data->svpwm->ops->enable(data->svpwm, i);
    }
    
    /* 检查电机状态是否就绪 */
    if (!speed_is_valid(data->speed)) {
        LOG_INF("Motor speed not valid, waiting for identification...");
        do {
            ret = k_event_wait(&data->event, MOTOR_EVENT_READY, false, K_MSEC(500));
        }while(ret == 0 && data->start);
    }
    

    if (ret == 0)
        return;
    /* 检查电机当前状态 */
    /* 由于motor_data结构体在controller.c中不可见，我们需要通过motor接口来操作 */
    
    /* 设置初始目标转速 */
    data->speed->target_rpm = 1000; /* 设置一个初始转速 */
    
    /* 通过motor接口更新状态到运行状态 */
    /* 这里假设电机已经完成识别，处于停止状态 */
    LOG_INF("Starting motor");
    data->motor->ops->update_state(data->motor, MOTOR_STATE_RUN);
    
    LOG_INF("Motor start command sent");
}

static void motor_start(struct motor_ctrl *ctrl)
{
    struct motor_ctrl_data *data = CONTAINER_OF(ctrl, struct motor_ctrl_data, ctrl);
    data->start = true;

    k_work_submit_to_queue(&data->work_q, &data->work);
}

static void motor_stop(struct motor_ctrl *ctrl)
{
    struct motor_ctrl_data *data = CONTAINER_OF(ctrl, struct motor_ctrl_data, ctrl);

    data->start = false;

    k_work_cancel(&data->work);
}

static const struct motor_ctrl_ops ops = {
    .start = motor_start,
    .stop = motor_stop,
};

struct motor_ctrl *motor_ctrl_init(const struct pwm_info *pwm_info, const struct adc_info *adc_info, uint32_t freq)
{
    struct motor_ctrl_data *data = k_malloc(sizeof(*data));
    static const struct k_work_queue_config work_q_cfg = {
		.name = "motor_ctrl_work_queue",
		.no_yield = IS_ENABLED(CONFIG_SYSTEM_WORKQUEUE_NO_YIELD),
		.essential = true,
		.work_timeout_ms = CONFIG_SYSTEM_WORKQUEUE_WORK_TIMEOUT_MS,
	};
    data->nb_channels = pwm_info->nb_channels;
    data->ctrl.system_clock_freq = freq;

    k_event_init(&data->event);

    data->adc = adc_init(adc_info);
    data->speed = speed_init(data->adc, &data->ctrl);

    uint32_t pwm_prescaler = 0; 
    uint32_t pwm_clock_freq = data->ctrl.system_clock_freq / (pwm_prescaler + 1);
    
    bool center_aligned = true; 
    uint32_t freq_divider = center_aligned ? 2 : 1;
    
    uint16_t target_freq = 20000; /* 目标频率20KHz */
    uint16_t cycle = (pwm_clock_freq / (freq_divider * target_freq)) - 1;
    
    if (cycle < 100) {
        cycle = 100; /* 最小cycle值 */
        LOG_WRN("Calculated cycle value too small, using minimum value: %d", cycle);
    } else if (cycle > 65535) {
        cycle = 65535; /* 最大cycle值 */
        LOG_WRN("Calculated cycle value too large, using maximum value: %d", cycle);
    }
    
    LOG_INF("Initializing SVPWM with target frequency %d Hz, calculated cycle %d", target_freq, cycle);

    data->svpwm = svpwm_init(pwm_info, target_freq, cycle, data->ctrl.system_clock_freq);
    data->motor = motor_init(data->speed, MOTOR_TYPE_BLDC, data->svpwm);

    data->ctrl.ops = &ops;

    data->start = false;

    k_work_queue_start(&data->work_q, motor_ctrl_work_q_stack,
        K_KERNEL_STACK_SIZEOF(motor_ctrl_work_q_stack),
        CONFIG_SYSTEM_WORKQUEUE_PRIORITY, &work_q_cfg);

    k_work_init(&data->work, motor_ctrl_work);
    
    return &data->ctrl;
}

void ctrl_event_post(struct motor_ctrl *ctrl, uint32_t event)
{
    struct motor_ctrl_data *data = CONTAINER_OF(ctrl, struct motor_ctrl_data, ctrl);

    k_event_post(&data->event, event);
}

int motor_ctrl_speed_register(struct motor_ctrl *ctrl, struct adc_callback_t *callback)
{
     struct motor_ctrl_data *data = CONTAINER_OF(ctrl, struct motor_ctrl_data, ctrl);
     return adc_register_callback(data->adc, callback);
}

void motor_ctrl(void *ctrl, bool enable)
{
    struct motor_ctrl *c = ctrl;
    if (enable)
        c->ops->start(c);
    else
        c->ops->stop(c);
}
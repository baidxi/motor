#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <motor/adc.h>

#define ADC_THREAD_STACK_SIZE   512
#define OVER_SAMPLE 0

struct motor_adc {
    struct adc_callback {
        adc_callback_func func;
        uint8_t id;
        void *param;
    } *callback;
    const struct adc_info *info;

    struct k_thread thread;
    k_tid_t tid;
    K_KERNEL_STACK_MEMBER(thread_stack, ADC_THREAD_STACK_SIZE);
    struct k_poll_signal async_sig[8];
};

LOG_MODULE_REGISTER(adc, LOG_LEVEL_INF);

static enum adc_action adc_done(const struct device *dev,
						 const struct adc_sequence *sequence,
						 uint16_t sampling_index)
{
    struct adc_callback *cb = sequence->options->user_data;

    if (cb && cb->func)
    {
        cb->func(sequence->buffer, sequence->buffer_size, cb->id, cb->param);
    }
    return ADC_ACTION_FINISH;
}

static void adc_thread_entry(void *v1, void *v2, void *v3)
{
    struct motor_adc *adc = v1;
    const struct adc_channel_cfg *cfg;
    int i;
    struct k_poll_event  async_evt[8] = {
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                K_POLL_MODE_NOTIFY_ONLY,
                &adc->async_sig[0]),
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                K_POLL_MODE_NOTIFY_ONLY,
                &adc->async_sig[1]),
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                K_POLL_MODE_NOTIFY_ONLY,
                &adc->async_sig[2]),
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                K_POLL_MODE_NOTIFY_ONLY,
                &adc->async_sig[3]),
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                K_POLL_MODE_NOTIFY_ONLY,
                &adc->async_sig[4]),
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                K_POLL_MODE_NOTIFY_ONLY,
                &adc->async_sig[5]),
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                K_POLL_MODE_NOTIFY_ONLY,
                &adc->async_sig[6]),
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                K_POLL_MODE_NOTIFY_ONLY,
                &adc->async_sig[7]),
    };
    struct adc_sequence *seqs = k_malloc(sizeof(*seqs) * adc->info->nb_channels);
    struct adc_sequence_options *options = k_malloc(sizeof(*options) * adc->info->nb_channels);
    int16_t *values = k_malloc(sizeof(int16_t) * adc->info->nb_channels * (OVER_SAMPLE + 1));

    if (!values || !options || !seqs)
    {
        LOG_ERR("thread init err");
        if (values)
            k_free(values);
        if (options)
            k_free(options);
        if (seqs)
            k_free(seqs);
        
        return;
    }

    for (i = 0; i < adc->info->nb_channels; i++)
    {
        cfg = &adc->info->channels[i].cfg;

        options[i].callback = adc_done;
        options[i].user_data = &adc->callback[i];
        options[i].extra_samplings = OVER_SAMPLE;
        options[i].interval_us = 20;

        seqs[i].buffer = &values[i * OVER_SAMPLE + 1];
        seqs[i].buffer_size = sizeof(int16_t) * (OVER_SAMPLE + 1);
        seqs[i].channels = BIT(cfg->channel_id);
        seqs[i].options = &options[i];
        seqs[i].resolution = 12;
        seqs[i].oversampling = 0;
    }

    while(1)
    {
        for (i = 0; i < adc->info->nb_channels; i++)
        {
            adc_read_async(adc->info->dev, &seqs[i], &adc->async_sig[i]);
        }
        k_poll(async_evt, adc->info->nb_channels, K_MSEC(1000));
    }

}

struct motor_adc *adc_init(const struct adc_info *info)
{
    struct motor_adc *adc;
    
    if (info && (!info->channels || !info->dev || !info->nb_channels))
    {
        LOG_ERR("adc info Invalid");
        return NULL;
    }

    adc = k_malloc(sizeof(struct motor_adc));

    if (adc)
    {
        memset(adc, 0, sizeof(*adc));
        adc->info = info;
        adc->callback = k_malloc(sizeof(struct adc_callback) * info->nb_channels);
        if (!adc->callback)
        {
            goto err;
        }

        memset(adc->callback, 0, sizeof(struct adc_callback) * info->nb_channels);

        adc->tid = k_thread_create(&adc->thread, adc->thread_stack, sizeof(adc->thread_stack), adc_thread_entry, adc, NULL, NULL, 5, 0, K_MSEC(0));
        if (!adc->tid)
        {
            LOG_ERR("create thread err");
            goto err;
        }
    }

    return adc;
err:
    LOG_ERR("init err");
    k_free(adc);
    return NULL;
}

int adc_register_callback(struct motor_adc *adc, adc_callback_func func, uint8_t id, void *param)
{
    if (id > adc->info->nb_channels)
        return -EINVAL;
        
    if (adc)
    {
        if (adc->callback[id].func)
            return -EEXIST;
        adc->callback[id].id = id;
        adc->callback[id].func = func;
        adc->callback[id].param = param;
    }
    return 0;
}
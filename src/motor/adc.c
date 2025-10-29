#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <motor/adc.h>

#define ADC_THREAD_STACK_SIZE   512
#define OVER_SAMPLE 0

struct adc_t {
    const struct adc_info *info;
    struct k_thread thread;
    k_tid_t tid;
    K_KERNEL_STACK_MEMBER(thread_stack, ADC_THREAD_STACK_SIZE);
    struct k_poll_signal async_sig[8];
    struct adc_sequence *seqs;
    uint16_t *values;
    struct adc_sequence_options *options;
    struct adc_callback_t **callbacks;
    struct adc_user_data {
        uint8_t id;
        struct adc_t *adc;
    } *user_data;
};

LOG_MODULE_REGISTER(adc, LOG_LEVEL_INF);

static enum adc_action adc_done(const struct device *dev,
						 const struct adc_sequence *sequence,
						 uint16_t sampling_index)
{
    struct adc_user_data *data = sequence->options->user_data;
    struct adc_callback_t *callback;

    for (callback = data->adc->callbacks[data->id]; callback; callback = callback->next)
    {
        callback->func(callback, sequence->buffer, sequence->buffer_size, callback->param);
    }

    return ADC_ACTION_FINISH;
}

static void adc_thread_entry(void *v1, void *v2, void *v3)
{
    struct adc_t *adc = v1;
    const struct adc_channel_cfg *cfg;
    struct adc_sequence *seq;
    struct adc_sequence_options *options;
    struct adc_user_data *user_data;
    int16_t *values = adc->values;
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

    for (i = 0; i < adc->info->nb_channels; i++)
    {
        cfg = &adc->info->channels[i].cfg;
        user_data = &adc->user_data[i];
        options = &adc->options[i];
        seq = &adc->seqs[i];

        user_data->id = i;
        user_data->adc = adc;

        options->callback = adc_done;
        options->user_data = user_data;
        options->extra_samplings = OVER_SAMPLE;
        options->interval_us = 0x20;

        seq->buffer = &values[i + i * OVER_SAMPLE];
        seq->buffer_size = sizeof(int16_t) * (OVER_SAMPLE + 1);
        seq->channels = BIT(cfg->channel_id);
        seq->options = options;
        seq->resolution = 12;
        seq->oversampling = 0;
    }

    while(1)
    {
        for (i = 0; i < adc->info->nb_channels; i++)
        {
            adc_read_async(adc->info->dev, &adc->seqs[i], &adc->async_sig[i]);
        }
        k_poll(async_evt, adc->info->nb_channels, K_MSEC(1000));
    }

}

struct adc_t *adc_init(const struct adc_info *info)
{
    struct adc_t *adc;
    size_t alloc_size;

    if (info && (!info->channels || !info->dev || !info->nb_channels))
    {
        LOG_ERR("adc info Invalid");
        return NULL;
    }

    if (!device_is_ready(info->dev))
    {
        LOG_ERR("adc device not ready");
        return NULL;
    }

    alloc_size = sizeof(*adc) ;

    adc = k_malloc(alloc_size);

    if (adc)
    {
        memset(adc, 0, alloc_size);

        adc->info = info;

        adc->options = k_malloc(sizeof(*adc->options) * info->nb_channels);
        adc->seqs = k_malloc(sizeof(*adc->seqs) * info->nb_channels);
        adc->values = k_malloc((sizeof(*adc->values) + OVER_SAMPLE) * info->nb_channels);
        adc->user_data = k_malloc(sizeof(*adc->user_data) * info->nb_channels);
        adc->callbacks = k_malloc(sizeof(void *) * info->nb_channels);

        memset(adc->callbacks, 0, sizeof(void *) * info->nb_channels);

        adc->tid = k_thread_create(&adc->thread, adc->thread_stack, sizeof(adc->thread_stack), adc_thread_entry, adc, NULL, NULL, 5, 0, K_FOREVER);
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

int adc_register_callback(struct adc_t *adc, struct adc_callback_t *cb)
{        
    struct adc_callback_t *callback;

    if (adc)
    {
        cb->next = NULL;
        callback = adc->callbacks[cb->id];
        if (!callback) {
            adc->callbacks[cb->id] = cb;
            return 0;
        }

        for (; callback->next; callback = callback->next)
        {

        }

        callback->next = cb;

        return 0;
    }
    return -1;
}

void adc_start(struct adc_t *adc)
{
    k_thread_start(adc->tid);
}
#include "zephyr/drivers/adc.h"
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
    struct adc_callback_t **callbacks;
};

LOG_MODULE_REGISTER(adc, LOG_LEVEL_INF);

static void adc_thread_entry(void *v1, void *v2, void *v3)
{
    struct adc_t *adc = v1;
    struct adc_sequence_options opts = {0};
    struct adc_sequence seq = { .options = &opts };
    struct k_poll_signal done_signal = {0};
    struct k_poll_event done_event = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &done_signal);
    int i, chan_avai, buf_size = sizeof(uint16_t)  * adc->info->nb_channels;
    uint16_t *buf = k_malloc(buf_size);
    uint8_t *chan = k_malloc(adc->info->nb_channels);
    const struct adc_channel_cfg *cfg;
    struct adc_callback_t *cb;

    seq.buffer = buf;
    seq.buffer_size = buf_size;
    seq.oversampling = 0;
    seq.resolution = 12;

    while(true)
    {
        chan_avai = 0;
        seq.channels = 0;
        memset(chan, 0, adc->info->nb_channels);

        for (i = 0; i < adc->info->nb_channels; i++)
        {
            if (adc->callbacks[i])
            {
                cfg = &adc->info->channels[i].cfg;
                seq.channels |= BIT(cfg->channel_id);
                chan[chan_avai++] = i;
            }
        }

        if (chan_avai == 0)
        {
            k_sleep(K_MSEC(100));
            continue;
        }

        adc_read_async(adc->info->dev, &seq, &done_signal);

        k_poll(&done_event, 1, K_MSEC(500));

        if (done_event.signal->result == 0)
        {
            for (i = 0; i < chan_avai; i++)
            {
                uint16_t value = buf[i];
                for (cb = adc->callbacks[chan[i]]; cb; cb = cb->next)
                {
                    cb->func(cb, &value, 2, NULL);
                }
            }
        }

        done_event.state = K_POLL_STATE_NOT_READY;
        k_poll_signal_reset(&done_signal);
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

    for (int i = 0; i < info->nb_channels; i++) {
        if (adc_channel_setup(info->dev, &info->channels[i].cfg) != 0) {
            LOG_ERR("Failed to setup ADC channel %d", info->channels[i].id);
            return NULL;
        }
    }
 
    alloc_size = sizeof(*adc) ;

    adc = k_malloc(alloc_size);

    if (adc)
    {
        memset(adc, 0, alloc_size);

        adc->info = info;
        adc->callbacks = k_malloc(sizeof(void *) * info->nb_channels);

        memset(adc->callbacks, 0, sizeof(void *) * info->nb_channels);

        adc->tid = k_thread_create(&adc->thread, adc->thread_stack, sizeof(adc->thread_stack), adc_thread_entry, adc, NULL, NULL, 100, 0, K_FOREVER);
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
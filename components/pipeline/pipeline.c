#include "audio_pipeline.h"

audio_pipeline_handle_t pipeline;

void pn_pipeline_init()
{
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
}

esp_err_t pn_pipeline_deinit()
{
    return audio_pipeline_deinit(pipeline);
}

esp_err_t pn_pipeline_register(audio_element_handle_t element, const char *name)
{
    return audio_pipeline_register(pipeline, element, name);
}

esp_err_t pn_pipeline_unregister(audio_element_handle_t element)
{
    return audio_pipeline_unregister(pipeline, element);
}

esp_err_t pn_pipeline_link(const char **link_tag, int link_num)
{
    // be careful with the second argument!
    return audio_pipeline_link(pipeline, link_tag, link_num);
}

esp_err_t pn_pipeline_run()
{
    return audio_pipeline_run(pipeline);
}

esp_err_t pn_pipeline_stop()
{
    return audio_pipeline_stop(pipeline);
}

esp_err_t pn_pipeline_wait_for_stop()
{
    return audio_pipeline_wait_for_stop(pipeline);
}

esp_err_t pn_pipeline_stop_all()
{
    esp_err_t result;

    result = pn_pipeline_stop();
    if (result != ESP_OK)
    {
        return result;
    }

    result = pn_pipeline_wait_for_stop();
    if (result != ESP_OK)
    {
        return result;
    }

    return ESP_OK;
}

esp_err_t pn_pipeline_terminate()
{
    return audio_pipeline_terminate(pipeline);
}

esp_err_t pn_pipeline_destroy()
{
    esp_err_t result;

    result = pn_pipeline_stop_all();
    if (result != ESP_OK)
    {
        return result;
    }

    result = pn_pipeline_terminate();
    if (result != ESP_OK)
    {
        return result;
    }

    return ESP_OK;
}

esp_err_t pn_pipeline_reset_ringbuffer()
{
    return audio_pipeline_reset_ringbuffer(pipeline);
}

esp_err_t pn_pipeline_reset_items_state()
{
    return audio_pipeline_reset_items_state(pipeline);
}

esp_err_t pn_pipeline_reset_all()
{
    esp_err_t result;

    result = pn_pipeline_reset_ringbuffer();
    if (result != ESP_OK)
    {
        return result;
    }

    result = pn_pipeline_reset_items_state();
    if (result != ESP_OK)
    {
        return result;
    }

    return ESP_OK;
}

esp_err_t pn_pipeline_set_listener(audio_event_iface_handle_t event)
{
    return audio_pipeline_set_listener(pipeline, event);
}

esp_err_t pn_pipeline_remove_listener()
{
    return audio_pipeline_remove_listener(pipeline);
}
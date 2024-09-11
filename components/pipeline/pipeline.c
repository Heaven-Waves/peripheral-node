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


#ifndef PERIHERAL_NODE_PIPELINE
#define PERIHERAL_NODE_PIPELINE

// #include "audio_pipeline.h"

void pn_pipeline_init();

esp_err_t pn_pipeline_deinit();

esp_err_t pn_pipeline_register(audio_element_handle_t element, const char *name);

esp_err_t pn_pipeline_unregister(audio_element_handle_t element);

esp_err_t pn_pipeline_link(const char **link_tag, int link_num);

esp_err_t pn_pipeline_run();

esp_err_t pn_pipeline_stop();

esp_err_t pn_pipeline_wait_for_stop();

esp_err_t pn_pipeline_stop_all();

esp_err_t pn_pipeline_terminate();

esp_err_t pn_pipeline_destroy();

esp_err_t pn_pipeline_reset_ringbuffer();

esp_err_t pn_pipeline_reset_items_state();

esp_err_t pn_pipeline_reset_all();

esp_err_t pn_pipeline_set_listener(audio_event_iface_handle_t event);

esp_err_t pn_pipeline_remove_listener();

#endif // PERIHERAL_NODE_PIPELINE
set(EXTRA_COMPONENT_DIRS $ENV{ADF_PATH}/components)

idf_component_register(
    SRCS "pipeline.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_pipeline
)
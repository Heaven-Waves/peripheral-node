set(EXTRA_COMPONENT_DIRS $ENV{ADF_PATH}/components)

idf_component_register(
    SRCS "logs.c"
    INCLUDE_DIRS "include"
    # REQUIRES esp_log
)
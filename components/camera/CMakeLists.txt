idf_component_register(
    SRCS "src/camera_service.c"
    INCLUDE_DIRS "include"
    REQUIRES common drivers log esp_psram
)

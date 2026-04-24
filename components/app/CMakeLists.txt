idf_component_register(
    SRCS "src/app_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES common ai camera drivers game storage ui freertos esp_timer log
)

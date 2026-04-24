idf_component_register(
    SRCS
        "src/board_profile.c"
        "src/display_hal.c"
        "src/input_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES common driver esp_timer log esp_adc esp_lcd heap espressif__esp32_s3_eye_noglib
)

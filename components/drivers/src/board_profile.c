#include "drivers/board_profile.h"

#include <stdio.h>

#include "common/app_config.h"

static const board_adc_keypoint_t k_esp32_s3_eye_keys[] = {
    {.key = INPUT_KEY_UP, .expected_mv = 380, .label = "UP+"},
    {.key = INPUT_KEY_DOWN, .expected_mv = 820, .label = "DN-"},
    {.key = INPUT_KEY_CONFIRM, .expected_mv = 1980, .label = "PLAY"},
    {.key = INPUT_KEY_BACK, .expected_mv = 2410, .label = "MENU"},
};

static const board_profile_t k_esp32_s3_eye = {
    .name = "ESP32-S3-EYE",
    .button_adc_gpio = GPIO_NUM_1,
    .button_adc_unit = ADC_UNIT_1,
    .button_adc_channel = ADC_CHANNEL_0,
    .button_keypoints = k_esp32_s3_eye_keys,
    .button_keypoint_count = sizeof(k_esp32_s3_eye_keys) / sizeof(k_esp32_s3_eye_keys[0]),
    .camera = {
        .sccb_sda = GPIO_NUM_4,
        .sccb_scl = GPIO_NUM_5,
        .vsync = GPIO_NUM_6,
        .href = GPIO_NUM_7,
        .xclk = GPIO_NUM_15,
        .pclk = GPIO_NUM_13,
        .data_y9 = GPIO_NUM_16,
        .data_y8 = GPIO_NUM_17,
        .data_y7 = GPIO_NUM_18,
        .data_y6 = GPIO_NUM_12,
        .data_y5 = GPIO_NUM_10,
        .data_y4 = GPIO_NUM_8,
        .data_y3 = GPIO_NUM_9,
        .data_y2 = GPIO_NUM_11,
#if CATDEX_CAMERA_RESET_GPIO >= 0
        .reset = (gpio_num_t)CATDEX_CAMERA_RESET_GPIO,
#else
        .reset = GPIO_NUM_NC,
#endif
#if CATDEX_CAMERA_PWDN_GPIO >= 0
        .pwdn = (gpio_num_t)CATDEX_CAMERA_PWDN_GPIO,
#else
        .pwdn = GPIO_NUM_NC,
#endif
    },
    .lcd = {
        .spi_host = SPI3_HOST,
        .sclk = GPIO_NUM_21,
        .mosi = GPIO_NUM_47,
        .dc = GPIO_NUM_43,
        .cs = GPIO_NUM_44,
        .reset = GPIO_NUM_NC,
        .backlight = GPIO_NUM_48,
        .pixel_clock_hz = 40 * 1000 * 1000,
        .width = 240,
        .height = 240,
        .x_gap = 0,
        .y_gap = 0,
        .invert_color = true,
        .j9_pins = {GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_44, GPIO_NUM_21, GPIO_NUM_43},
        .j8_pins = {GPIO_NUM_0, GPIO_NUM_3, GPIO_NUM_45, GPIO_NUM_46},
        .note = "LCD SPI pins follow official ESP32-S3-EYE BSP assumptions",
    },
    .notes = "Buttons and main camera data pins are schematic-confirmed. LCD SPI pin semantics follow the official ESP32-S3-EYE BSP. Camera RESET/PWDN GPIOs remain configurable until the daughterboard wiring is fully verified.",
};

const board_profile_t *board_profile_get_active(void)
{
#if CONFIG_CATDEX_BOARD_PROFILE_ESP32_S3_EYE
    return &k_esp32_s3_eye;
#else
    return &k_esp32_s3_eye;
#endif
}

const char *board_profile_key_name(input_key_t key)
{
    switch (key) {
        case INPUT_KEY_UP:
            return "UP";
        case INPUT_KEY_DOWN:
            return "DOWN";
        case INPUT_KEY_LEFT:
            return "LEFT";
        case INPUT_KEY_RIGHT:
            return "RIGHT";
        case INPUT_KEY_CONFIRM:
            return "CONFIRM";
        case INPUT_KEY_BACK:
            return "BACK";
        default:
            return "NONE";
    }
}

void board_profile_describe_camera(char *buffer, size_t buffer_len)
{
    const board_profile_t *profile = board_profile_get_active();
    if (buffer == NULL || buffer_len == 0 || profile == NULL) {
        return;
    }

    snprintf(buffer, buffer_len,
             "Cam SDA=%d SCL=%d XCLK=%d PCLK=%d",
             profile->camera.sccb_sda,
             profile->camera.sccb_scl,
             profile->camera.xclk,
             profile->camera.pclk);
}

void board_profile_describe_lcd(char *buffer, size_t buffer_len)
{
    const board_profile_t *profile = board_profile_get_active();
    if (buffer == NULL || buffer_len == 0 || profile == NULL) {
        return;
    }

    snprintf(buffer, buffer_len,
             "LCD ST7789 CLK=%d MOSI=%d DC=%d CS=%d BL=%d",
             profile->lcd.sclk,
             profile->lcd.mosi,
             profile->lcd.dc,
             profile->lcd.cs,
             profile->lcd.backlight);
}

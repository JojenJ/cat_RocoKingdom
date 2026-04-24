#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_adc/adc_oneshot.h"

#include "drivers/input_hal.h"

typedef struct {
    input_key_t key;
    int expected_mv;
    const char *label;
} board_adc_keypoint_t;

typedef struct {
    gpio_num_t sccb_sda;
    gpio_num_t sccb_scl;
    gpio_num_t vsync;
    gpio_num_t href;
    gpio_num_t xclk;
    gpio_num_t pclk;
    gpio_num_t data_y9;
    gpio_num_t data_y8;
    gpio_num_t data_y7;
    gpio_num_t data_y6;
    gpio_num_t data_y5;
    gpio_num_t data_y4;
    gpio_num_t data_y3;
    gpio_num_t data_y2;
    gpio_num_t reset;
    gpio_num_t pwdn;
} board_camera_pins_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t sclk;
    gpio_num_t mosi;
    gpio_num_t dc;
    gpio_num_t cs;
    gpio_num_t reset;
    gpio_num_t backlight;
    uint32_t pixel_clock_hz;
    uint16_t width;
    uint16_t height;
    uint16_t x_gap;
    uint16_t y_gap;
    bool invert_color;
    gpio_num_t j9_pins[5];
    gpio_num_t j8_pins[4];
    const char *note;
} board_lcd_connector_t;

typedef struct {
    const char *name;
    gpio_num_t button_adc_gpio;
    adc_unit_t button_adc_unit;
    adc_channel_t button_adc_channel;
    const board_adc_keypoint_t *button_keypoints;
    size_t button_keypoint_count;
    board_camera_pins_t camera;
    board_lcd_connector_t lcd;
    const char *notes;
} board_profile_t;

const board_profile_t *board_profile_get_active(void);
const char *board_profile_key_name(input_key_t key);
void board_profile_describe_camera(char *buffer, size_t buffer_len);
void board_profile_describe_lcd(char *buffer, size_t buffer_len);

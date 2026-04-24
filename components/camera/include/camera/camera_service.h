#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool camera_ready;
    bool using_real_camera;
    bool init_attempted;
    bool preview_available;
    uint32_t frame_counter;
    uint32_t frame_failures;
    uint16_t last_width;
    uint16_t last_height;
    uint32_t last_frame_len;
    uint16_t preview_width;
    uint16_t preview_height;
    uint16_t *preview_rgb565;
    size_t preview_pixels;
    char status_text[48];
} camera_service_t;

esp_err_t camera_service_init(camera_service_t *camera);
void camera_service_poll(camera_service_t *camera);
void camera_service_get_status(const camera_service_t *camera, char *buffer, size_t buffer_len);
bool camera_service_get_preview_rgb565(const camera_service_t *camera,
                                       const uint16_t **pixels,
                                       uint16_t *width,
                                       uint16_t *height);
bool camera_service_build_thumbnail_rgb565(const camera_service_t *camera,
                                           uint16_t *dst_rgb565,
                                           uint16_t dst_width,
                                           uint16_t dst_height);

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common/app_config.h"

typedef struct {
    bool has_preview;
    const uint16_t *rgb565;
    uint16_t width;
    uint16_t height;
} display_preview_t;

typedef enum {
    DISPLAY_LAYOUT_STANDARD = 0,
    DISPLAY_LAYOUT_CAPTURE,
    DISPLAY_LAYOUT_PHOTO_DETAIL,
    DISPLAY_LAYOUT_MAIN_MENU,
    DISPLAY_LAYOUT_CAPTURE_CUSTOM,
    DISPLAY_LAYOUT_DETAIL_CUSTOM,
    DISPLAY_LAYOUT_CAPTURE_RESULT,
    DISPLAY_LAYOUT_IMAGE_ONLY,
} display_layout_t;

typedef struct {
    const uint16_t *rgb565;
    uint16_t src_w, src_h;
    int16_t dst_x, dst_y;
    uint16_t dst_w, dst_h;
} display_overlay_t;

#define DISPLAY_MAX_OVERLAYS 4

typedef struct {
    display_layout_t layout;
    char title[CATDEX_UI_LINE_LEN];
    char lines[CATDEX_UI_LINE_COUNT][CATDEX_UI_LINE_LEN];
    char footer[CATDEX_UI_LINE_LEN];
    display_preview_t preview;
    const uint16_t *bg_image_rgb565;
    display_overlay_t overlays[DISPLAY_MAX_OVERLAYS];
    uint8_t overlay_count;
} display_frame_t;

void display_hal_init(void);
void display_hal_present(const display_frame_t *frame);
void display_hal_blit_preview(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h);
bool display_hal_is_lcd_ready(void);
const char *display_hal_backend_name(void);

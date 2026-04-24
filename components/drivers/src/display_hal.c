#include "drivers/display_hal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "bsp/display.h"
#include "common/app_config.h"
#include "drivers/board_profile.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

typedef struct {
    char c;
    uint8_t rows[7];
} glyph5x7_t;

static const glyph5x7_t k_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'#', {0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'*', {0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C}},
    {':', {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
};

typedef struct {
    bool lcd_ready;
    bool capture_static_ready;
    int backlight_on_level;
    char backend_name[24];
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    uint16_t *framebuffer;
    size_t framebuffer_pixels;
    uint16_t *scratch;
    uint16_t *capture_preview_buffer;
    uint16_t *capture_band_buffers[5];
    size_t scratch_pixels;
    size_t capture_band_pixels;
    size_t width;
    size_t height;
    display_frame_t last_frame;
    SemaphoreHandle_t lcd_mutex;
} display_hal_state_t;

static display_hal_state_t s_display;
static const char *TAG = "display_hal";
static const size_t k_lcd_scratch_rows = 40;
static const int k_capture_preview_x = 40;
static const int k_capture_preview_y = 36;
static const int k_capture_preview_w = 160;
static const int k_capture_preview_h = 120;
static const int k_capture_preview_border = 4;
static const bool k_lcd_smoke_test_mode = false;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
static const glyph5x7_t *find_glyph(char c);
static void copy_uppercase(char *dst, size_t dst_len, const char *src);
static void run_lcd_smoke_test(void);
static void set_backlight_level(int level);
static void render_fullscreen_frame(const display_frame_t *frame);

static esp_err_t wait_for_pending_io(void)
{
    if (!s_display.lcd_ready || s_display.io == NULL) {
        return ESP_OK;
    }

    return esp_lcd_panel_io_tx_param(s_display.io, -1, NULL, 0);
}

static esp_err_t draw_bitmap_sync(int x0, int y0, int x1, int y1, const void *pixels)
{
    if (!s_display.lcd_ready || s_display.panel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_lcd_panel_draw_bitmap(s_display.panel, x0, y0, x1, y1, pixels);
}

static void fill_framebuffer(uint16_t color)
{
    if (s_display.framebuffer == NULL) {
        return;
    }

    for (size_t i = 0; i < s_display.framebuffer_pixels; ++i) {
        s_display.framebuffer[i] = color;
    }
}

static void fill_framebuffer_rect(int x, int y, int width, int height, uint16_t color)
{
    if (s_display.framebuffer == NULL) {
        return;
    }

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= (int)s_display.width || y >= (int)s_display.height || width <= 0 || height <= 0) {
        return;
    }
    if (x + width > (int)s_display.width) {
        width = (int)s_display.width - x;
    }
    if (y + height > (int)s_display.height) {
        height = (int)s_display.height - y;
    }

    for (int row = 0; row < height; ++row) {
        uint16_t *dst = &s_display.framebuffer[(size_t)(y + row) * s_display.width + (size_t)x];
        for (int col = 0; col < width; ++col) {
            dst[col] = color;
        }
    }
}

/* Draw text with highlight (+0,-1) and soft shadow (+1,+1) for embossed look */
static void draw_text_shadow_to_framebuffer(int y, int height, uint16_t bg, uint16_t fg, uint16_t shadow,
                                             const char *text, int scale, int x_start)
{
    if (s_display.framebuffer == NULL || height <= 0 || scale <= 0) return;
    fill_framebuffer_rect(0, y, (int)s_display.width, height, bg);
    char upper[CATDEX_UI_LINE_LEN] = {0};
    copy_uppercase(upper, sizeof(upper), text);
    const int glyph_w = 5, glyph_h = 7;
    const int stride = glyph_w * scale + scale;
    const int y_start = y + (height - glyph_h * scale) / 2;
    /* unswap for color math */
    uint16_t fg_n  = (uint16_t)((fg     >> 8) | (fg     << 8));
    uint16_t bg_n  = (uint16_t)((bg     >> 8) | (bg     << 8));
    uint16_t sh_n  = (uint16_t)((shadow >> 8) | (shadow << 8));
    /* soft shadow: blend shadow+bg 50% */
    uint16_t soft_n = (uint16_t)(
        ((((sh_n >> 11) & 0x1F) + ((bg_n >> 11) & 0x1F)) / 2) << 11 |
        ((((sh_n >>  5) & 0x3F) + ((bg_n >>  5) & 0x3F)) / 2) << 5  |
        ((((sh_n)       & 0x1F) + ((bg_n)        & 0x1F)) / 2)
    );
    /* highlight: blend fg+white 50% */
    uint16_t hi_n = (uint16_t)(
        ((((fg_n >> 11) & 0x1F) + 31) / 2) << 11 |
        ((((fg_n >>  5) & 0x3F) + 63) / 2) << 5  |
        ((((fg_n)       & 0x1F) + 31) / 2)
    );
    uint16_t soft_shadow = (uint16_t)((soft_n >> 8) | (soft_n << 8));
    uint16_t hi          = (uint16_t)((hi_n   >> 8) | (hi_n   << 8));
    /* 3 passes: soft shadow +1+1, highlight +0-1, main */
    for (int pass = 0; pass < 3; ++pass) {
        uint16_t color = pass == 0 ? soft_shadow : (pass == 1 ? hi : fg);
        int dx = pass == 0 ? 1 : 0;
        int dy = pass == 0 ? 1 : (pass == 1 ? -1 : 0);
        for (size_t ci = 0; upper[ci] != '\0'; ++ci) {
            int x0 = x_start + (int)ci * stride;
            if (x0 + glyph_w * scale >= (int)s_display.width) break;
            const glyph5x7_t *glyph = find_glyph(upper[ci]);
            for (int row = 0; row < glyph_h; ++row) {
                for (int col = 0; col < glyph_w; ++col) {
                    if ((glyph->rows[row] & (1U << (glyph_w - 1 - col))) == 0) continue;
                    for (int sy = 0; sy < scale; ++sy) {
                        int py = y_start + row * scale + sy + dy;
                        if (py < 0 || py >= (int)s_display.height) continue;
                        for (int sx = 0; sx < scale; ++sx) {
                            int px = x0 + col * scale + sx + dx;
                            if (px < 0 || px >= (int)s_display.width) continue;
                            s_display.framebuffer[(size_t)py * s_display.width + (size_t)px] = color;
                        }
                    }
                }
            }
        }
    }
}

static void draw_text_band_to_framebuffer(int y, int height, uint16_t bg, uint16_t fg, const char *text, int scale, int x_start)
{
    if (s_display.framebuffer == NULL || height <= 0 || scale <= 0) {
        return;
    }

    fill_framebuffer_rect(0, y, (int)s_display.width, height, bg);

    char upper[CATDEX_UI_LINE_LEN] = {0};
    copy_uppercase(upper, sizeof(upper), text);

    const int glyph_w = 5;
    const int glyph_h = 7;
    const int stride = glyph_w * scale + scale;
    const int y_start = y + (height - glyph_h * scale) / 2;

    for (size_t char_idx = 0; upper[char_idx] != '\0'; ++char_idx) {
        int x0 = x_start + (int)char_idx * stride;
        if (x0 + glyph_w * scale >= (int)s_display.width) {
            break;
        }

        const glyph5x7_t *glyph = find_glyph(upper[char_idx]);
        for (int row = 0; row < glyph_h; ++row) {
            for (int col = 0; col < glyph_w; ++col) {
                if ((glyph->rows[row] & (1U << (glyph_w - 1 - col))) == 0) {
                    continue;
                }
                for (int sy = 0; sy < scale; ++sy) {
                    int py = y_start + row * scale + sy;
                    if (py < 0 || py >= (int)s_display.height) {
                        continue;
                    }
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = x0 + col * scale + sx;
                        if (px < 0 || px >= (int)s_display.width) {
                            continue;
                        }
                        s_display.framebuffer[(size_t)py * s_display.width + (size_t)px] = fg;
                    }
                }
            }
        }
    }
}

/* Draw text band blended over existing framebuffer content (semi-transparent bg) */
static void draw_text_band_blend_to_framebuffer(int y, int height, uint16_t bg, uint16_t fg,
                                                 const char *text, int scale, int x_start, uint8_t alpha)
{
    if (s_display.framebuffer == NULL || height <= 0 || scale <= 0) return;
    /* framebuffer is byte-swapped; unswap before blending, reswap after */
    uint16_t bg_n = (uint16_t)((bg >> 8) | (bg << 8));
    for (int row = 0; row < height; ++row) {
        int py = y + row;
        if (py < 0 || py >= (int)s_display.height) continue;
        for (int col = 0; col < (int)s_display.width; ++col) {
            uint16_t sw = s_display.framebuffer[(size_t)py * s_display.width + (size_t)col];
            uint16_t src = (uint16_t)((sw >> 8) | (sw << 8));
            uint16_t r = (((bg_n >> 11) & 0x1F) * alpha + ((src >> 11) & 0x1F) * (8 - alpha)) / 8;
            uint16_t g = (((bg_n >>  5) & 0x3F) * alpha + ((src >>  5) & 0x3F) * (8 - alpha)) / 8;
            uint16_t b = (((bg_n)       & 0x1F) * alpha + ((src)       & 0x1F) * (8 - alpha)) / 8;
            uint16_t out = (r << 11) | (g << 5) | b;
            s_display.framebuffer[(size_t)py * s_display.width + (size_t)col] = (uint16_t)((out >> 8) | (out << 8));
        }
    }
    char upper[CATDEX_UI_LINE_LEN] = {0};
    copy_uppercase(upper, sizeof(upper), text);
    const int glyph_w = 5, glyph_h = 7;
    const int stride = glyph_w * scale + scale;
    const int y_start = y + (height - glyph_h * scale) / 2;
    for (size_t ci = 0; upper[ci] != '\0'; ++ci) {
        int x0 = x_start + (int)ci * stride;
        if (x0 + glyph_w * scale >= (int)s_display.width) break;
        const glyph5x7_t *glyph = find_glyph(upper[ci]);
        for (int row = 0; row < glyph_h; ++row) {
            for (int col = 0; col < glyph_w; ++col) {
                if ((glyph->rows[row] & (1U << (glyph_w - 1 - col))) == 0) continue;
                for (int sy = 0; sy < scale; ++sy) {
                    int py = y_start + row * scale + sy;
                    if (py < 0 || py >= (int)s_display.height) continue;
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = x0 + col * scale + sx;
                        if (px < 0 || px >= (int)s_display.width) continue;
                        s_display.framebuffer[(size_t)py * s_display.width + (size_t)px] = fg;
                    }
                }
            }
        }
    }
}

static void draw_preview_to_framebuffer(const display_preview_t *preview,
                                        int x,
                                        int y,
                                        int dst_w,
                                        int dst_h,
                                        bool flip_y)
{
    if (s_display.framebuffer == NULL || preview == NULL || !preview->has_preview || preview->rgb565 == NULL) {
        return;
    }

    for (int row = 0; row < dst_h; ++row) {
        int py = y + row;
        if (py < 0 || py >= (int)s_display.height) {
            continue;
        }

        int sample_y = (row * (int)preview->height) / dst_h;
        if (flip_y) {
            sample_y = (int)preview->height - 1 - sample_y;
        }
        if (sample_y < 0) {
            sample_y = 0;
        }
        if (sample_y >= (int)preview->height) {
            sample_y = (int)preview->height - 1;
        }

        for (int col = 0; col < dst_w; ++col) {
            int px = x + col;
            if (px < 0 || px >= (int)s_display.width) {
                continue;
            }

            int sample_x = (col * (int)preview->width) / dst_w;
            if (sample_x < 0) {
                sample_x = 0;
            }
            if (sample_x >= (int)preview->width) {
                sample_x = (int)preview->width - 1;
            }

            s_display.framebuffer[(size_t)py * s_display.width + (size_t)px] =
                preview->rgb565[(size_t)sample_y * (size_t)preview->width + (size_t)sample_x];
        }
    }
}

static void draw_sprite_to_framebuffer(const display_preview_t *preview,
                                       int x, int y, int dst_w, int dst_h)
{
    if (s_display.framebuffer == NULL || preview == NULL || !preview->has_preview || preview->rgb565 == NULL) {
        return;
    }

    for (int row = 0; row < dst_h; ++row) {
        int py = y + row;
        if (py < 0 || py >= (int)s_display.height) continue;
        int sample_y = (row * (int)preview->height) / dst_h;
        if (sample_y >= (int)preview->height) sample_y = (int)preview->height - 1;

        for (int col = 0; col < dst_w; ++col) {
            int px = x + col;
            if (px < 0 || px >= (int)s_display.width) continue;
            int sample_x = (col * (int)preview->width) / dst_w;
            if (sample_x >= (int)preview->width) sample_x = (int)preview->width - 1;

            uint16_t pixel = preview->rgb565[(size_t)sample_y * (size_t)preview->width + (size_t)sample_x];
            /* skip color key (transparent pixels from img2c.py with alpha) */
            uint16_t pixel_native = ((pixel & 0x00FF) << 8) | ((pixel & 0xFF00) >> 8);
            if (pixel_native == 0xF81F || pixel == 0xF81F) continue;
            s_display.framebuffer[(size_t)py * s_display.width + (size_t)px] = pixel;
        }
    }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}

static const glyph5x7_t *find_glyph(char c)
{
    size_t count = sizeof(k_font) / sizeof(k_font[0]);
    for (size_t i = 0; i < count; ++i) {
        if (k_font[i].c == c) {
            return &k_font[i];
        }
    }
    return &k_font[0];
}

static void copy_uppercase(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }

    size_t i = 0;
    for (; i + 1 < dst_len && src != NULL && src[i] != '\0'; ++i) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static void log_frame(const display_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, "%s", frame->title);
    for (size_t i = 0; i < CATDEX_UI_LINE_COUNT; ++i) {
        if (frame->lines[i][0] != '\0') {
            ESP_LOGI(TAG, "%s", frame->lines[i]);
        }
    }
    if (frame->footer[0] != '\0') {
        ESP_LOGI(TAG, "%s", frame->footer);
    }
    ESP_LOGI(TAG, "==============================");
}

static esp_err_t fill_region(uint16_t color, int x, int y, int width, int height)
{
    if (!s_display.lcd_ready || s_display.panel == NULL || s_display.scratch == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t max_rows = s_display.scratch_pixels / (size_t)width;
    if (max_rows == 0) {
        return ESP_ERR_NO_MEM;
    }

    int remaining = height;
    int y_offset = y;
    while (remaining > 0) {
        int rows = remaining > (int)max_rows ? (int)max_rows : remaining;
        size_t pixels = (size_t)width * (size_t)rows;
        ESP_RETURN_ON_ERROR(wait_for_pending_io(), TAG, "panel wait failed");
        for (size_t i = 0; i < pixels; ++i) {
            s_display.scratch[i] = color;
        }

        esp_err_t err = draw_bitmap_sync(x, y_offset, x + width, y_offset + rows, s_display.scratch);
        if (err != ESP_OK) {
            return err;
        }

        y_offset += rows;
        remaining -= rows;
    }

    return ESP_OK;
}

static void remember_frame(const display_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    memcpy(&s_display.last_frame, frame, sizeof(s_display.last_frame));
}

static void render_fullscreen_frame(const display_frame_t *frame)
{
    if (!s_display.lcd_ready || frame == NULL || s_display.framebuffer == NULL) {
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(wait_for_pending_io());

    if (frame->layout == DISPLAY_LAYOUT_CAPTURE) {
        const uint16_t bg         = rgb565(234, 216, 176);
        const uint16_t title_bg   = rgb565(217, 154,  43);
        const uint16_t title_fg   = rgb565(255, 243, 214);
        const uint16_t title_shad = rgb565( 90,  53,  24);
        const uint16_t divider    = rgb565( 90,  53,  24);
        const uint16_t corner_col = rgb565( 90,  53,  24);
        const uint16_t card_bg    = rgb565(255, 243, 214);
        const uint16_t card_edge  = rgb565(184, 154, 106);
        const uint16_t info_fg    = rgb565( 46,  27,  12);
        const uint16_t status_bg  = rgb565( 79, 122,  58);
        const uint16_t status_fg  = rgb565(255, 243, 214);
        const uint16_t footer_bg  = rgb565( 90,  53,  24);
        const uint16_t footer_fg  = rgb565(255, 243, 214);
        const int W = (int)s_display.width;

        fill_framebuffer(bg);
        fill_framebuffer_rect(0, 0, W, 30, title_bg);
        fill_framebuffer_rect(2, 2, W-4, 5, rgb565(255, 220, 120));
        fill_framebuffer_rect(2, 26, W-4, 2, title_shad);
        draw_text_band_to_framebuffer(1, 28, title_bg, title_shad, frame->title, 2, 9);
        draw_text_band_to_framebuffer(0, 28, title_bg, title_fg,   frame->title, 2, 8);
        fill_framebuffer_rect(0, 30, W, 3, divider);

        const int px = k_capture_preview_x, py = k_capture_preview_y;
        const int pw = k_capture_preview_w, ph = k_capture_preview_h;
        const int pb = k_capture_preview_border, cs = 10;
        fill_framebuffer_rect(px-pb, py-pb, cs, pb, corner_col);
        fill_framebuffer_rect(px-pb, py-pb, pb, cs, corner_col);
        fill_framebuffer_rect(px+pw+pb-cs, py-pb, cs, pb, corner_col);
        fill_framebuffer_rect(px+pw, py-pb, pb, cs, corner_col);
        fill_framebuffer_rect(px-pb, py+ph, cs, pb, corner_col);
        fill_framebuffer_rect(px-pb, py+ph-cs, pb, cs, corner_col);
        fill_framebuffer_rect(px+pw+pb-cs, py+ph, cs, pb, corner_col);
        fill_framebuffer_rect(px+pw, py+ph-cs, pb, cs, corner_col);
        fill_framebuffer_rect(px-pb, py-pb, pw+pb*2, 1, card_edge);
        fill_framebuffer_rect(px-pb, py+ph+pb-1, pw+pb*2, 1, card_edge);
        fill_framebuffer_rect(px-pb, py-pb, 1, ph+pb*2, card_edge);
        fill_framebuffer_rect(px+pw+pb-1, py-pb, 1, ph+pb*2, card_edge);

        if (frame->preview.has_preview) {
            draw_preview_to_framebuffer(&frame->preview, px, py, pw, ph, true);
        }

        fill_framebuffer_rect(0, 162, W, 2, divider);
        fill_framebuffer_rect(4, 165, W-8, 18, card_bg);
        fill_framebuffer_rect(4, 165, W-8, 1, card_edge);
        fill_framebuffer_rect(4, 182, W-8, 1, card_edge);
        draw_text_band_to_framebuffer(166, 16, card_bg, info_fg, frame->lines[2], 1, 8);
        fill_framebuffer_rect(4, 184, W-8, 18, card_bg);
        fill_framebuffer_rect(4, 184, W-8, 1, card_edge);
        fill_framebuffer_rect(4, 201, W-8, 1, card_edge);
        draw_text_band_to_framebuffer(185, 16, card_bg, info_fg, frame->lines[3], 1, 8);
        fill_framebuffer_rect(4, 203, W-8, 17, status_bg);
        fill_framebuffer_rect(4, 203, W-8, 2, rgb565(123, 201, 67));
        draw_text_band_to_framebuffer(204, 15, status_bg, status_fg, frame->lines[5], 1, 8);

        fill_framebuffer_rect(0, 220, W, 1, divider);
        fill_framebuffer_rect(0, 221, W, 19, footer_bg);
        draw_text_band_to_framebuffer(222, 17, footer_bg, footer_fg, frame->footer, 1, 8);

    } else if (frame->layout == DISPLAY_LAYOUT_PHOTO_DETAIL) {
        const uint16_t bg         = rgb565(234, 216, 176); /* #EAD8B0 */
        const uint16_t title_bg   = rgb565(217, 154,  43); /* #D99A2B */
        const uint16_t title_fg   = rgb565(255, 243, 214); /* #FFF3D6 */
        const uint16_t title_shad = rgb565( 90,  53,  24); /* #5A3518 */
        const uint16_t border_col = rgb565( 90,  53,  24);
        const uint16_t photo_bg   = rgb565(184, 154, 106); /* #B89A6A */
        const uint16_t line_bg    = rgb565(255, 243, 214);
        const uint16_t line_fg    = rgb565( 46,  27,  12); /* #2E1B0C */
        const uint16_t alt_bg     = rgb565(234, 216, 176);
        const uint16_t footer_bg  = rgb565( 90,  53,  24);
        const uint16_t footer_fg  = rgb565(255, 243, 214);
        const int photo_size = 88;
        const int photo_x = ((int)s_display.width - photo_size) / 2;
        const int photo_y = 30;
        const int line_y[] = {126, 144, 162, 180, 198, 216};
        const int line_h = 16;

        fill_framebuffer(bg);

        /* title bar with shine + shadow */
        fill_framebuffer_rect(0, 0, (int)s_display.width, 28, title_bg);
        fill_framebuffer_rect(2, 2, (int)s_display.width - 4, 5, rgb565(255, 180, 60));
        draw_text_band_to_framebuffer(1, 26, title_bg, title_shad, frame->title, 2, 9);
        draw_text_band_to_framebuffer(0, 26, title_bg, title_fg,   frame->title, 2, 8);
        fill_framebuffer_rect(0, 28, (int)s_display.width, 2, border_col);

        /* photo frame: gold border + inner bg */
        fill_framebuffer_rect(photo_x - 4, photo_y - 4, photo_size + 8, photo_size + 8, border_col);
        fill_framebuffer_rect(photo_x - 2, photo_y - 2, photo_size + 4, photo_size + 4, photo_bg);
        if (frame->preview.has_preview) {
            draw_preview_to_framebuffer(&frame->preview, photo_x, photo_y, photo_size, photo_size, false);
        } else {
            draw_text_band_to_framebuffer(photo_y + 30, 20, photo_bg, title_fg, "NO PHOTO", 2, photo_x + 4);
        }

        /* stat lines */
        fill_framebuffer_rect(0, 124, (int)s_display.width, 2, border_col);
        for (size_t i = 0; i < 6; ++i) {
            uint16_t rb = (i % 2 == 0) ? line_bg : alt_bg;
            fill_framebuffer_rect(0, line_y[i], 3, line_h, border_col);
            draw_text_band_to_framebuffer(line_y[i], line_h, rb, line_fg, frame->lines[i], 1, 8);
        }

        fill_framebuffer_rect(0, 228, (int)s_display.width, 1, border_col);
        draw_text_band_to_framebuffer(229, 11, footer_bg, footer_fg, frame->footer, 1, 8);
    } else if (frame->layout == DISPLAY_LAYOUT_MAIN_MENU) {
        const uint16_t bg        = rgb565(234, 216, 176); /* #EAD8B0 parchment */
        const uint16_t outer_bdr = rgb565( 90,  53,  24); /* #5A3518 dark brown */
        const uint16_t inner_bdr = rgb565(184, 154, 106); /* #B89A6A warm brown */
        const uint16_t title_bg  = rgb565(217, 154,  43); /* #D99A2B gold */
        const uint16_t title_fg  = rgb565(255, 243, 214); /* #FFF3D6 */
        const uint16_t title_shad= rgb565( 90,  53,  24);
        const uint16_t sel_bg    = rgb565( 79, 122,  58); /* #4F7A3A green */
        const uint16_t sel_fg    = rgb565(255, 243, 214);
        const uint16_t sel_shad  = rgb565( 46,  27,  12);
        const uint16_t item_bg   = rgb565(255, 243, 214); /* #FFF3D6 cream */
        const uint16_t item_fg   = rgb565( 46,  27,  12); /* #2E1B0C */
        const uint16_t item_shad = rgb565(184, 154, 106);
        const uint16_t item_acc  = rgb565(217, 154,  43); /* gold left bar */
        const uint16_t footer_bg = rgb565( 90,  53,  24);
        const uint16_t footer_fg = rgb565(255, 243, 214);

        const int W = (int)s_display.width;
        const int H = (int)s_display.height;

        fill_framebuffer(bg);
        fill_framebuffer_rect(0, 0, W, 4, outer_bdr);
        fill_framebuffer_rect(0, H-4, W, 4, outer_bdr);
        fill_framebuffer_rect(0, 0, 4, H, outer_bdr);
        fill_framebuffer_rect(W-4, 0, 4, H, outer_bdr);
        fill_framebuffer_rect(5, 5, W-10, 1, inner_bdr);
        fill_framebuffer_rect(5, H-6, W-10, 1, inner_bdr);
        fill_framebuffer_rect(5, 5, 1, H-10, inner_bdr);
        fill_framebuffer_rect(W-6, 5, 1, H-10, inner_bdr);

        fill_framebuffer_rect(4, 4, W-8, 44, title_bg);
        fill_framebuffer_rect(6, 44, W-12, 2, title_shad);
        /* deep shadow pass at +2,+2 */
        draw_text_band_to_framebuffer(9, 34, title_bg, title_shad, frame->title, 2, 10);
        /* emboss via shadow_to_framebuffer (soft shadow +1+1, highlight -1-1, main) */
        draw_text_shadow_to_framebuffer(7, 34, title_bg, title_fg, title_shad, frame->title, 2, 8);
        fill_framebuffer_rect(4, 48, W-8, 3, outer_bdr);
        fill_framebuffer_rect(4, 51, W-8, 2, title_bg);

        const int item_x = 8, item_w = W-16, item_h = 30, item_gap = 3;
        int item_y = 55;
        for (size_t i = 0; i < CATDEX_UI_LINE_COUNT; ++i) {
            if (frame->lines[i][0] == '\0') break;
            if (item_y + item_h > H - 26) break;
            bool sel = frame->lines[i][0] == '>';
            const char *label = frame->lines[i];
            if (label[0]=='>'||label[0]==' ') label++;
            if (label[0]==' ') label++;
            if (sel) {
                fill_framebuffer_rect(item_x, item_y, item_w, item_h, sel_bg);
                fill_framebuffer_rect(item_x-1, item_y-1, item_w+2, 1, outer_bdr);
                fill_framebuffer_rect(item_x-1, item_y+item_h, item_w+2, 1, outer_bdr);
                draw_text_shadow_to_framebuffer(item_y+3, item_h-6, sel_bg, sel_fg, sel_shad, label, 2, 16);
            } else {
                fill_framebuffer_rect(item_x, item_y, item_w, item_h, item_bg);
                fill_framebuffer_rect(item_x, item_y, 5, item_h, item_acc);
                fill_framebuffer_rect(item_x-1, item_y-1, item_w+2, 1, inner_bdr);
                fill_framebuffer_rect(item_x-1, item_y+item_h, item_w+2, 1, inner_bdr);
                draw_text_shadow_to_framebuffer(item_y+3, item_h-6, item_bg, item_fg, item_shad, label, 2, 16);
            }
            item_y += item_h + item_gap;
        }

        fill_framebuffer_rect(4, H-26, W-8, 2, outer_bdr);
        fill_framebuffer_rect(4, H-24, W-8, 20, footer_bg);
        draw_text_band_to_framebuffer(H-22, 18, footer_bg, footer_fg, frame->footer, 1, 8);

    } else if (frame->layout == DISPLAY_LAYOUT_CAPTURE_RESULT) {
        /* Scheme 4: capture success — green + gold + parchment */
        const uint16_t bg         = rgb565(234, 216, 176); /* #EAD8B0 */
        const uint16_t title_bg   = rgb565( 79, 122,  58); /* #4F7A3A green */
        const uint16_t title_fg   = rgb565(255, 243, 214); /* #FFF3D6 */
        const uint16_t title_shad = rgb565( 46,  27,  12); /* #2E1B0C */
        const uint16_t border_col = rgb565(217, 154,  43); /* #D99A2B gold */
        const uint16_t panel_bg   = rgb565(255, 243, 214);
        const uint16_t name_fg    = rgb565( 79, 122,  58);
        const uint16_t stat_fg    = rgb565( 46,  27,  12);
        const uint16_t footer_bg  = rgb565( 90,  53,  24); /* #5A3518 */
        const uint16_t footer_fg  = rgb565(255, 243, 214);
        const uint16_t star_gold  = rgb565(217, 154,  43);
        const uint16_t star_green = rgb565(123, 201,  67);
        const int W = (int)s_display.width;
        const int H = (int)s_display.height;

        fill_framebuffer(bg);

        fill_framebuffer_rect(0, 0, W, 30, title_bg);
        fill_framebuffer_rect(2, 2, W-4, 5, rgb565(123,201,67));
        fill_framebuffer_rect(0, 30, W, 3, border_col);
        draw_text_band_to_framebuffer(1, 28, title_bg, title_shad, frame->title, 2, 9);
        draw_text_band_to_framebuffer(0, 28, title_bg, title_fg,   frame->title, 2, 8);

        const int photo_size = 100;
        const int photo_x = (W - photo_size) / 2;
        const int photo_y = 40;
        fill_framebuffer_rect(photo_x-5, photo_y-5, photo_size+10, photo_size+10, border_col);
        fill_framebuffer_rect(photo_x-3, photo_y-3, photo_size+6,  photo_size+6,  panel_bg);
        if (frame->preview.has_preview) {
            draw_preview_to_framebuffer(&frame->preview, photo_x, photo_y, photo_size, photo_size, false);
        }

        const int sp[][2] = {{photo_x-8,photo_y-2},{photo_x+photo_size+4,photo_y-2},
                             {photo_x-8,photo_y+photo_size-4},{photo_x+photo_size+4,photo_y+photo_size-4}};
        for (int s = 0; s < 4; ++s)
            fill_framebuffer_rect(sp[s][0], sp[s][1], 4, 4, (s%2==0) ? star_gold : star_green);

        const int info_y = photo_y + photo_size + 10;
        fill_framebuffer_rect(4, info_y-2, W-8, 2, border_col);
        fill_framebuffer_rect(4, info_y,   W-8, 56, panel_bg);
        fill_framebuffer_rect(4, info_y+56, W-8, 1, rgb565(184,154,106));
        draw_text_band_to_framebuffer(info_y+2,  18, panel_bg, name_fg, frame->lines[0], 2, 8);
        draw_text_band_to_framebuffer(info_y+22, 16, panel_bg, stat_fg, frame->lines[1], 1, 8);
        draw_text_band_to_framebuffer(info_y+40, 16, panel_bg, stat_fg, frame->lines[2], 1, 8);

        fill_framebuffer_rect(0, H-18, W, 2, border_col);
        fill_framebuffer_rect(0, H-16, W, 16, footer_bg);
        draw_text_band_to_framebuffer(H-15, 15, footer_bg, footer_fg, frame->footer, 1, 8);

    } else if (frame->layout == DISPLAY_LAYOUT_CAPTURE_CUSTOM ||
               frame->layout == DISPLAY_LAYOUT_DETAIL_CUSTOM) {
        /* --- Custom layout: optional full-screen bg image + overlay --- */
        if (frame->bg_image_rgb565 != NULL) {
            /* blit 240x240 RGB565 array directly into framebuffer */
            memcpy(s_display.framebuffer, frame->bg_image_rgb565,
                   s_display.framebuffer_pixels * sizeof(uint16_t));
        } else {
            fill_framebuffer(rgb565(234, 216, 176)); /* #EAD8B0 parchment */
        }

        if (frame->layout == DISPLAY_LAYOUT_CAPTURE_CUSTOM) {
            if (frame->preview.has_preview) {
                draw_preview_to_framebuffer(&frame->preview,
                                            k_capture_preview_x, k_capture_preview_y,
                                            k_capture_preview_w, k_capture_preview_h, true);
            }
            const uint16_t band_bg = rgb565( 46,  27,  12); /* #2E1B0C dark brown */
            const uint16_t band_fg = rgb565(255, 243, 214); /* #FFF3D6 parchment */
            const uint16_t gold_fg = rgb565(217, 154,  43); /* #D99A2B gold */
            draw_text_band_blend_to_framebuffer(168, 16, band_bg, band_fg, frame->lines[2], 1, 8, 6);
            draw_text_band_blend_to_framebuffer(186, 16, band_bg, band_fg, frame->lines[3], 1, 8, 6);
            draw_text_band_blend_to_framebuffer(204, 16, band_bg, gold_fg, frame->lines[5], 1, 8, 6);
            draw_text_band_blend_to_framebuffer(224, 16, band_bg, band_fg, frame->footer,   1, 8, 6);
        } else {
            /* DETAIL_CUSTOM: full-screen photo + light semi-transparent bands + embossed text */
            const uint16_t band_bg = rgb565(120,  72,  24); /* deep amber */
            const uint16_t text_fg = rgb565(255, 230, 160); /* warm cream */
            const uint16_t gold_fg = rgb565(255, 210,  80); /* bright gold */
            const uint16_t shad    = rgb565( 60,  30,   8); /* very dark brown */

            if (frame->preview.has_preview) {
                draw_preview_to_framebuffer(&frame->preview, 0, 0,
                                            (int)s_display.width, (int)s_display.height, false);
            }

            if (frame->title[0]    != '\0') draw_text_band_blend_to_framebuffer(  0, 18, band_bg, text_fg, frame->title,    2, 8, 7);
            if (frame->lines[0][0] != '\0') draw_text_band_blend_to_framebuffer( 20, 16, band_bg, text_fg, frame->lines[0], 1, 8, 7);
            if (frame->lines[1][0] != '\0') draw_text_band_blend_to_framebuffer( 38, 16, band_bg, gold_fg, frame->lines[1], 1, 8, 6);
            if (frame->lines[2][0] != '\0') draw_text_band_blend_to_framebuffer(158, 16, band_bg, text_fg, frame->lines[2], 1, 8, 7);
            if (frame->lines[3][0] != '\0') draw_text_band_blend_to_framebuffer(176, 16, band_bg, text_fg, frame->lines[3], 1, 8, 7);
            if (frame->lines[4][0] != '\0') draw_text_band_blend_to_framebuffer(194, 16, band_bg, text_fg, frame->lines[4], 1, 8, 7);
            if (frame->lines[5][0] != '\0') draw_text_band_blend_to_framebuffer(212, 16, band_bg, gold_fg, frame->lines[5], 1, 8, 6);
            if (frame->footer[0]   != '\0') draw_text_band_blend_to_framebuffer(228, 12, band_bg, shad,    frame->footer,   1, 8, 7);
        }

    } else if (frame->layout == DISPLAY_LAYOUT_IMAGE_ONLY) {
        if (frame->bg_image_rgb565 != NULL) {
            memcpy(s_display.framebuffer, frame->bg_image_rgb565,
                   s_display.framebuffer_pixels * sizeof(uint16_t));
        } else {
            fill_framebuffer(rgb565(234, 216, 176));
        }
    } else {
        /* STANDARD — vintage magic tome palette */
        const uint16_t bg         = rgb565(234, 216, 176); /* #EAD8B0 */
        const uint16_t title_bg   = rgb565(217, 154,  43); /* #D99A2B gold */
        const uint16_t title_fg   = rgb565(255, 243, 214); /* #FFF3D6 */
        const uint16_t title_shad = rgb565( 90,  53,  24); /* #5A3518 */
        const uint16_t divider    = rgb565( 90,  53,  24);
        const uint16_t card_bg    = rgb565(255, 243, 214); /* #FFF3D6 */
        const uint16_t card_alt   = rgb565(234, 216, 176); /* #EAD8B0 */
        const uint16_t card_edge  = rgb565(184, 154, 106); /* #B89A6A */
        const uint16_t line_fg    = rgb565( 46,  27,  12); /* #2E1B0C */
        const uint16_t sel_bg     = rgb565( 79, 122,  58); /* #4F7A3A green */
        const uint16_t sel_fg     = rgb565(255, 243, 214);
        const uint16_t sel_acc    = rgb565( 46,  27,  12);
        const uint16_t footer_bg  = rgb565( 90,  53,  24);
        const uint16_t footer_fg  = rgb565(255, 243, 214);

        fill_framebuffer(bg);

        fill_framebuffer_rect(0, 0, (int)s_display.width, 30, title_bg);
        fill_framebuffer_rect(2, 2, (int)s_display.width-4, 5, rgb565(255, 220, 120));
        fill_framebuffer_rect(2, 26, (int)s_display.width-4, 2, title_shad);
        draw_text_band_to_framebuffer(1, 28, title_bg, title_shad, frame->title, 2, 9);
        draw_text_band_to_framebuffer(0, 28, title_bg, title_fg,   frame->title, 2, 8);
        fill_framebuffer_rect(0, 30, (int)s_display.width, 3, divider);

        for (size_t i = 0; i < CATDEX_UI_LINE_COUNT; ++i) {
            int y = 33 + (int)i * 22;
            if (y + 22 > (int)s_display.height - 18) break;
            bool sel = frame->lines[i][0] == '>';
            if (sel) {
                fill_framebuffer_rect(0, y, (int)s_display.width, 22, sel_bg);
                fill_framebuffer_rect(0, y, 5, 22, sel_acc);
                fill_framebuffer_rect(0, y, (int)s_display.width, 2, rgb565(123,201,67));
                draw_text_band_to_framebuffer(y+3, 16, sel_bg, sel_fg, frame->lines[i], 1, 10);
            } else {
                uint16_t rb = (i % 2 == 0) ? card_bg : card_alt;
                fill_framebuffer_rect(0, y, (int)s_display.width, 22, rb);
                fill_framebuffer_rect(0, y, 4, 22, title_bg);
                fill_framebuffer_rect(0, y+21, (int)s_display.width, 1, card_edge);
                draw_text_band_to_framebuffer(y+3, 16, rb, line_fg, frame->lines[i], 1, 8);
            }
        }

        fill_framebuffer_rect(0, (int)s_display.height-18, (int)s_display.width, 2, divider);
        fill_framebuffer_rect(0, (int)s_display.height-16, (int)s_display.width, 16, footer_bg);
        draw_text_band_to_framebuffer((int)s_display.height-15, 15, footer_bg, footer_fg, frame->footer, 1, 8);
    }

    /* overlay layers: blit each display_overlay_t onto framebuffer in order */
    for (uint8_t oi = 0; oi < frame->overlay_count && oi < DISPLAY_MAX_OVERLAYS; ++oi) {
        const display_overlay_t *ov = &frame->overlays[oi];
        if (ov->rgb565 == NULL || ov->src_w == 0 || ov->src_h == 0 || ov->dst_w == 0 || ov->dst_h == 0) continue;
        display_preview_t tmp = { .has_preview = true, .rgb565 = ov->rgb565, .width = ov->src_w, .height = ov->src_h };
        draw_sprite_to_framebuffer(&tmp, ov->dst_x, ov->dst_y, ov->dst_w, ov->dst_h);
    }

    {
    int rows_per_chunk = (int)(s_display.scratch_pixels / s_display.width);
    if (rows_per_chunk < 1) rows_per_chunk = 1;

    for (int y = 0; y < (int)s_display.height; y += rows_per_chunk) {
        int rows = rows_per_chunk;
        if (y + rows > (int)s_display.height) {
            rows = (int)s_display.height - y;
        }

        size_t pixels = (size_t)rows * s_display.width;

        // 关键：复用 scratch 前，先等前一笔 SPI DMA/队列传输结束
        ESP_ERROR_CHECK_WITHOUT_ABORT(wait_for_pending_io());

        memcpy(s_display.scratch,
               s_display.framebuffer + (size_t)y * s_display.width,
               pixels * sizeof(uint16_t));

        ESP_ERROR_CHECK_WITHOUT_ABORT(
            draw_bitmap_sync(0, y, (int)s_display.width, y + rows, s_display.scratch)
        );
    }

    // 等最后一块也真正刷完
    ESP_ERROR_CHECK_WITHOUT_ABORT(wait_for_pending_io());
}
}

static esp_err_t init_real_lcd(void)
{
    const board_profile_t *profile = board_profile_get_active();
    if (profile == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const bsp_display_config_t bsp_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * 40 * (int)sizeof(uint16_t),
    };

    esp_err_t err = bsp_display_new(&bsp_cfg, &s_display.panel, &s_display.io);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_panel_set_gap(s_display.panel, 0, 0);

    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_display.panel, true), TAG, "panel power-on failed");
    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), TAG, "backlight enable failed");

    s_display.width = BSP_LCD_H_RES;
    s_display.height = BSP_LCD_V_RES;
    s_display.backlight_on_level = 1;
    s_display.framebuffer_pixels = s_display.width * s_display.height;
    s_display.scratch_pixels = s_display.width * k_lcd_scratch_rows;
    s_display.capture_band_pixels = s_display.width * 24U;
    s_display.framebuffer = heap_caps_malloc(s_display.framebuffer_pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (s_display.framebuffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_display.scratch = heap_caps_malloc(s_display.scratch_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_display.scratch == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_display.capture_preview_buffer =
        heap_caps_malloc((size_t)(k_capture_preview_w + k_capture_preview_border * 2) *
                             (size_t)(k_capture_preview_h + k_capture_preview_border * 2) * sizeof(uint16_t),
                         MALLOC_CAP_SPIRAM);
    if (s_display.capture_preview_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < 5; ++i) {
        s_display.capture_band_buffers[i] =
            heap_caps_malloc(s_display.capture_band_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (s_display.capture_band_buffers[i] == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_display.lcd_ready = true;
    snprintf(s_display.backend_name, sizeof(s_display.backend_name), "%s", "lcd-bsp");
    ESP_LOGI(TAG,
             "LCD backend ready: %ux%u via BSP MOSI=%d SCLK=%d DC=%d CS=%d BL=%d",
             (unsigned)s_display.width,
             (unsigned)s_display.height,
             profile->lcd.mosi,
             profile->lcd.sclk,
             profile->lcd.dc,
             profile->lcd.cs,
             profile->lcd.backlight);

    if (k_lcd_smoke_test_mode) {
        run_lcd_smoke_test();
    }

    return ESP_OK;
}

static void run_lcd_smoke_test(void)
{
    const uint16_t white = rgb565(255, 255, 255);
    const uint16_t black = rgb565(0, 0, 0);
    const uint16_t red = rgb565(255, 0, 0);
    const uint16_t green = rgb565(0, 255, 0);
    const uint16_t blue = rgb565(0, 0, 255);
    const struct {
        const char *name;
        uint16_t color;
        uint32_t hold_ms;
    } steps[] = {
        {"white-1", white, 2000},
        {"black", black, 1200},
        {"white-2", white, 2000},
        {"red", red, 1000},
        {"green", green, 1000},
        {"blue", blue, 1000},
        {"white-final", white, 0},
    };
    size_t pixels = s_display.width * s_display.height;
    uint16_t *full_frame = NULL;

    ESP_LOGI(TAG, "LCD smoke test enabled");
    ESP_LOGI(TAG, "Backlight active-high config: %s", CATDEX_DISPLAY_BACKLIGHT_ACTIVE_HIGH ? "true" : "false");
    ESP_LOGI(TAG, "LCD smoke test: forcing backlight HIGH for entire test");
    set_backlight_level(1);

    full_frame = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (full_frame == NULL) {
        ESP_LOGE(TAG, "LCD smoke test: full-frame buffer alloc failed");
        return;
    }

    for (size_t step = 0; step < sizeof(steps) / sizeof(steps[0]); ++step) {
        ESP_LOGI(TAG, "LCD smoke test: %s", steps[step].name);
        for (size_t i = 0; i < pixels; ++i) {
            full_frame[i] = steps[step].color;
        }

        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_draw_bitmap(s_display.panel,
                                                                0,
                                                                0,
                                                                (int)s_display.width,
                                                                (int)s_display.height,
                                                                full_frame));
        if (steps[step].hold_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(steps[step].hold_ms));
        }
    }

    free(full_frame);
    ESP_LOGI(TAG, "LCD smoke test complete: final state should remain white");
}

static void set_backlight_level(int level)
{
    if (level > 0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_on());
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_off());
    }
}

void display_hal_init(void)
{
    const board_profile_t *profile = board_profile_get_active();
    char lcd_desc[64] = {0};
    board_profile_describe_lcd(lcd_desc, sizeof(lcd_desc));

    memset(&s_display, 0, sizeof(s_display));
    s_display.lcd_mutex = xSemaphoreCreateMutex();
    configASSERT(s_display.lcd_mutex);
    snprintf(s_display.backend_name, sizeof(s_display.backend_name), "%s", "serial-only");

    ESP_LOGI(TAG, "Active board: %s", profile != NULL ? profile->name : "unknown");
    ESP_LOGI(TAG, "LCD connector: %s", lcd_desc);

#if CATDEX_DISPLAY_TEXT_LOG_ONLY
    ESP_LOGI(TAG, "Display HAL initialized in text placeholder mode");
    ESP_LOGI(TAG, "LCD drawing remains on serial logs because text-only backend is enabled");
#else
    esp_err_t err = init_real_lcd();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LCD init failed, falling back to serial logs: %s", esp_err_to_name(err));
        snprintf(s_display.backend_name, sizeof(s_display.backend_name), "%s", "lcd-fallback");
    }
#endif
}

void display_hal_present(const display_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    if (CATDEX_DISPLAY_LOG_MIRROR || !s_display.lcd_ready) {
        log_frame(frame);
    }

    if (k_lcd_smoke_test_mode) {
        return;
    }

    if (s_display.lcd_ready) {
        xSemaphoreTake(s_display.lcd_mutex, portMAX_DELAY);
        render_fullscreen_frame(frame);
        remember_frame(frame);
        xSemaphoreGive(s_display.lcd_mutex);
    }
}

bool display_hal_is_lcd_ready(void)
{
    return s_display.lcd_ready;
}

const char *display_hal_backend_name(void)
{
    return s_display.backend_name;
}

void display_hal_blit_preview(const uint16_t *rgb565, uint16_t src_w, uint16_t src_h)
{
    if (!s_display.lcd_ready || rgb565 == NULL || src_w == 0 || src_h == 0) return;
    if (s_display.capture_preview_buffer == NULL) return;

    const int dx = k_capture_preview_x;
    const int dy = k_capture_preview_y;
    const int dw = k_capture_preview_w;
    const int dh = k_capture_preview_h;
    uint16_t *buf = s_display.capture_preview_buffer;

    for (int row = 0; row < dh; ++row) {
        int sy = (int)src_h - 1 - (row * (int)src_h / dh);
        if (sy < 0) sy = 0;
        if (sy >= (int)src_h) sy = (int)src_h - 1;
        for (int col = 0; col < dw; ++col) {
            int sx = col * (int)src_w / dw;
            if (sx >= (int)src_w) sx = (int)src_w - 1;
            buf[row * dw + col] = rgb565[sy * src_w + sx];
        }
    }

    xSemaphoreTake(s_display.lcd_mutex, portMAX_DELAY);
    ESP_ERROR_CHECK_WITHOUT_ABORT(wait_for_pending_io());
    ESP_ERROR_CHECK_WITHOUT_ABORT(draw_bitmap_sync(dx, dy, dx + dw, dy + dh, buf));
    ESP_ERROR_CHECK_WITHOUT_ABORT(wait_for_pending_io());
    xSemaphoreGive(s_display.lcd_mutex);
}

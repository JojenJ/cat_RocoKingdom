#include "camera/camera_service.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "drivers/board_profile.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

#if __has_include("esp_camera.h")
#define CATDEX_HAS_ESP32_CAMERA 1
#include "esp_camera.h"
#include "sensor.h"
#else
#define CATDEX_HAS_ESP32_CAMERA 0
#endif

static const char *TAG = "camera_service";
static const uint32_t k_camera_frame_log_interval = 20;
static const struct {
    uint16_t width;
    uint16_t height;
} k_preview_fallback_sizes[] = {
    {160, 120},
    {96, 72},
    {80, 60},
    {64, 48},
};

static bool camera_psram_available(void)
{
#if CONFIG_SPIRAM
    return esp_psram_is_initialized();
#else
    return false;
#endif
}

#if CATDEX_HAS_ESP32_CAMERA
static bool s_camera_driver_initialized;

static const char *camera_sensor_name(uint16_t pid)
{
    switch (pid) {
        case OV2640_PID:
            return "OV2640";
        default:
            return "CAM";
    }
}

static void update_status_text(camera_service_t *camera, const char *sensor_name)
{
    if (camera == NULL) {
        return;
    }

    snprintf(camera->status_text,
             sizeof(camera->status_text),
             "%s %ux%u f=%lu",
             sensor_name != NULL ? sensor_name : "CAM",
             (unsigned)camera->last_width,
             (unsigned)camera->last_height,
             (unsigned long)camera->frame_counter);
}

static esp_err_t ensure_preview_buffer(camera_service_t *camera, uint16_t width, uint16_t height)
{
    uint16_t *new_buffer = NULL;

    if (camera == NULL || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pixels = (size_t)width * (size_t)height;
    if (camera->preview_rgb565 != NULL && camera->preview_pixels == pixels) {
        camera->preview_width = width;
        camera->preview_height = height;
        return ESP_OK;
    }

    if (camera->preview_rgb565 != NULL) {
        heap_caps_free(camera->preview_rgb565);
        camera->preview_rgb565 = NULL;
    }

    new_buffer = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_8BIT);
    if (new_buffer == NULL) {
        camera->preview_pixels = 0;
        camera->preview_width = 0;
        camera->preview_height = 0;
        camera->preview_available = false;
        ESP_LOGW(TAG,
                 "Preview buffer alloc failed: %ux%u (%lu bytes), free=%lu largest=%lu",
                 (unsigned)width,
                 (unsigned)height,
                 (unsigned long)(pixels * sizeof(uint16_t)),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return ESP_ERR_NO_MEM;
    }

    memset(new_buffer, 0, pixels * sizeof(uint16_t));
    camera->preview_rgb565 = new_buffer;
    camera->preview_pixels = pixels;
    camera->preview_width = width;
    camera->preview_height = height;
    return ESP_OK;
}

static void update_preview_from_fb(camera_service_t *camera, const void *fb_buf, size_t fb_len, uint16_t width, uint16_t height)
{
    const uint16_t *src = (const uint16_t *)fb_buf;
    uint16_t dst_width = 0;
    uint16_t dst_height = 0;
    bool allocated = false;

    if (camera == NULL || fb_buf == NULL) {
        return;
    }

    size_t needed = (size_t)width * (size_t)height * sizeof(uint16_t);
    if (fb_len < needed) {
        camera->preview_available = false;
        return;
    }

    for (size_t i = 0; i < sizeof(k_preview_fallback_sizes) / sizeof(k_preview_fallback_sizes[0]); ++i) {
        dst_width = width > k_preview_fallback_sizes[i].width ? k_preview_fallback_sizes[i].width : width;
        dst_height = height > k_preview_fallback_sizes[i].height ? k_preview_fallback_sizes[i].height : height;

        if (ensure_preview_buffer(camera, dst_width, dst_height) == ESP_OK) {
            allocated = true;
            if (i > 0) {
                ESP_LOGW(TAG,
                         "Preview buffer downgraded to %ux%u for source %ux%u",
                         (unsigned)dst_width,
                         (unsigned)dst_height,
                         (unsigned)width,
                         (unsigned)height);
            }
            break;
        }
    }

    if (!allocated) {
        ESP_LOGW(TAG, "Preview buffer allocation failed for %ux%u after fallbacks", width, height);
        return;
    }

    for (uint16_t row = 0; row < dst_height; ++row) {
        uint16_t sample_y = (uint16_t)(((uint32_t)row * height) / dst_height);
        if (sample_y >= height) {
            sample_y = height - 1;
        }

        for (uint16_t col = 0; col < dst_width; ++col) {
            uint16_t sample_x = (uint16_t)(((uint32_t)col * width) / dst_width);
            if (sample_x >= width) {
                sample_x = width - 1;
            }

            camera->preview_rgb565[(size_t)row * dst_width + col] =
                src[(size_t)sample_y * width + sample_x];
        }
    }
    camera->preview_available = true;
}

static esp_err_t init_real_camera(camera_service_t *camera)
{
    const board_profile_t *profile = board_profile_get_active();
    if (camera == NULL || profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    camera_config_t config = {
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
        .pin_d0 = profile->camera.data_y2,
        .pin_d1 = profile->camera.data_y3,
        .pin_d2 = profile->camera.data_y4,
        .pin_d3 = profile->camera.data_y5,
        .pin_d4 = profile->camera.data_y6,
        .pin_d5 = profile->camera.data_y7,
        .pin_d6 = profile->camera.data_y8,
        .pin_d7 = profile->camera.data_y9,
        .pin_xclk = profile->camera.xclk,
        .pin_pclk = profile->camera.pclk,
        .pin_vsync = profile->camera.vsync,
        .pin_href = profile->camera.href,
        .pin_sccb_sda = profile->camera.sccb_sda,
        .pin_sccb_scl = profile->camera.sccb_scl,
        .pin_pwdn = profile->camera.pwdn,
        .pin_reset = profile->camera.reset,
        .xclk_freq_hz = 20000000,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QQVGA,
        .jpeg_quality = 12,
        .fb_count = 1,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .fb_location = camera_psram_available() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        return err;
    }

    s_camera_driver_initialized = true;
    camera->camera_ready = true;
    camera->using_real_camera = true;

    sensor_t *sensor = esp_camera_sensor_get();
    const char *sensor_name = sensor != NULL ? camera_sensor_name(sensor->id.PID) : "CAM";

    if (sensor != NULL) {
        sensor->set_framesize(sensor, config.frame_size);
        sensor->set_pixformat(sensor, PIXFORMAT_RGB565);
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb != NULL) {
        camera->last_width = fb->width;
        camera->last_height = fb->height;
        camera->last_frame_len = fb->len;
        camera->frame_counter = 1;
        update_preview_from_fb(camera, fb->buf, fb->len, fb->width, fb->height);
        esp_camera_fb_return(fb);
    }

    update_status_text(camera, sensor_name);
    ESP_LOGI(TAG,
             "Real camera ready: sensor=%s reset=%d pwdn=%d fb=%s",
             sensor_name,
             profile->camera.reset,
             profile->camera.pwdn,
             camera_psram_available() ? "psram" : "dram");
    return ESP_OK;
}
#endif

static void init_mock_camera(camera_service_t *camera)
{
    if (camera == NULL) {
        return;
    }

    char camera_desc[48] = {0};
    board_profile_describe_camera(camera_desc, sizeof(camera_desc));
    camera->camera_ready = false;
    camera->using_real_camera = false;
    camera->frame_counter = 0;
    camera->frame_failures = 0;
    camera->last_width = 0;
    camera->last_height = 0;
    camera->last_frame_len = 0;
    camera->preview_available = false;
    camera->preview_width = 0;
    camera->preview_height = 0;
    snprintf(camera->status_text, sizeof(camera->status_text), "%s", camera_desc);
}

esp_err_t camera_service_init(camera_service_t *camera)
{
    if (camera == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(camera, 0, sizeof(*camera));
    camera->init_attempted = true;

#if CATDEX_HAS_ESP32_CAMERA
    esp_err_t err = init_real_camera(camera);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Real camera init failed: %s", esp_err_to_name(err));
#endif

    init_mock_camera(camera);
    ESP_LOGI(TAG, "Using mock camera status backend");
    return ESP_OK;
}

void camera_service_poll(camera_service_t *camera)
{
    if (camera == NULL) {
        return;
    }

#if CATDEX_HAS_ESP32_CAMERA
    if (!camera->using_real_camera || !camera->camera_ready || !s_camera_driver_initialized) {
        return;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        camera->frame_failures++;
        snprintf(camera->status_text,
                 sizeof(camera->status_text),
                 "CAM frame fail x%lu",
                 (unsigned long)camera->frame_failures);
        return;
    }

    camera->frame_counter++;
    camera->last_width = fb->width;
    camera->last_height = fb->height;
    camera->last_frame_len = fb->len;

    if ((camera->frame_counter % k_camera_frame_log_interval) == 0) {
        ESP_LOGI(TAG,
                 "fb=%p width=%u height=%u len=%lu",
                 (void *)fb->buf,
                 (unsigned)fb->width,
                 (unsigned)fb->height,
                 (unsigned long)fb->len);
    }

    update_preview_from_fb(camera, fb->buf, fb->len, fb->width, fb->height);

    sensor_t *sensor = esp_camera_sensor_get();
    const char *sensor_name = sensor != NULL ? camera_sensor_name(sensor->id.PID) : "CAM";
    update_status_text(camera, sensor_name);
    esp_camera_fb_return(fb);
#else
    (void)camera;
#endif
}

void camera_service_get_status(const camera_service_t *camera, char *buffer, size_t buffer_len)
{
    if (camera == NULL || buffer == NULL || buffer_len == 0) {
        return;
    }

    snprintf(buffer, buffer_len, "%s", camera->status_text);
}

bool camera_service_get_preview_rgb565(const camera_service_t *camera,
                                       const uint16_t **pixels,
                                       uint16_t *width,
                                       uint16_t *height)
{
    if (camera == NULL || pixels == NULL || width == NULL || height == NULL) {
        return false;
    }

    if (!camera->preview_available || camera->preview_rgb565 == NULL) {
        *pixels = NULL;
        *width = 0;
        *height = 0;
        return false;
    }

    *pixels = camera->preview_rgb565;
    *width = camera->preview_width;
    *height = camera->preview_height;
    return true;
}

bool camera_service_build_thumbnail_rgb565(const camera_service_t *camera,
                                           uint16_t *dst_rgb565,
                                           uint16_t dst_width,
                                           uint16_t dst_height)
{
    int crop_size = 0;
    int src_x = 0;
    int src_y = 0;

    if (camera == NULL || dst_rgb565 == NULL || dst_width == 0 || dst_height == 0 ||
        !camera->preview_available || camera->preview_rgb565 == NULL) {
        return false;
    }

    crop_size = camera->preview_width < camera->preview_height ? (int)camera->preview_width : (int)camera->preview_height;
    src_x = ((int)camera->preview_width - crop_size) / 2;
    src_y = ((int)camera->preview_height - crop_size) / 2;

    for (uint16_t row = 0; row < dst_height; ++row) {
        int sample_y = src_y + crop_size - 1 - (((int)row * crop_size) / (int)dst_height);
        if (sample_y < 0) {
            sample_y = 0;
        }
        if (sample_y >= (int)camera->preview_height) {
            sample_y = (int)camera->preview_height - 1;
        }

        for (uint16_t col = 0; col < dst_width; ++col) {
            int sample_x = src_x + (((int)col * crop_size) / (int)dst_width);
            if (sample_x < 0) {
                sample_x = 0;
            }
            if (sample_x >= (int)camera->preview_width) {
                sample_x = (int)camera->preview_width - 1;
            }

            dst_rgb565[(size_t)row * dst_width + col] =
                camera->preview_rgb565[(size_t)sample_y * camera->preview_width + (size_t)sample_x];
        }
    }

    return true;
}

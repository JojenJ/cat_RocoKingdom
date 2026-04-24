#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "ai/classifier.h"
#include "camera/camera_service.h"
#include "drivers/input_hal.h"
#include "game/game_service.h"
#include "storage/save_storage.h"
#include "ui/ui_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

typedef struct {
    app_page_t current_page;
    size_t selected_menu_index;
    size_t selected_backpack_index;
    size_t cat_detail_tab_index;
    classifier_t classifier;
    classifier_result_t current_prediction;
    camera_service_t camera;
    save_storage_t storage;
    game_service_t game;
    uint16_t detail_photo_rgb565[CATDEX_THUMB_WIDTH * CATDEX_THUMB_HEIGHT];
    bool detail_photo_available;
    CapturedCat last_captured;
    int64_t toast_expires_at_us;
    char toast_message[CATDEX_UI_LINE_LEN];
    char capture_fail_reason[CATDEX_UI_LINE_LEN];
    TaskHandle_t camera_task;
    TaskHandle_t ai_task;
    TaskHandle_t render_task;
    SemaphoreHandle_t frame_mutex;    /* protects camera.preview_rgb565 */
    SemaphoreHandle_t predict_mutex;  /* protects current_prediction */
} app_controller_t;

esp_err_t app_controller_init(app_controller_t *controller);
esp_err_t app_controller_run(app_controller_t *controller);

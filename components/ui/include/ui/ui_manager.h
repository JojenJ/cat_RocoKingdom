#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ai/classifier.h"
#include "camera/camera_service.h"
#include "common/project_types.h"
#include "drivers/input_hal.h"

typedef enum {
    APP_PAGE_MAIN_MENU = 0,
    APP_PAGE_CAPTURE,
    APP_PAGE_DEX,
    APP_PAGE_BACKPACK,
    APP_PAGE_CAT_DETAIL,
    APP_PAGE_SYSTEM_INFO,
    APP_PAGE_SETTINGS,
    APP_PAGE_CAPTURE_RESULT,
    APP_PAGE_CAPTURE_FAIL,
} app_page_t;

typedef struct {
    app_page_t current_page;
    size_t selected_menu_index;
    const classifier_result_t *prediction;
    const GameSaveData *save_data;
    const CapturedCat *latest_capture;
    const CapturedCat *selected_cat;
    const camera_service_t *camera;
    const uint16_t *preview_rgb565;
    uint16_t preview_width;
    uint16_t preview_height;
    const uint16_t *detail_photo_rgb565;
    uint16_t detail_photo_width;
    uint16_t detail_photo_height;
    size_t selected_backpack_index;
    size_t cat_detail_tab_index;
    input_debug_snapshot_t input_debug;
    bool demo_mode_enabled;
    bool detail_photo_available;
    bool classifier_backend_ready;
    const char *classifier_backend_name;
    const CapturedCat *last_captured;
    char toast_message[CATDEX_UI_LINE_LEN];
    char capture_fail_reason[CATDEX_UI_LINE_LEN];
} ui_view_model_t;

void ui_manager_init(void);
void ui_manager_render(const ui_view_model_t *model);

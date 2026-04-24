#include "app/app_controller.h"

#include <stdio.h>
#include <string.h>

#include "drivers/display_hal.h"
#include "ai/classifier.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "app_controller";
/* When true, render task blits the 96x96 model input instead of camera preview */
static volatile bool s_show_model_input = false;
static const char *k_toast_capture_fail = "Capture failed";
static const size_t k_cat_detail_tab_count = 2;

static esp_err_t ensure_capture_services_ready(app_controller_t *controller)
{
    if (controller == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (controller->camera.init_attempted) {
        return ESP_OK;
    }

    esp_err_t camera_err = camera_service_init(&controller->camera);
    if (camera_err != ESP_OK) {
        ESP_LOGW(TAG, "Camera service init returned: %s", esp_err_to_name(camera_err));
    }

    return ESP_OK;
}

/* Camera task (Core 1): poll camera only. Never touches LCD or AI. */
static void camera_task_fn(void *arg)
{
    app_controller_t *c = (app_controller_t *)arg;
    while (true) {
        if (c->current_page == APP_PAGE_CAPTURE) {
            ensure_capture_services_ready(c);
            xSemaphoreTake(c->frame_mutex, portMAX_DELAY);
            camera_service_poll(&c->camera);
            xSemaphoreGive(c->frame_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

/* AI task (Core 1): periodic inference, never touches LCD. */
static void ai_task_fn(void *arg)
{
    app_controller_t *c = (app_controller_t *)arg;
    while (true) {
        if (c->current_page == APP_PAGE_CAPTURE) {
            const uint16_t *px = NULL;
            uint16_t w = 0, h = 0;
            xSemaphoreTake(c->frame_mutex, portMAX_DELAY);
            bool got = camera_service_get_preview_rgb565(&c->camera, &px, &w, &h);
            xSemaphoreGive(c->frame_mutex);

            if (got) {
                classifier_result_t result = {0};
                classifier_set_input_rgb565(&c->classifier, px, w, h);
                classifier_predict(&c->classifier, &result);

                xSemaphoreTake(c->predict_mutex, portMAX_DELAY);
                c->current_prediction = result;
                xSemaphoreGive(c->predict_mutex);

                ESP_LOGI(TAG, "AI: has_result=%d species=%d conf=%u%%",
                         result.has_result, result.species, result.confidence);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

/* Render task (Core 0): sole owner of LCD writes on capture page. */
static void render_task_fn(void *arg)
{
    app_controller_t *c = (app_controller_t *)arg;
    while (true) {
        if (c->current_page == APP_PAGE_CAPTURE) {
            if (s_show_model_input && g_ai_debug_input_ready) {
                /* Show 96x96 model input upscaled to LCD */
                display_hal_blit_preview(g_ai_debug_input_rgb565, 96, 96);
            } else {
                /* Normal camera preview */
                const uint16_t *px = NULL;
                uint16_t w = 0, h = 0;
                xSemaphoreTake(c->frame_mutex, portMAX_DELAY);
                bool got = camera_service_get_preview_rgb565(&c->camera, &px, &w, &h);
                xSemaphoreGive(c->frame_mutex);

                if (got) {
                    display_hal_blit_preview(px, w, h);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static const CapturedCat *get_selected_backpack_cat(const app_controller_t *controller)
{
    const GameSaveData *save = NULL;

    if (controller == NULL) {
        return NULL;
    }

    save = game_service_get_save_data(&controller->game);
    if (save == NULL || save->captured_count == 0 || controller->selected_backpack_index >= save->captured_count) {
        return NULL;
    }

    return &save->captured[save->captured_count - 1U - controller->selected_backpack_index];
}

static void load_selected_cat_photo(app_controller_t *controller)
{
    const CapturedCat *cat = get_selected_backpack_cat(controller);
    const CapturedCat *latest = NULL;
    bool found = false;

    if (controller == NULL) {
        return;
    }

    memset(controller->detail_photo_rgb565, 0, sizeof(controller->detail_photo_rgb565));
    controller->detail_photo_available = false;

    if (cat == NULL) {
        return;
    }

    if (save_storage_load_thumbnail_rgb565(&controller->storage,
                                           cat->unique_id,
                                           controller->detail_photo_rgb565,
                                           CATDEX_THUMB_WIDTH,
                                           CATDEX_THUMB_HEIGHT,
                                           &found) == ESP_OK) {
        controller->detail_photo_available = found;
    }

    if (controller->detail_photo_available) {
        return;
    }

    latest = game_service_get_latest_capture(&controller->game);
    if (latest != NULL && latest->unique_id == cat->unique_id &&
        latest->unique_id == controller->last_captured.unique_id &&
        camera_service_build_thumbnail_rgb565(&controller->camera,
                                              controller->detail_photo_rgb565,
                                              CATDEX_THUMB_WIDTH,
                                              CATDEX_THUMB_HEIGHT)) {
        controller->detail_photo_available = true;
        ESP_LOGI(TAG, "Using in-memory fallback photo for cat #%lu", (unsigned long)cat->unique_id);
    }
}

static bool clear_expired_toast(app_controller_t *controller)
{
    (void)controller;
    return false;
}

static void fill_view_model(app_controller_t *controller, ui_view_model_t *model)
{
    const uint16_t *preview_pixels = NULL;
    uint16_t preview_width = 0;
    uint16_t preview_height = 0;

    memset(model, 0, sizeof(*model));
    model->current_page = controller->current_page;
    model->selected_menu_index = controller->selected_menu_index;

    xSemaphoreTake(controller->predict_mutex, portMAX_DELAY);
    model->prediction = &controller->current_prediction;
    xSemaphoreGive(controller->predict_mutex);
    model->save_data = game_service_get_save_data(&controller->game);
    model->latest_capture = game_service_get_latest_capture(&controller->game);
    model->selected_cat = get_selected_backpack_cat(controller);
    model->camera = &controller->camera;
    model->selected_backpack_index = controller->selected_backpack_index;
    model->cat_detail_tab_index = controller->cat_detail_tab_index;
    model->detail_photo_available = controller->detail_photo_available;
    model->classifier_backend_ready = classifier_backend_ready(&controller->classifier);
    model->classifier_backend_name = classifier_backend_name(&controller->classifier);
    model->last_captured = &controller->last_captured;
    model->detail_photo_rgb565 = controller->detail_photo_available ? controller->detail_photo_rgb565 : NULL;
    model->detail_photo_width = CATDEX_THUMB_WIDTH;
    model->detail_photo_height = CATDEX_THUMB_HEIGHT;

    xSemaphoreTake(controller->frame_mutex, portMAX_DELAY);
    if (camera_service_get_preview_rgb565(&controller->camera, &preview_pixels, &preview_width, &preview_height)) {
        model->preview_rgb565 = preview_pixels;
        model->preview_width = preview_width;
        model->preview_height = preview_height;
    }
    xSemaphoreGive(controller->frame_mutex);

    input_hal_get_debug_snapshot(&model->input_debug);
    model->demo_mode_enabled = input_hal_demo_enabled();
    snprintf(model->toast_message, sizeof(model->toast_message), "%s", controller->toast_message);
    snprintf(model->capture_fail_reason, sizeof(model->capture_fail_reason), "%s", controller->capture_fail_reason);
}

static void render_now(app_controller_t *controller)
{
    ui_view_model_t model = {0};
    fill_view_model(controller, &model);
    ui_manager_render(&model);
}

static void enter_page(app_controller_t *controller, app_page_t page)
{
    if (controller == NULL) {
        return;
    }

    controller->current_page = page;
    controller->toast_message[0] = '\0';
    controller->toast_expires_at_us = 0;

    if (page == APP_PAGE_CAPTURE) {
        ensure_capture_services_ready(controller);
    } else if (page == APP_PAGE_CAPTURE_RESULT) {
        /* detail_photo_rgb565 already filled by handle_capture_key thumbnail build */
        controller->detail_photo_available = true;
    } else if (page == APP_PAGE_BACKPACK) {
        controller->cat_detail_tab_index = 0;
    } else if (page == APP_PAGE_CAT_DETAIL) {
        controller->cat_detail_tab_index = 0;
        load_selected_cat_photo(controller);
    }

    render_now(controller);
}

static void handle_main_menu_key(app_controller_t *controller, input_key_t key)
{
    switch (key) {
        case INPUT_KEY_UP:
            if (controller->selected_menu_index > 0) {
                controller->selected_menu_index--;
            }
            break;
        case INPUT_KEY_DOWN:
            if (controller->selected_menu_index < 4) {
                controller->selected_menu_index++;
            }
            break;
        case INPUT_KEY_CONFIRM:
            switch (controller->selected_menu_index) {
                case 0:
                    enter_page(controller, APP_PAGE_CAPTURE);
                    return;
                case 1:
                    enter_page(controller, APP_PAGE_DEX);
                    return;
                case 2:
                    enter_page(controller, APP_PAGE_BACKPACK);
                    return;
                case 3:
                    enter_page(controller, APP_PAGE_SYSTEM_INFO);
                    return;
                case 4:
                    enter_page(controller, APP_PAGE_SETTINGS);
                    return;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    render_now(controller);
}

static void handle_capture_key(app_controller_t *controller, input_key_t key)
{
    if (key == INPUT_KEY_BACK) {
        enter_page(controller, APP_PAGE_MAIN_MENU);
        return;
    }

    if (key == INPUT_KEY_CONFIRM) {
        xSemaphoreTake(controller->predict_mutex, portMAX_DELAY);
        bool has_result = controller->current_prediction.has_result;
        classifier_result_t prediction_copy = controller->current_prediction;
        xSemaphoreGive(controller->predict_mutex);

        if (!has_result) {
            snprintf(controller->capture_fail_reason, sizeof(controller->capture_fail_reason),
                     "No target detected");
            enter_page(controller, APP_PAGE_CAPTURE_FAIL);
            return;
        }
        CapturedCat captured = {0};
        esp_err_t err = game_service_capture_prediction(&controller->game, &prediction_copy, &captured);
        if (err == ESP_OK) {
            if (camera_service_build_thumbnail_rgb565(&controller->camera,
                                                      controller->detail_photo_rgb565,
                                                      CATDEX_THUMB_WIDTH,
                                                      CATDEX_THUMB_HEIGHT)) {
                esp_err_t thumb_err = save_storage_save_thumbnail_rgb565(&controller->storage,
                                                                         captured.unique_id,
                                                                         controller->detail_photo_rgb565,
                                                                         CATDEX_THUMB_WIDTH,
                                                                         CATDEX_THUMB_HEIGHT);
                if (thumb_err != ESP_OK) {
                    ESP_LOGW(TAG, "Thumbnail save failed: %s", esp_err_to_name(thumb_err));
                }
            } else {
                ESP_LOGW(TAG, "Thumbnail build failed for cat #%lu", (unsigned long)captured.unique_id);
            }
            controller->last_captured = captured;
            controller->selected_backpack_index = 0;
            enter_page(controller, APP_PAGE_CAPTURE_RESULT);
        } else {
            if (err == ESP_ERR_INVALID_RESPONSE) {
                snprintf(controller->capture_fail_reason, sizeof(controller->capture_fail_reason),
                         "Conf %u%% < %u%% needed", prediction_copy.confidence, CATDEX_MIN_CONFIDENCE_TO_CAPTURE);
            } else {
                snprintf(controller->capture_fail_reason, sizeof(controller->capture_fail_reason),
                         "%s", k_toast_capture_fail);
            }
            enter_page(controller, APP_PAGE_CAPTURE_FAIL);
        }
        return;
    }

    if (key == INPUT_KEY_UP) {
        /* Toggle: show model input (96x96) vs camera preview on LCD */
        s_show_model_input = !s_show_model_input;
        ESP_LOGI(TAG, "DBG: model input preview %s", s_show_model_input ? "ON" : "OFF");
    }
}

static void handle_backpack_key(app_controller_t *controller, input_key_t key)
{
    const GameSaveData *save = NULL;

    if (controller == NULL) {
        return;
    }

    save = game_service_get_save_data(&controller->game);

    if (key == INPUT_KEY_BACK) {
        enter_page(controller, APP_PAGE_MAIN_MENU);
        return;
    }

    if (save == NULL || save->captured_count == 0) {
        if (key == INPUT_KEY_CONFIRM) {
            enter_page(controller, APP_PAGE_MAIN_MENU);
        }
        return;
    }

    if (key == INPUT_KEY_UP) {
        if (controller->selected_backpack_index > 0) {
            controller->selected_backpack_index--;
        }
    } else if (key == INPUT_KEY_DOWN) {
        if (controller->selected_backpack_index + 1U < save->captured_count) {
            controller->selected_backpack_index++;
        }
    } else if (key == INPUT_KEY_CONFIRM) {
        enter_page(controller, APP_PAGE_CAT_DETAIL);
        return;
    }

    render_now(controller);
}

static void handle_cat_detail_key(app_controller_t *controller, input_key_t key)
{
    if (controller == NULL) {
        return;
    }

    if (key == INPUT_KEY_BACK || key == INPUT_KEY_CONFIRM) {
        enter_page(controller, APP_PAGE_BACKPACK);
        return;
    }

    if (key == INPUT_KEY_UP) {
        controller->cat_detail_tab_index = (controller->cat_detail_tab_index + 1U) % k_cat_detail_tab_count;
        render_now(controller);
    } else if (key == INPUT_KEY_DOWN) {
        controller->cat_detail_tab_index = (controller->cat_detail_tab_index + 1U) % k_cat_detail_tab_count;
        render_now(controller);
    }
}

static void handle_capture_result_key(app_controller_t *controller, input_key_t key)
{
    if (key == INPUT_KEY_CONFIRM || key == INPUT_KEY_BACK) {
        enter_page(controller, APP_PAGE_CAPTURE);
    }
}

static void handle_capture_fail_key(app_controller_t *controller, input_key_t key)
{
    if (key == INPUT_KEY_CONFIRM || key == INPUT_KEY_BACK) {
        enter_page(controller, APP_PAGE_CAPTURE);
    }
}

static void handle_simple_page_key(app_controller_t *controller, input_key_t key)
{
    if (key == INPUT_KEY_BACK || key == INPUT_KEY_CONFIRM) {
        enter_page(controller, APP_PAGE_MAIN_MENU);
    }
}

esp_err_t app_controller_init(app_controller_t *controller)
{
    if (controller == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(controller, 0, sizeof(*controller));

    ESP_ERROR_CHECK(save_storage_init(&controller->storage));
    ESP_ERROR_CHECK(game_service_init(&controller->game, &controller->storage));

    esp_err_t seed_err = game_service_seed_demo_if_needed(&controller->game);
    if (seed_err != ESP_OK) {
        ESP_LOGW(TAG, "Demo seed failed: %s", esp_err_to_name(seed_err));
    }

    classifier_init(&controller->classifier);
    input_hal_init();
    ui_manager_init();

    /* Pre-init camera so first capture page entry has no delay */
    camera_service_init(&controller->camera);

    controller->current_page = APP_PAGE_MAIN_MENU;
    controller->selected_menu_index = 0;
    controller->selected_backpack_index = 0;
    controller->cat_detail_tab_index = 0;
    controller->detail_photo_available = false;
    controller->toast_message[0] = '\0';

    controller->frame_mutex   = xSemaphoreCreateMutex();
    controller->predict_mutex = xSemaphoreCreateMutex();
    configASSERT(controller->frame_mutex);
    configASSERT(controller->predict_mutex);

    xTaskCreatePinnedToCore(camera_task_fn, "cam",    4096, controller, 5, &controller->camera_task, 1);
    xTaskCreatePinnedToCore(ai_task_fn,     "ai",     8192, controller, 3, &controller->ai_task,     1);
    xTaskCreatePinnedToCore(render_task_fn, "render", 4096, controller, 4, &controller->render_task, 0);

    render_now(controller);

    return ESP_OK;
}

esp_err_t app_controller_run(app_controller_t *controller)
{
    if (controller == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    while (true) {
        bool toast_cleared = clear_expired_toast(controller);
        input_key_t key = input_hal_poll();

        if (key != INPUT_KEY_NONE) {
            switch (controller->current_page) {
                case APP_PAGE_MAIN_MENU:
                    handle_main_menu_key(controller, key);
                    break;
                case APP_PAGE_CAPTURE:
                    handle_capture_key(controller, key);
                    break;
                case APP_PAGE_DEX:
                    handle_simple_page_key(controller, key);
                    break;
                case APP_PAGE_BACKPACK:
                    handle_backpack_key(controller, key);
                    break;
                case APP_PAGE_CAT_DETAIL:
                    handle_cat_detail_key(controller, key);
                    break;
                case APP_PAGE_CAPTURE_RESULT:
                    handle_capture_result_key(controller, key);
                    break;
                case APP_PAGE_CAPTURE_FAIL:
                    handle_capture_fail_key(controller, key);
                    break;
                case APP_PAGE_SYSTEM_INFO:
                case APP_PAGE_SETTINGS:
                    handle_simple_page_key(controller, key);
                    break;
                default:
                    enter_page(controller, APP_PAGE_MAIN_MENU);
                    break;
            }
        } else if (toast_cleared) {
            render_now(controller);
        }
        /* On capture page: UI text rendered once on enter_page and on key press.
           Preview is continuously blitted by camera_blit_task. No render here. */

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

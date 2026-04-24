#include "ui/ui_manager.h"

#include <stdio.h>
#include <string.h>
#include "assets/assets.h"
#include "common/project_catalog.h"
#include "drivers/board_profile.h"
#include "drivers/display_hal.h"

static const char *k_main_menu_items[] = {
    "Start Capture",
    "Dex",
    "Backpack",
    "System Info",
    "Settings",
};
extern const uint16_t capture_fail_bg[240 * 240];
extern const uint16_t pet_talent_bg[120*120];
static void append_line(display_frame_t *frame, size_t index, const char *text)
{
    if (frame == NULL || text == NULL || index >= CATDEX_UI_LINE_COUNT) {
        return;
    }

    snprintf(frame->lines[index], sizeof(frame->lines[index]), "%s", text);
}

static void render_main_menu(display_frame_t *frame, const ui_view_model_t *model)
{
    frame->layout = DISPLAY_LAYOUT_MAIN_MENU;
    snprintf(frame->title, sizeof(frame->title), "Roco Kingdom");

    for (size_t i = 0; i < sizeof(k_main_menu_items) / sizeof(k_main_menu_items[0]) && i < CATDEX_UI_LINE_COUNT; ++i) {
        snprintf(frame->lines[i], sizeof(frame->lines[i]), "%c %s",
                 model->selected_menu_index == i ? '>' : ' ',
                 k_main_menu_items[i]);
    }

    snprintf(frame->footer, sizeof(frame->footer), "Confirm=enter  Demo=%s", model->demo_mode_enabled ? "on" : "off");
}

static void render_capture(display_frame_t *frame, const ui_view_model_t *model)
{
    char species_line[CATDEX_UI_LINE_LEN] = {0};
    char latest_line[CATDEX_UI_LINE_LEN] = {0};

    frame->layout = DISPLAY_LAYOUT_CAPTURE;
    snprintf(frame->title, sizeof(frame->title), "Capture Deck");

    if (model->prediction != NULL && model->prediction->has_result) {
        const cat_species_profile_t *profile = project_catalog_get_species_profile(model->prediction->species);
        snprintf(species_line, sizeof(species_line), "Predict: %.40s (%u%%)",
                 profile != NULL ? profile->display_name : "Unknown",
                 model->prediction->confidence);
    } else {
        snprintf(species_line, sizeof(species_line), "Predict: Unknown");
    }
    append_line(frame, 2, species_line);

    if (model->latest_capture != NULL) {
        snprintf(latest_line, sizeof(latest_line), "Last catch: %.24s Lv.%u",
                 model->latest_capture->nickname,
                 model->latest_capture->level);
        append_line(frame, 3, latest_line);
    }

    if (model->toast_message[0] != '\0') {
        append_line(frame, 5, model->toast_message);
    } else {
        append_line(frame, 5, "Confirm capture | Back menu");
    }

    frame->preview.has_preview = model->preview_rgb565 != NULL && model->preview_width > 0 && model->preview_height > 0;
    frame->preview.rgb565 = model->preview_rgb565;
    frame->preview.width = model->preview_width;
    frame->preview.height = model->preview_height;

    snprintf(frame->footer, sizeof(frame->footer), "Dex %u/%u  Box %u/%u",
             model->save_data != NULL ? model->save_data->discovered_species_count : 0,
             CAT_SPECIES_COUNT,
             model->save_data != NULL ? model->save_data->captured_count : 0,
             CATDEX_MAX_CAPTURED);
}

static void render_dex(display_frame_t *frame, const ui_view_model_t *model)
{
    frame->layout = DISPLAY_LAYOUT_STANDARD;
    snprintf(frame->title, sizeof(frame->title), "Field Dex");

    if (model->save_data == NULL) {
        append_line(frame, 0, "No save data");
        snprintf(frame->footer, sizeof(frame->footer), "Back=menu");
        return;
    }

    for (size_t i = 0; i < CAT_SPECIES_COUNT && i < CATDEX_UI_LINE_COUNT; ++i) {
        const DexEntry *entry = &model->save_data->dex_entries[i];
        const cat_species_profile_t *profile = project_catalog_get_species_profile((CatSpecies)i);
        snprintf(frame->lines[i], sizeof(frame->lines[i]), "%02u %c %-20.20s x%u",
                 (unsigned)i + 1U,
                 entry->discovered ? '*' : '-',
                 entry->discovered && profile != NULL ? profile->display_name : "Undiscovered",
                 entry->capture_count);
    }

    snprintf(frame->footer, sizeof(frame->footer), "Found %u/%u | Back=menu",
             model->save_data->discovered_species_count,
             CAT_SPECIES_COUNT);
}

static void render_backpack(display_frame_t *frame, const ui_view_model_t *model)
{
    frame->layout = DISPLAY_LAYOUT_STANDARD;
    snprintf(frame->title, sizeof(frame->title), "Backpack");

    if (model->save_data == NULL || model->save_data->captured_count == 0) {
        append_line(frame, 0, "No captured cats yet");
        snprintf(frame->footer, sizeof(frame->footer), "Capture one first");
        return;
    }

    size_t shown = 0;
    size_t captured_count = model->save_data->captured_count;
    size_t window_start = 0;
    if (model->selected_backpack_index >= CATDEX_UI_LINE_COUNT) {
        window_start = model->selected_backpack_index - (CATDEX_UI_LINE_COUNT - 1U);
    }

    for (size_t i = window_start; i < captured_count && shown < CATDEX_UI_LINE_COUNT; ++i) {
        const CapturedCat *cat = &model->save_data->captured[captured_count - 1U - i];
        snprintf(frame->lines[shown], sizeof(frame->lines[shown]), "%c#%03lu %.16s Lv.%u",
                 model->selected_backpack_index == i ? '>' : ' ',
                 (unsigned long)cat->unique_id,
                 cat->nickname,
                 cat->level);
        shown++;
    }

    snprintf(frame->footer, sizeof(frame->footer), "Confirm=view  %u/%u",
             (unsigned)(model->selected_backpack_index + 1U),
             model->save_data->captured_count);
}

static void render_cat_detail(display_frame_t *frame, const ui_view_model_t *model)
{
    const CapturedCat *cat = model->selected_cat;

    if (cat == NULL) {
        frame->layout = DISPLAY_LAYOUT_DETAIL_CUSTOM;
        snprintf(frame->title, sizeof(frame->title), "Cat Detail");
        snprintf(frame->footer, sizeof(frame->footer), "Back");
        return;
    }

    const cat_species_profile_t *profile = project_catalog_get_species_profile(cat->species);

    if (model->cat_detail_tab_index == 0) {
        /* photo view: full-screen photo + overlay sprite */
        frame->layout = DISPLAY_LAYOUT_DETAIL_CUSTOM;
        frame->preview.has_preview = model->detail_photo_available && model->detail_photo_rgb565 != NULL;
        frame->preview.rgb565 = model->detail_photo_rgb565;
        frame->preview.width = model->detail_photo_width;
        frame->preview.height = model->detail_photo_height;

        snprintf(frame->title,    sizeof(frame->title),    "%.22s", cat->nickname);
        snprintf(frame->lines[0], sizeof(frame->lines[0]), "%.20s Lv.%u",
                 profile != NULL ? profile->display_name : "Unknown", cat->level);

        snprintf(frame->footer, sizeof(frame->footer), "Down=stats  Back=exit");

        frame->overlay_count = 1;
        frame->overlays[0].rgb565 = pet_talent_bg;
        frame->overlays[0].src_w = 120; frame->overlays[0].src_h = 120;
        frame->overlays[0].dst_x = 20;  frame->overlays[0].dst_y = 120;
        frame->overlays[0].dst_w = 120; frame->overlays[0].dst_h = 120;
    } else {
        /* stats view: standard layout, no photo */
        frame->layout = DISPLAY_LAYOUT_STANDARD;
        snprintf(frame->title, sizeof(frame->title), "%.22s", cat->nickname);
        snprintf(frame->lines[0], sizeof(frame->lines[0]), "%.20s Lv.%u",
                 profile != NULL ? profile->display_name : "Unknown", cat->level);
        snprintf(frame->lines[1], sizeof(frame->lines[1]), "%.12s %.10s",
                 project_catalog_rarity_name(cat->rarity),
                 project_catalog_personality_name(cat->personality));
        snprintf(frame->lines[2], sizeof(frame->lines[2]), "HP:%u ATK:%u DEF:%u",
                 cat->talent.hp, cat->talent.attack, cat->talent.defense);
        snprintf(frame->lines[3], sizeof(frame->lines[3]), "AGI:%u AFF:%u",
                 cat->talent.agility, cat->talent.affinity);
        snprintf(frame->lines[4], sizeof(frame->lines[4]), "Weight: %ug", cat->weight_grams);
        for (size_t i = 0; i < cat->skill_count && i < 3; ++i) {
            snprintf(frame->lines[5 + i], sizeof(frame->lines[5 + i]), "S%zu %.16s P%u",
                     i + 1, cat->skills[i].name, cat->skills[i].power);
        }
        snprintf(frame->footer, sizeof(frame->footer), "Up=photo  Back=exit");
    }
}

static void render_capture_result(display_frame_t *frame, const ui_view_model_t *model)
{
    const CapturedCat *cat = model->last_captured;
    frame->layout = DISPLAY_LAYOUT_DETAIL_CUSTOM;

    /* full-screen photo as background */
    frame->preview.has_preview = model->detail_photo_available && model->detail_photo_rgb565 != NULL;
    frame->preview.rgb565 = model->detail_photo_rgb565;
    frame->preview.width = model->detail_photo_width;
    frame->preview.height = model->detail_photo_height;

    if (cat == NULL || cat->unique_id == 0) {
        snprintf(frame->title, sizeof(frame->title), "Captured!");
        snprintf(frame->footer, sizeof(frame->footer), "Confirm=back");
        return;
    }

    const cat_species_profile_t *profile = project_catalog_get_species_profile(cat->species);
    snprintf(frame->title,    sizeof(frame->title),    "Captured!");
    snprintf(frame->lines[0], sizeof(frame->lines[0]), "%.22s %.20s Lv.%u", cat->nickname,profile != NULL ? profile->display_name : "Unknown", cat->level);



    snprintf(frame->footer,   sizeof(frame->footer),   "Confirm/Back=continue");

    // 叠加小图标（任意位置/大小）
    frame->overlay_count = 1;
    frame->overlays[0].rgb565 = pet_talent_bg;
    frame->overlays[0].src_w = 120; frame->overlays[0].src_h = 120;
    frame->overlays[0].dst_x = 20; frame->overlays[0].dst_y = 120;
    frame->overlays[0].dst_w = 120; frame->overlays[0].dst_h = 120;
}

static void render_system_info(display_frame_t *frame, const ui_view_model_t *model)
{
    frame->layout = DISPLAY_LAYOUT_STANDARD;
    char lcd_desc[CATDEX_UI_LINE_LEN] = {0};
    char camera_desc[CATDEX_UI_LINE_LEN] = {0};
    const board_profile_t *profile = board_profile_get_active();

    board_profile_describe_lcd(lcd_desc, sizeof(lcd_desc));
    camera_service_get_status(model->camera, camera_desc, sizeof(camera_desc));

    snprintf(frame->title, sizeof(frame->title), "System Info");
    snprintf(frame->lines[0], sizeof(frame->lines[0]), "Board: %.40s", profile != NULL ? profile->name : "Unknown");
    snprintf(frame->lines[1], sizeof(frame->lines[1]), "Input: %dmV %s%s",
             model->input_debug.voltage_mv,
             board_profile_key_name(model->input_debug.decoded_key),
             model->demo_mode_enabled ? " +demo" : "");
    snprintf(frame->lines[2], sizeof(frame->lines[2]), "Disp: %.28s", display_hal_backend_name());
    snprintf(frame->lines[3], sizeof(frame->lines[3]), "LCD: %.42s", lcd_desc);
    snprintf(frame->lines[4], sizeof(frame->lines[4]), "Cam:%s  AI:%s %s",
             (model->camera != NULL && model->camera->using_real_camera) ? "real" : "mock",
             model->classifier_backend_name != NULL ? model->classifier_backend_name : "unknown",
             model->classifier_backend_ready ? "ok" : "wait");
    snprintf(frame->lines[5], sizeof(frame->lines[5]), "%.52s",
             camera_desc[0] != '\0' ? camera_desc : "Camera status unavailable");
    snprintf(frame->lines[6], sizeof(frame->lines[6]), "Dex: %u/%u",
             model->save_data != NULL ? model->save_data->discovered_species_count : 0,
             CAT_SPECIES_COUNT);
    snprintf(frame->lines[7], sizeof(frame->lines[7]), "Bag: %u/%u",
             model->save_data != NULL ? model->save_data->captured_count : 0,
             CATDEX_MAX_CAPTURED);
    snprintf(frame->footer, sizeof(frame->footer), "Back=menu");
}

static void render_settings(display_frame_t *frame, const ui_view_model_t *model)
{
    (void)model;
    frame->layout = DISPLAY_LAYOUT_STANDARD;
    snprintf(frame->title, sizeof(frame->title), "Settings");
    append_line(frame, 0, "1. Input demo: off");
    append_line(frame, 1, "2. ADC keypad: enabled");
    append_line(frame, 2, "3. LCD backend: ready");
    append_line(frame, 3, "4. Camera preview: live");
    append_line(frame, 4, "5. Board profile: S3-EYE");
    append_line(frame, 5, "6. AI backend: mock");
    snprintf(frame->footer, sizeof(frame->footer), "Back=menu");
}

void ui_manager_init(void)
{
    display_hal_init();
}

static void render_capture_fail(display_frame_t *frame, const ui_view_model_t *model)
{
    (void)model;
    frame->layout = DISPLAY_LAYOUT_IMAGE_ONLY;
    frame->bg_image_rgb565 = capture_fail_bg;
}

void ui_manager_render(const ui_view_model_t *model)
{
    if (model == NULL) {
        return;
    }

    display_frame_t frame = {0};

    switch (model->current_page) {
        case APP_PAGE_MAIN_MENU:
            render_main_menu(&frame, model);
            break;
        case APP_PAGE_CAPTURE:
            render_capture(&frame, model);
            break;
        case APP_PAGE_DEX:
            render_dex(&frame, model);
            break;
        case APP_PAGE_BACKPACK:
            render_backpack(&frame, model);
            break;
        case APP_PAGE_CAT_DETAIL:
            render_cat_detail(&frame, model);
            break;
        case APP_PAGE_SYSTEM_INFO:
            render_system_info(&frame, model);
            break;
        case APP_PAGE_SETTINGS:
            render_settings(&frame, model);
            break;
        case APP_PAGE_CAPTURE_RESULT:
            render_capture_result(&frame, model);
            break;
        case APP_PAGE_CAPTURE_FAIL:
            render_capture_fail(&frame, model);
            break;
        default:
            snprintf(frame.title, sizeof(frame.title), "CatDex");
            append_line(&frame, 0, "Unknown page");
            break;
    }

    display_hal_present(&frame);
}

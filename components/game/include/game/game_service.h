#pragma once

#include "esp_err.h"

#include "ai/classifier.h"
#include "common/project_types.h"
#include "storage/save_storage.h"

typedef struct {
    save_storage_t *storage;
    GameSaveData save_data;
} game_service_t;

esp_err_t game_service_init(game_service_t *service, save_storage_t *storage);
esp_err_t game_service_seed_demo_if_needed(game_service_t *service);
esp_err_t game_service_capture_prediction(game_service_t *service,
                                          const classifier_result_t *prediction,
                                          CapturedCat *out_captured);
esp_err_t game_service_set_favorite(game_service_t *service, uint32_t unique_id, bool is_favorite);
const GameSaveData *game_service_get_save_data(const game_service_t *service);
const DexEntry *game_service_get_dex_entry(const game_service_t *service, CatSpecies species);
const CapturedCat *game_service_get_capture_by_rank(const game_service_t *service,
                                                    size_t rank,
                                                    WarehouseSortMode sort_mode);
const CapturedCat *game_service_get_latest_capture(const game_service_t *service);

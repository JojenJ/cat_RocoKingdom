#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "common/project_types.h"

typedef struct {
    const char *nvs_namespace;
    const char *blob_key;
    const char *spiffs_base_path;
    const char *spiffs_partition_label;
    bool spiffs_mounted;
} save_storage_t;

esp_err_t save_storage_init(save_storage_t *storage);
esp_err_t save_storage_load(save_storage_t *storage, GameSaveData *out_data, bool *was_created);
esp_err_t save_storage_save(save_storage_t *storage, const GameSaveData *data);
esp_err_t save_storage_save_thumbnail_rgb565(save_storage_t *storage,
                                             uint32_t unique_id,
                                             const uint16_t *rgb565,
                                             uint16_t width,
                                             uint16_t height);
esp_err_t save_storage_load_thumbnail_rgb565(save_storage_t *storage,
                                             uint32_t unique_id,
                                             uint16_t *dst_rgb565,
                                             uint16_t width,
                                             uint16_t height,
                                             bool *out_found);

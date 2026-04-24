#include "storage/save_storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs.h"

static const char *TAG = "save_storage";
static const uint32_t k_thumbnail_magic = 0x31485443UL; /* "CTH1" */
static const uint16_t k_legacy_thumb_size = 64;

typedef struct {
    uint32_t magic;
    uint16_t width;
    uint16_t height;
    uint16_t reserved;
} thumbnail_header_t;

static void build_thumbnail_path(const save_storage_t *storage,
                                 uint32_t unique_id,
                                 char *buffer,
                                 size_t buffer_len)
{
    if (storage == NULL || buffer == NULL || buffer_len == 0) {
        return;
    }

    snprintf(buffer,
             buffer_len,
             "%s/cat_%08lu.rgb565",
             storage->spiffs_base_path,
             (unsigned long)unique_id);
}

static void scale_rgb565_nearest(const uint16_t *src,
                                 uint16_t src_width,
                                 uint16_t src_height,
                                 uint16_t *dst,
                                 uint16_t dst_width,
                                 uint16_t dst_height)
{
    if (src == NULL || dst == NULL || src_width == 0 || src_height == 0 || dst_width == 0 || dst_height == 0) {
        return;
    }

    for (uint16_t row = 0; row < dst_height; ++row) {
        uint16_t sample_y = (uint16_t)(((uint32_t)row * src_height) / dst_height);
        if (sample_y >= src_height) {
            sample_y = src_height - 1;
        }

        for (uint16_t col = 0; col < dst_width; ++col) {
            uint16_t sample_x = (uint16_t)(((uint32_t)col * src_width) / dst_width);
            if (sample_x >= src_width) {
                sample_x = src_width - 1;
            }

            dst[(size_t)row * dst_width + col] = src[(size_t)sample_y * src_width + sample_x];
        }
    }
}

esp_err_t save_storage_init(save_storage_t *storage)
{
    if (storage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    storage->nvs_namespace = "catdex";
    storage->blob_key = "save_blob";
    storage->spiffs_base_path = "/storage";
    storage->spiffs_partition_label = "storage";
    storage->spiffs_mounted = false;

    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = storage->spiffs_base_path,
        .partition_label = storage->spiffs_partition_label,
        .max_files = 8,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&spiffs_conf);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    storage->spiffs_mounted = true;
    ESP_LOGI(TAG,
             "SPIFFS ready: base=%s label=%s",
             storage->spiffs_base_path,
             storage->spiffs_partition_label);
    return ESP_OK;
}

esp_err_t save_storage_load(save_storage_t *storage, GameSaveData *out_data, bool *was_created)
{
    if (storage == NULL || out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (was_created != NULL) {
        *was_created = false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(storage->nvs_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = sizeof(*out_data);
    err = nvs_get_blob(handle, storage->blob_key, out_data, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        project_types_init_save_data(out_data);
        if (was_created != NULL) {
            *was_created = true;
        }
        nvs_close(handle);
        ESP_LOGI(TAG, "No save found, created default save");
        return ESP_OK;
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        return err;
    }

    if (required_size != sizeof(*out_data) || out_data->schema_version != CATDEX_SCHEMA_VERSION) {
        ESP_LOGW(TAG, "Save schema mismatch, resetting save");
        project_types_init_save_data(out_data);
        if (was_created != NULL) {
            *was_created = true;
        }
    } else {
        project_types_refresh_discovery_count(out_data);
    }

    return ESP_OK;
}

esp_err_t save_storage_save(save_storage_t *storage, const GameSaveData *data)
{
    if (storage == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(storage->nvs_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, storage->blob_key, data, sizeof(*data));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t save_storage_save_thumbnail_rgb565(save_storage_t *storage,
                                             uint32_t unique_id,
                                             const uint16_t *rgb565,
                                             uint16_t width,
                                             uint16_t height)
{
    char path[96] = {0};
    size_t pixel_count = 0;
    FILE *file = NULL;
    thumbnail_header_t header = {0};

    if (storage == NULL || rgb565 == NULL || width == 0 || height == 0 || !storage->spiffs_mounted) {
        return ESP_ERR_INVALID_ARG;
    }

    build_thumbnail_path(storage, unique_id, path, sizeof(path));
    pixel_count = (size_t)width * (size_t)height;
    header.magic = k_thumbnail_magic;
    header.width = width;
    header.height = height;

    file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Thumbnail open-for-write failed: %s errno=%d", path, errno);
        return ESP_FAIL;
    }

    if (fwrite(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        ESP_LOGW(TAG, "Thumbnail header write failed: %s errno=%d", path, errno);
        return ESP_FAIL;
    }

    if (fwrite(rgb565, sizeof(uint16_t), pixel_count, file) != pixel_count) {
        fclose(file);
        ESP_LOGW(TAG, "Thumbnail write failed: %s errno=%d", path, errno);
        return ESP_FAIL;
    }

    fclose(file);
    struct stat st = {0};
    if (stat(path, &st) == 0) {
        ESP_LOGI(TAG,
                 "Thumbnail saved: %s (%ld bytes)",
                 path,
                 (long)st.st_size);
    } else {
        ESP_LOGI(TAG, "Thumbnail saved: %s", path);
    }
    return ESP_OK;
}

esp_err_t save_storage_load_thumbnail_rgb565(save_storage_t *storage,
                                             uint32_t unique_id,
                                             uint16_t *dst_rgb565,
                                             uint16_t width,
                                             uint16_t height,
                                             bool *out_found)
{
    char path[96] = {0};
    size_t pixel_count = 0;
    FILE *file = NULL;
    struct stat st = {0};
    thumbnail_header_t header = {0};
    uint16_t src_width = 0;
    uint16_t src_height = 0;
    uint16_t *temp_rgb565 = NULL;
    size_t temp_pixels = 0;

    if (out_found != NULL) {
        *out_found = false;
    }

    if (storage == NULL || dst_rgb565 == NULL || width == 0 || height == 0 || !storage->spiffs_mounted) {
        return ESP_ERR_INVALID_ARG;
    }

    build_thumbnail_path(storage, unique_id, path, sizeof(path));
    pixel_count = (size_t)width * (size_t)height;

    if (stat(path, &st) != 0) {
        ESP_LOGI(TAG, "Thumbnail not found: %s", path);
        return ESP_OK;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGI(TAG, "Thumbnail not found: %s", path);
        return ESP_OK;
    }

    if ((size_t)st.st_size == sizeof(thumbnail_header_t) + pixel_count * sizeof(uint16_t)) {
        if (fread(&header, sizeof(header), 1, file) == 1 &&
            header.magic == k_thumbnail_magic &&
            header.width == width &&
            header.height == height &&
            fread(dst_rgb565, sizeof(uint16_t), pixel_count, file) == pixel_count) {
            fclose(file);
            if (out_found != NULL) {
                *out_found = true;
            }
            ESP_LOGI(TAG, "Thumbnail loaded: %s", path);
            return ESP_OK;
        }

        rewind(file);
    }

    if (fread(&header, sizeof(header), 1, file) == 1 && header.magic == k_thumbnail_magic) {
        src_width = header.width;
        src_height = header.height;
    } else {
        rewind(file);
        if ((size_t)st.st_size == (size_t)k_legacy_thumb_size * k_legacy_thumb_size * sizeof(uint16_t)) {
            src_width = k_legacy_thumb_size;
            src_height = k_legacy_thumb_size;
        } else if ((size_t)st.st_size == pixel_count * sizeof(uint16_t)) {
            src_width = width;
            src_height = height;
        }
    }

    if (src_width == 0 || src_height == 0) {
        fclose(file);
        ESP_LOGW(TAG, "Thumbnail format unsupported: %s size=%ld", path, (long)st.st_size);
        return ESP_FAIL;
    }

    temp_pixels = (size_t)src_width * (size_t)src_height;
    temp_rgb565 = malloc(temp_pixels * sizeof(uint16_t));
    if (temp_rgb565 == NULL) {
        fclose(file);
        ESP_LOGW(TAG, "Thumbnail temp alloc failed: %s", path);
        return ESP_ERR_NO_MEM;
    }

    if (fread(temp_rgb565, sizeof(uint16_t), temp_pixels, file) != temp_pixels) {
        free(temp_rgb565);
        fclose(file);
        ESP_LOGW(TAG, "Thumbnail read failed: %s errno=%d", path, errno);
        return ESP_FAIL;
    }

    fclose(file);
    scale_rgb565_nearest(temp_rgb565, src_width, src_height, dst_rgb565, width, height);
    free(temp_rgb565);

    if (out_found != NULL) {
        *out_found = true;
    }
    ESP_LOGI(TAG, "Thumbnail loaded: %s (%ux%u -> %ux%u)", path, src_width, src_height, width, height);
    return ESP_OK;
}

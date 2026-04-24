#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app/app_controller.h"

static const char *TAG = "main";
static app_controller_t s_controller;

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Starting CatDex Stage 1 MVP");
    ESP_LOGI(TAG, "app_controller_t size = %u bytes", (unsigned)sizeof(s_controller));

    ESP_ERROR_CHECK(app_controller_init(&s_controller));
    ESP_ERROR_CHECK(app_controller_run(&s_controller));
}

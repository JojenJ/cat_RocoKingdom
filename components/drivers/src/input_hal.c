#include "drivers/input_hal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "common/app_config.h"
#include "drivers/board_profile.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "input_hal";
static const size_t k_adc_filter_samples = 1;
static const uint8_t k_adc_stable_polls = 1;

typedef struct {
    bool initialized;
    bool cali_enabled;
    adc_oneshot_unit_handle_t oneshot_handle;
    adc_cali_handle_t cali_handle;
    int last_raw;
    int last_voltage_mv;
    input_key_t last_decoded_key;
    input_key_t candidate_key;
    uint8_t candidate_count;
    input_key_t latched_key;
} adc_button_state_t;

static adc_button_state_t s_adc_state;
static int64_t s_last_demo_step_us;
static size_t s_demo_index;
static bool s_demo_script_allowed;

static bool init_adc_calibration(const board_profile_t *profile, adc_cali_handle_t *out_handle)
{
    if (profile == NULL || out_handle == NULL) {
        return false;
    }

    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = profile->button_adc_unit,
            .chan = profile->button_adc_channel,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_cfg = {
            .unit_id = profile->button_adc_unit,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    return calibrated;
}

static esp_err_t init_adc_buttons(const board_profile_t *profile)
{
    if (profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = profile->button_adc_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc_state.oneshot_handle), TAG, "create adc unit failed");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_state.oneshot_handle,
                                                   profile->button_adc_channel,
                                                   &chan_cfg),
                        TAG,
                        "config adc channel failed");

    s_adc_state.cali_enabled = init_adc_calibration(profile, &s_adc_state.cali_handle);

    s_adc_state.initialized = true;
    s_adc_state.last_raw = 0;
    s_adc_state.last_voltage_mv = 0;
    s_adc_state.last_decoded_key = INPUT_KEY_NONE;
    s_adc_state.candidate_key = INPUT_KEY_NONE;
    s_adc_state.candidate_count = 0;
    s_adc_state.latched_key = INPUT_KEY_NONE;

    return ESP_OK;
}

static input_key_t decode_voltage_to_key(const board_profile_t *profile, int voltage_mv)
{
    if (profile == NULL || profile->button_keypoints == NULL || profile->button_keypoint_count == 0) {
        return INPUT_KEY_NONE;
    }

    int best_delta = INT_MAX;
    input_key_t best_key = INPUT_KEY_NONE;

    for (size_t i = 0; i < profile->button_keypoint_count; ++i) {
        int delta = abs(voltage_mv - profile->button_keypoints[i].expected_mv);
        if (delta < best_delta) {
            best_delta = delta;
            best_key = profile->button_keypoints[i].key;
        }
    }

    if (best_delta <= CONFIG_CATDEX_INPUT_ADC_TOLERANCE_MV) {
        return best_key;
    }
    return INPUT_KEY_NONE;
}

static input_key_t poll_adc_buttons(const board_profile_t *profile)
{
    if (!s_adc_state.initialized || profile == NULL) {
        return INPUT_KEY_NONE;
    }

    int raw_sum = 0;
    int voltage_sum = 0;
    size_t valid_samples = 0;
    for (size_t i = 0; i < k_adc_filter_samples; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_state.oneshot_handle, profile->button_adc_channel, &raw) != ESP_OK) {
            continue;
        }

        int voltage_mv = 0;
        if (s_adc_state.cali_enabled) {
            if (adc_cali_raw_to_voltage(s_adc_state.cali_handle, raw, &voltage_mv) != ESP_OK) {
                voltage_mv = raw * 3300 / 4095;
            }
        } else {
            voltage_mv = raw * 3300 / 4095;
        }

        raw_sum += raw;
        voltage_sum += voltage_mv;
        valid_samples++;
    }

    if (valid_samples == 0) {
        return INPUT_KEY_NONE;
    }

    int raw = raw_sum / (int)valid_samples;
    int voltage_mv = voltage_sum / (int)valid_samples;
    input_key_t decoded = decode_voltage_to_key(profile, voltage_mv);
    s_adc_state.last_raw = raw;
    s_adc_state.last_voltage_mv = voltage_mv;
    s_adc_state.last_decoded_key = decoded;

#if CATDEX_INPUT_ADC_LOG_RAW
    static int64_t s_last_log_us;
    int64_t now = esp_timer_get_time();
    if ((decoded != INPUT_KEY_NONE || s_adc_state.latched_key != INPUT_KEY_NONE) &&
        (now - s_last_log_us) > 250000) {
        s_last_log_us = now;
        ESP_LOGI(TAG, "ADC key raw=%d mv=%d decoded=%s", raw, voltage_mv, board_profile_key_name(decoded));
    }
#endif

    if (decoded == INPUT_KEY_NONE) {
        s_adc_state.candidate_key = INPUT_KEY_NONE;
        s_adc_state.candidate_count = 0;
        s_adc_state.latched_key = INPUT_KEY_NONE;
        return INPUT_KEY_NONE;
    }

    if (decoded != s_adc_state.candidate_key) {
        s_adc_state.candidate_key = decoded;
        s_adc_state.candidate_count = 1;
        return INPUT_KEY_NONE;
    }

    if (s_adc_state.candidate_count < k_adc_stable_polls) {
        s_adc_state.candidate_count++;
        return INPUT_KEY_NONE;
    }

    if (s_adc_state.latched_key == decoded) {
        return INPUT_KEY_NONE;
    }

    s_adc_state.latched_key = decoded;
    return decoded;
}

static input_key_t poll_demo_script(void)
{
    if (!s_demo_script_allowed) {
        return INPUT_KEY_NONE;
    }

#if CATDEX_ENABLE_INPUT_DEMO
    static const input_key_t k_script[] = {
        INPUT_KEY_CONFIRM,
        INPUT_KEY_CONFIRM,
        INPUT_KEY_BACK,
        INPUT_KEY_DOWN,
        INPUT_KEY_CONFIRM,
        INPUT_KEY_BACK,
        INPUT_KEY_DOWN,
        INPUT_KEY_CONFIRM,
        INPUT_KEY_BACK,
        INPUT_KEY_DOWN,
        INPUT_KEY_CONFIRM,
        INPUT_KEY_BACK,
    };

    int64_t now = esp_timer_get_time();
    if (s_demo_index >= sizeof(k_script) / sizeof(k_script[0])) {
        return INPUT_KEY_NONE;
    }

    if ((now - s_last_demo_step_us) >= 1800000) {
        s_last_demo_step_us = now;
        return k_script[s_demo_index++];
    }
#endif
    return INPUT_KEY_NONE;
}

void input_hal_init(void)
{
    const board_profile_t *profile = board_profile_get_active();
    s_last_demo_step_us = esp_timer_get_time();
    s_demo_index = 0;
    s_demo_script_allowed = false;

    esp_err_t err = init_adc_buttons(profile);
    if (err == ESP_OK) {
        /* On real hardware, prefer physical keys over the scripted demo. */
        s_demo_script_allowed = false;
        ESP_LOGI(TAG,
                 "Input HAL initialized for %s keypad on GPIO%d, demo mode=%s",
                 profile->name,
                 profile->button_adc_gpio,
                 s_demo_script_allowed ? "on" : "off");
    } else {
        s_demo_script_allowed = CATDEX_ENABLE_INPUT_DEMO;
        ESP_LOGW(TAG, "ADC keypad init failed: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "Demo script fallback=%s", s_demo_script_allowed ? "on" : "off");
    }
}

input_key_t input_hal_poll(void)
{
    const board_profile_t *profile = board_profile_get_active();
    input_key_t key = poll_adc_buttons(profile);
    if (key != INPUT_KEY_NONE) {
        return key;
    }
    return poll_demo_script();
}

bool input_hal_demo_enabled(void)
{
    return s_demo_script_allowed;
}

void input_hal_get_debug_snapshot(input_debug_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    snapshot->adc_available = s_adc_state.initialized;
    snapshot->raw = s_adc_state.last_raw;
    snapshot->voltage_mv = s_adc_state.last_voltage_mv;
    snapshot->decoded_key = s_adc_state.last_decoded_key;
}

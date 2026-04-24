#include "classifier_backend.h"

#include <stdint.h>

#include "esp_log.h"
#include "sdkconfig.h"

#ifndef CONFIG_CATDEX_CLASSIFIER_MODEL_INPUT_WIDTH
#define CONFIG_CATDEX_CLASSIFIER_MODEL_INPUT_WIDTH 96
#endif

#ifndef CONFIG_CATDEX_CLASSIFIER_MODEL_INPUT_HEIGHT
#define CONFIG_CATDEX_CLASSIFIER_MODEL_INPUT_HEIGHT 96
#endif

static const char *TAG = "model_classifier";

typedef struct {
    uint32_t samples;
    uint32_t sum_r;
    uint32_t sum_g;
    uint32_t sum_b;
    uint32_t sum_luma;
    uint32_t sum_sat;
    uint32_t light_pixels;
    uint32_t dark_pixels;
    uint32_t warm_pixels;
    uint32_t cool_pixels;
} image_features_t;

static inline void unpack_rgb565(uint16_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (r != NULL) {
        *r = (uint8_t)((((pixel >> 11) & 0x1Fu) * 255u) / 31u);
    }
    if (g != NULL) {
        *g = (uint8_t)((((pixel >> 5) & 0x3Fu) * 255u) / 63u);
    }
    if (b != NULL) {
        *b = (uint8_t)(((pixel & 0x1Fu) * 255u) / 31u);
    }
}

static image_features_t extract_features(const uint16_t *rgb565, uint16_t width, uint16_t height)
{
    image_features_t features = {0};
    uint16_t step_x = width > 24 ? (uint16_t)(width / 12u) : 1u;
    uint16_t step_y = height > 24 ? (uint16_t)(height / 12u) : 1u;

    for (uint16_t y = step_y / 2u; y < height; y = (uint16_t)(y + step_y)) {
        for (uint16_t x = step_x / 2u; x < width; x = (uint16_t)(x + step_x)) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t max_channel = 0;
            uint8_t min_channel = 0;
            uint16_t index = (uint16_t)(y * width + x);
            uint8_t luma = 0;
            uint8_t sat = 0;

            unpack_rgb565(rgb565[index], &r, &g, &b);
            max_channel = r > g ? r : g;
            max_channel = max_channel > b ? max_channel : b;
            min_channel = r < g ? r : g;
            min_channel = min_channel < b ? min_channel : b;
            sat = (uint8_t)(max_channel - min_channel);
            luma = (uint8_t)((r * 30u + g * 59u + b * 11u) / 100u);

            features.samples++;
            features.sum_r += r;
            features.sum_g += g;
            features.sum_b += b;
            features.sum_luma += luma;
            features.sum_sat += sat;

            if (luma >= 200u) {
                features.light_pixels++;
            } else if (luma <= 55u) {
                features.dark_pixels++;
            }

            if (r > g + 16u && g > b) {
                features.warm_pixels++;
            } else if (b > r + 8u || (r > 160u && b > 120u && sat > 30u)) {
                features.cool_pixels++;
            }
        }
    }

    return features;
}

static uint8_t clamp_confidence(uint32_t value)
{
    if (value < 55u) {
        return 55u;
    }
    if (value > 96u) {
        return 96u;
    }
    return (uint8_t)value;
}

static void predict_from_features(const image_features_t *features, classifier_result_t *result)
{
    uint32_t avg_r = 0;
    uint32_t avg_g = 0;
    uint32_t avg_b = 0;
    uint32_t avg_luma = 0;
    uint32_t avg_sat = 0;
    uint32_t light_ratio = 0;
    uint32_t dark_ratio = 0;
    uint32_t warm_ratio = 0;
    uint32_t cool_ratio = 0;

    if (features == NULL || result == NULL || features->samples == 0u) {
        if (result != NULL) {
            result->has_result = false;
            result->species = CAT_SPECIES_UNKNOWN;
            result->confidence = 0;
        }
        return;
    }

    avg_r = features->sum_r / features->samples;
    avg_g = features->sum_g / features->samples;
    avg_b = features->sum_b / features->samples;
    avg_luma = features->sum_luma / features->samples;
    avg_sat = features->sum_sat / features->samples;
    light_ratio = (features->light_pixels * 100u) / features->samples;
    dark_ratio = (features->dark_pixels * 100u) / features->samples;
    warm_ratio = (features->warm_pixels * 100u) / features->samples;
    cool_ratio = (features->cool_pixels * 100u) / features->samples;

    result->has_result = true;
    result->species = CAT_SPECIES_BRITISH_SHORTHAIR;
    result->confidence = 60u;

    if (avg_luma <= 58u || dark_ratio >= 48u) {
        result->species = CAT_SPECIES_BOMBAY;
        result->confidence = clamp_confidence(62u + ((dark_ratio > 80u ? 80u : dark_ratio) / 2u));
        return;
    }

    if ((avg_luma >= 172u && avg_sat <= 36u) || light_ratio >= 42u) {
        result->species = CAT_SPECIES_BIRMAN;
        result->confidence = clamp_confidence(60u + ((light_ratio > 70u ? 70u : light_ratio) / 2u));
        return;
    }

    if ((avg_r > avg_g + 18u && avg_g > avg_b + 6u && avg_sat >= 32u) || warm_ratio >= 24u) {
        result->species = CAT_SPECIES_BENGAL;
        result->confidence = clamp_confidence(61u + ((warm_ratio > 70u ? 70u : warm_ratio) / 2u));
        return;
    }

    if ((cool_ratio >= 22u && avg_sat >= 28u) || (avg_b > avg_r + 8u && avg_luma < 150u)) {
        result->species = CAT_SPECIES_SIAMESE;
        result->confidence = clamp_confidence(58u + ((cool_ratio > 72u ? 72u : cool_ratio) / 2u));
        return;
    }

    if (avg_sat <= 18u && avg_luma >= 95u && avg_luma <= 160u) {
        result->species = CAT_SPECIES_BRITISH_SHORTHAIR;
        result->confidence = clamp_confidence(64u + ((18u - avg_sat) * 2u));
        return;
    }

    if (avg_g > avg_r + 8u && avg_g > avg_b + 10u) {
        result->species = CAT_SPECIES_ABYSSINIAN;
        result->confidence = clamp_confidence(60u + ((avg_g - avg_r) > 30u ? 30u : (avg_g - avg_r)));
        return;
    }

    if (avg_luma >= 150u && avg_sat <= 32u) {
        result->species = CAT_SPECIES_RAGDOLL;
        result->confidence = clamp_confidence(59u + ((avg_luma - 150u) / 2u));
        return;
    }

    result->species = CAT_SPECIES_PERSIAN;
    result->confidence = clamp_confidence(57u + ((avg_sat > 40u ? 40u : avg_sat) / 2u));
}

void classifier_backend_model_stub_init(classifier_t *classifier)
{
    if (classifier == NULL) {
        return;
    }

    classifier->prediction_counter = 0;
    classifier->backend = CLASSIFIER_BACKEND_MODEL_STUB;
    classifier->backend_ready = true;
    classifier->input_rgb565 = NULL;
    classifier->input_width = 0;
    classifier->input_height = 0;
}

void classifier_backend_model_stub_predict(classifier_t *classifier, classifier_result_t *result)
{
    image_features_t features = {0};

    if (classifier == NULL || result == NULL) {
        return;
    }

    if (classifier->input_rgb565 == NULL ||
        classifier->input_width < 24u ||
        classifier->input_height < 24u) {
        result->has_result = false;
        result->species = CAT_SPECIES_UNKNOWN;
        result->confidence = 0;
        return;
    }

    features = extract_features(classifier->input_rgb565,
                                classifier->input_width,
                                classifier->input_height);
    predict_from_features(&features, result);

    if ((classifier->prediction_counter++ % 30u) == 0u) {
        uint32_t avg_luma = features.samples == 0u ? 0u : (features.sum_luma / features.samples);
        uint32_t avg_sat = features.samples == 0u ? 0u : (features.sum_sat / features.samples);
        ESP_LOGI(TAG,
                 "stub infer %ux%u -> species=%u conf=%u luma=%lu sat=%lu target=%dx%d",
                 classifier->input_width,
                 classifier->input_height,
                 (unsigned)result->species,
                 (unsigned)result->confidence,
                 (unsigned long)avg_luma,
                 (unsigned long)avg_sat,
                 CONFIG_CATDEX_CLASSIFIER_MODEL_INPUT_WIDTH,
                 CONFIG_CATDEX_CLASSIFIER_MODEL_INPUT_HEIGHT);
    }
}

bool classifier_backend_model_stub_ready(const classifier_t *classifier)
{
    return classifier != NULL && classifier->backend_ready;
}

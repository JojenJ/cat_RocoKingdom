#include "classifier_backend.h"

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#ifdef CONFIG_CATDEX_CLASSIFIER_BACKEND_ESPDL

#include "dl_model_base.hpp"
#include "dl_tensor_base.hpp"
#include "fbs_model.hpp"

static const char *TAG = "espdl";

#define MODEL_INPUT_W  96
#define MODEL_INPUT_H  96
#define MODEL_CLASSES  12

static const char *const k_labels[MODEL_CLASSES] = {
    "Abyssinian",       /* 0 */
    "Bengal",           /* 1 */
    "Birman",           /* 2 */
    "Bombay",           /* 3 */
    "British Shorthair",/* 4 */
    "Egyptian Mau",     /* 5 */
    "Maine Coon",       /* 6 */
    "Persian",          /* 7 */
    "Ragdoll",          /* 8 */
    "Russian Blue",     /* 9 */
    "Siamese",          /* 10 */
    "Sphynx",           /* 11 */
};

static const CatSpecies k_species[MODEL_CLASSES] = {
    CAT_SPECIES_ABYSSINIAN,
    CAT_SPECIES_BENGAL,
    CAT_SPECIES_BIRMAN,
    CAT_SPECIES_BOMBAY,
    CAT_SPECIES_BRITISH_SHORTHAIR,
    CAT_SPECIES_EGYPTIAN_MAU,
    CAT_SPECIES_MAINE_COON,
    CAT_SPECIES_PERSIAN,
    CAT_SPECIES_RAGDOLL,
    CAT_SPECIES_RUSSIAN_BLUE,
    CAT_SPECIES_SIAMESE,
    CAT_SPECIES_SPHYNX,
};

static dl::Model *s_model = NULL;
static uint8_t s_input_buf[MODEL_INPUT_W * MODEL_INPUT_H * 3];

/* ── Debug flags ────────────────────────────────────────────────────────────
 * AI_DBG_LOG_INPUT   : print input buffer stats every N inferences
 * AI_DBG_LOG_OUTPUT  : print full 12-dim output vector + top3
 * AI_DBG_SHOW_INPUT  : copy model input back to a global RGB565 buffer
 *                      so the render task can blit it to LCD
 * AI_DBG_FIXED_IMAGE : skip camera, feed a solid grey test pattern
 *                      Set to 1 to test model/quantization in isolation.
 * ────────────────────────────────────────────────────────────────────────── */
#ifndef AI_DBG_LOG_INPUT
#define AI_DBG_LOG_INPUT   1   /* 1 = enabled */
#endif
#ifndef AI_DBG_LOG_OUTPUT
#define AI_DBG_LOG_OUTPUT  1
#endif
#ifndef AI_DBG_SHOW_INPUT
#define AI_DBG_SHOW_INPUT  1
#endif
#ifndef AI_DBG_FIXED_IMAGE
#define AI_DBG_FIXED_IMAGE 0   /* set to 1 to bypass camera */
#endif

#define AI_DBG_LOG_EVERY   5   /* log every N inferences */

/* Exposed so render task can blit it: 96x96 RGB565, valid when AI_DBG_SHOW_INPUT=1 */
uint16_t g_ai_debug_input_rgb565[MODEL_INPUT_W * MODEL_INPUT_H];
volatile bool g_ai_debug_input_ready = false;

extern const uint8_t _binary_student96_w1_s3_ptq_espdl_start[] __attribute__((aligned(16)));
extern const uint8_t _binary_student96_w1_s3_ptq_espdl_end[];

/* Center-square crop + nearest-neighbor resize + RGB565→RGB888.
 * Crops the largest centered square from src to avoid stretch distortion. */
static void rgb565_to_rgb888_96x96(const uint16_t *src, uint16_t sw, uint16_t sh, uint8_t *dst)
{
    /* center-square crop */
    int crop = sw < sh ? sw : sh;
    int ox = ((int)sw - crop) / 2;
    int oy = ((int)sh - crop) / 2;

    for (int dy = 0; dy < MODEL_INPUT_H; dy++) {
        int sy = oy + (dy * crop) / MODEL_INPUT_H;
        for (int dx = 0; dx < MODEL_INPUT_W; dx++) {
            int sx = ox + (dx * crop) / MODEL_INPUT_W;
            /* OV2640 RGB565 is big-endian on the wire; esp_camera gives it
             * byte-swapped already on ESP32-S3, so no manual swap needed.
             * If colors look wrong, toggle the __builtin_bswap16 below. */
            uint16_t px = src[sy * sw + sx];
            int base = (dy * MODEL_INPUT_W + dx) * 3;
            dst[base]     = (uint8_t)(((px >> 11) & 0x1F) * 255 / 31); /* R */
            dst[base + 1] = (uint8_t)(((px >>  5) & 0x3F) * 255 / 63); /* G */
            dst[base + 2] = (uint8_t)((px & 0x1F) * 255 / 31);         /* B */
        }
    }
}

#if AI_DBG_FIXED_IMAGE
/* Grey ramp test pattern: top-left dark, bottom-right bright.
 * On a correctly deployed model this should produce low but non-uniform scores.
 * If all scores are identical, output parsing is broken. */
static void fill_fixed_test_pattern(uint8_t *dst)
{
    for (int y = 0; y < MODEL_INPUT_H; y++) {
        for (int x = 0; x < MODEL_INPUT_W; x++) {
            uint8_t v = (uint8_t)(((x + y) * 255) / (MODEL_INPUT_W + MODEL_INPUT_H - 2));
            int base = (y * MODEL_INPUT_W + x) * 3;
            dst[base] = dst[base+1] = dst[base+2] = v;
        }
    }
}
#endif

/* Convert RGB888 96x96 → RGB565 for LCD preview */
static void rgb888_to_rgb565_96x96(const uint8_t *src, uint16_t *dst)
{
    for (int i = 0; i < MODEL_INPUT_W * MODEL_INPUT_H; i++) {
        uint8_t r = src[i*3], g = src[i*3+1], b = src[i*3+2];
        dst[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
}

static void dbg_log_input_stats(const uint8_t *buf, int n)
{
    int mn = 255, mx = 0; long sum = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] < mn) mn = buf[i];
        if (buf[i] > mx) mx = buf[i];
        sum += buf[i];
    }
    ESP_LOGI(TAG, "INPUT rgb888 min=%d max=%d mean=%ld", mn, mx, sum/n);
    /* first 12 values */
    ESP_LOGI(TAG, "INPUT[0..11]: %d %d %d %d %d %d %d %d %d %d %d %d",
             buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],
             buf[6],buf[7],buf[8],buf[9],buf[10],buf[11]);
}

static void dbg_log_output(const float *scores, int n, int top1)
{
    ESP_LOGI(TAG, "OUTPUT raw: %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
             scores[0],scores[1],scores[2],scores[3],scores[4],scores[5],
             scores[6],scores[7],scores[8],scores[9],scores[10],scores[11]);
    /* top3 */
    int t[3] = {0,1,2};
    for (int i = 0; i < n; i++) {
        if (scores[i] > scores[t[0]]) { t[2]=t[1]; t[1]=t[0]; t[0]=i; }
        else if (scores[i] > scores[t[1]]) { t[2]=t[1]; t[1]=i; }
        else if (scores[i] > scores[t[2]]) { t[2]=i; }
    }
    ESP_LOGI(TAG, "TOP3: [%d]%s=%.3f [%d]%s=%.3f [%d]%s=%.3f",
             t[0],k_labels[t[0]],scores[t[0]],
             t[1],k_labels[t[1]],scores[t[1]],
             t[2],k_labels[t[2]],scores[t[2]]);
    (void)top1;
}

void classifier_backend_espdl_init(classifier_t *classifier)
{
    if (!classifier) return;

    classifier->backend = CLASSIFIER_BACKEND_ESPDL;
    classifier->backend_ready = false;
    classifier->input_rgb565 = NULL;
    classifier->input_width = 0;
    classifier->input_height = 0;
    classifier->prediction_counter = 0;

    size_t sz = (size_t)(_binary_student96_w1_s3_ptq_espdl_end -
                         _binary_student96_w1_s3_ptq_espdl_start);
    ESP_LOGI(TAG, "loading model %u bytes", (unsigned)sz);

    s_model = new dl::Model((const char *)_binary_student96_w1_s3_ptq_espdl_start,
                            fbs::MODEL_LOCATION_IN_FLASH_RODATA);
    if (!s_model) {
        ESP_LOGE(TAG, "model alloc failed");
        return;
    }

    classifier->backend_ready = true;
    ESP_LOGI(TAG, "model ready");
}

void classifier_backend_espdl_predict(classifier_t *classifier, classifier_result_t *result)
{
    if (!classifier || !result) return;

    result->has_result = false;
    result->species = CAT_SPECIES_UNKNOWN;
    result->confidence = 0;

    if (!classifier->backend_ready || !s_model ||
        !classifier->input_rgb565 ||
        classifier->input_width < 8 || classifier->input_height < 8) {
        return;
    }

    int64_t t0 = esp_timer_get_time();

#if AI_DBG_FIXED_IMAGE
    fill_fixed_test_pattern(s_input_buf);
    ESP_LOGW(TAG, "*** FIXED TEST PATTERN MODE — camera bypassed ***");
#else
    rgb565_to_rgb888_96x96(classifier->input_rgb565,
                           classifier->input_width,
                           classifier->input_height,
                           s_input_buf);
#endif

#if AI_DBG_SHOW_INPUT
    rgb888_to_rgb565_96x96(s_input_buf, g_ai_debug_input_rgb565);
    g_ai_debug_input_ready = true;
#endif

#if AI_DBG_LOG_INPUT
    if ((classifier->prediction_counter % AI_DBG_LOG_EVERY) == 0) {
        ESP_LOGI(TAG, "INPUT src=%ux%u -> 96x96 crop+resize",
                 classifier->input_width, classifier->input_height);
        dbg_log_input_stats(s_input_buf, MODEL_INPUT_W * MODEL_INPUT_H * 3);
    }
#endif

    /* Get model's pre-allocated input tensor and fill it */
    dl::TensorBase *in_tensor = s_model->get_input();
    if (!in_tensor) {
        ESP_LOGE(TAG, "no input tensor");
        return;
    }

    int8_t *tdata = in_tensor->get_element_ptr<int8_t>();
    int in_exp = in_tensor->get_exponent();
    int n = MODEL_INPUT_H * MODEL_INPUT_W * 3;

    static bool s_in_params_logged = false;
    if (!s_in_params_logged) {
        s_in_params_logged = true;
        ESP_LOGI(TAG, "INPUT TENSOR exponent=%d scale=2^%d int8_range=[-128,127]", in_exp, in_exp);
    }

    /* ImageNet mean/std normalize then quantize.
     * Matches standard torchvision transforms: mean=[0.485,0.456,0.406] std=[0.229,0.224,0.225]
     * normalized = (pixel/255 - mean) / std  -> range approx [-2.1, 2.6]
     * int8 = round(normalized / 2^exp)
     * If your training used different mean/std, update these constants. */
    static const float k_mean[3] = {0.485f, 0.456f, 0.406f};
    static const float k_std[3]  = {0.229f, 0.224f, 0.225f};
    float in_scale = powf(2.0f, (float)in_exp);

    float norm_min = 1e9f, norm_max = -1e9f;
    for (int i = 0; i < n; i++) {
        int ch = i % 3;
        float norm = (s_input_buf[i] / 255.0f - k_mean[ch]) / k_std[ch];
        if (norm < norm_min) norm_min = norm;
        if (norm > norm_max) norm_max = norm;
        int v = (int)roundf(norm / in_scale);
        tdata[i] = (int8_t)(v > 127 ? 127 : (v < -128 ? -128 : v));
    }

#if AI_DBG_LOG_INPUT
    if ((classifier->prediction_counter % AI_DBG_LOG_EVERY) == 0) {
        ESP_LOGI(TAG, "NORM range [%.3f, %.3f] -> int8 [%d, %d]",
                 norm_min, norm_max,
                 (int)(int8_t)(norm_min/in_scale < -128 ? -128 : norm_min/in_scale),
                 (int)(int8_t)(norm_max/in_scale >  127 ?  127 : norm_max/in_scale));
    }
#endif

    s_model->run();

    dl::TensorBase *out = s_model->get_output();
    if (!out) {
        ESP_LOGE(TAG, "no output tensor");
        return;
    }

    std::vector<int> shape = out->get_shape();
    int num_classes = shape.back();
    if (num_classes > MODEL_CLASSES) num_classes = MODEL_CLASSES;

    float scores[MODEL_CLASSES];
    if (out->dtype == dl::DATA_TYPE_FLOAT) {
        float *fptr = out->get_element_ptr<float>();
        for (int i = 0; i < num_classes; i++) scores[i] = fptr[i];
    } else {
        /* int8 quantized: value * 2^exponent */
        int8_t *iptr = out->get_element_ptr<int8_t>();
        int exp = out->get_exponent();
        float scale = exp >= 0 ? (float)(1 << exp) : 1.0f / (float)(1 << (-exp));
        ESP_LOGD(TAG, "out dtype=%d exponent=%d scale=%.6f", (int)out->dtype, exp, scale);
        for (int i = 0; i < num_classes; i++) scores[i] = iptr[i] * scale;
    }

    /* top1 */
    int top1 = 0;
    for (int i = 1; i < num_classes; i++) {
        if (scores[i] > scores[top1]) top1 = i;
    }

    /* softmax confidence */
    float max_s = scores[top1];
    float sum = 0.0f;
    float exp_scores[MODEL_CLASSES];
    for (int i = 0; i < num_classes; i++) {
        exp_scores[i] = expf(scores[i] - max_s);
        sum += exp_scores[i];
    }
    uint8_t conf = (sum > 0.0f) ? (uint8_t)(exp_scores[top1] / sum * 100.0f) : 0;

    int64_t ms = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "infer %lldms -> [%d] %s conf=%u%%", ms, top1, k_labels[top1], conf);

#if AI_DBG_LOG_OUTPUT
    if ((classifier->prediction_counter % AI_DBG_LOG_EVERY) == 0) {
        dbg_log_output(scores, num_classes, top1);
        /* softmax probabilities */
        if (sum > 0.0f) {
            ESP_LOGI(TAG, "SOFTMAX: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f",
                exp_scores[0]/sum, exp_scores[1]/sum, exp_scores[2]/sum,
                exp_scores[3]/sum, exp_scores[4]/sum, exp_scores[5]/sum,
                exp_scores[6]/sum, exp_scores[7]/sum, exp_scores[8]/sum,
                exp_scores[9]/sum, exp_scores[10]/sum, exp_scores[11]/sum);
        }
    }
#endif

    result->has_result = true;
    result->species = k_species[top1];
    result->confidence = conf;
    classifier->prediction_counter++;
}

bool classifier_backend_espdl_ready(const classifier_t *classifier)
{
    return classifier && classifier->backend_ready && s_model;
}

#endif /* CONFIG_CATDEX_CLASSIFIER_BACKEND_ESPDL */

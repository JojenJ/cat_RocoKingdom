#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common/project_types.h"

typedef struct {
    bool has_result;
    CatSpecies species;
    uint8_t confidence;
} classifier_result_t;

typedef enum {
    CLASSIFIER_BACKEND_MOCK = 0,
    CLASSIFIER_BACKEND_MODEL_STUB,
    CLASSIFIER_BACKEND_ESPDL,
} classifier_backend_t;

typedef struct {
    uint32_t prediction_counter;
    classifier_backend_t backend;
    bool backend_ready;
    const uint16_t *input_rgb565;
    uint16_t input_width;
    uint16_t input_height;
} classifier_t;

void classifier_init(classifier_t *classifier);
void classifier_set_input_rgb565(classifier_t *classifier,
                                 const uint16_t *rgb565,
                                 uint16_t width,
                                 uint16_t height);
void classifier_predict(classifier_t *classifier, classifier_result_t *result);
const char *classifier_backend_name(const classifier_t *classifier);
bool classifier_backend_ready(const classifier_t *classifier);

/* Debug: 96x96 RGB565 of the last image fed to the model.
 * Valid only when AI_DBG_SHOW_INPUT=1 (default on).
 * Read from render task to blit to LCD for visual verification. */
#ifdef __cplusplus
extern "C" {
#endif
extern uint16_t g_ai_debug_input_rgb565[96 * 96];
extern volatile bool g_ai_debug_input_ready;
#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>

#include "ai/classifier.h"

#ifdef __cplusplus
extern "C" {
#endif

void classifier_backend_mock_init(classifier_t *classifier);
void classifier_backend_mock_predict(classifier_t *classifier, classifier_result_t *result);
bool classifier_backend_mock_ready(const classifier_t *classifier);

void classifier_backend_model_stub_init(classifier_t *classifier);
void classifier_backend_model_stub_predict(classifier_t *classifier, classifier_result_t *result);
bool classifier_backend_model_stub_ready(const classifier_t *classifier);

void classifier_backend_espdl_init(classifier_t *classifier);
void classifier_backend_espdl_predict(classifier_t *classifier, classifier_result_t *result);
bool classifier_backend_espdl_ready(const classifier_t *classifier);

#ifdef __cplusplus
}
#endif

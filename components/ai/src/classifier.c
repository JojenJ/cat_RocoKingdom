#include "ai/classifier.h"

#include "classifier_backend.h"
#include "sdkconfig.h"

static const classifier_backend_t k_default_backend =
#if CONFIG_CATDEX_CLASSIFIER_BACKEND_ESPDL
    CLASSIFIER_BACKEND_ESPDL;
#elif CONFIG_CATDEX_CLASSIFIER_BACKEND_MODEL_STUB
    CLASSIFIER_BACKEND_MODEL_STUB;
#else
    CLASSIFIER_BACKEND_MOCK;
#endif

void classifier_init(classifier_t *classifier)
{
    if (classifier == NULL) {
        return;
    }

    classifier->input_rgb565 = NULL;
    classifier->input_width = 0;
    classifier->input_height = 0;

    switch (k_default_backend) {
        case CLASSIFIER_BACKEND_ESPDL:
            classifier_backend_espdl_init(classifier);
            break;
        case CLASSIFIER_BACKEND_MODEL_STUB:
            classifier_backend_model_stub_init(classifier);
            break;
        case CLASSIFIER_BACKEND_MOCK:
        default:
            classifier_backend_mock_init(classifier);
            break;
    }
}

void classifier_set_input_rgb565(classifier_t *classifier,
                                 const uint16_t *rgb565,
                                 uint16_t width,
                                 uint16_t height)
{
    if (classifier == NULL) {
        return;
    }

    classifier->input_rgb565 = rgb565;
    classifier->input_width = width;
    classifier->input_height = height;
}

void classifier_predict(classifier_t *classifier, classifier_result_t *result)
{
    if (classifier == NULL || result == NULL) {
        return;
    }

    switch (classifier->backend) {
        case CLASSIFIER_BACKEND_ESPDL:
            classifier_backend_espdl_predict(classifier, result);
            break;
        case CLASSIFIER_BACKEND_MODEL_STUB:
            classifier_backend_model_stub_predict(classifier, result);
            break;
        case CLASSIFIER_BACKEND_MOCK:
        default:
            classifier_backend_mock_predict(classifier, result);
            break;
    }
}

const char *classifier_backend_name(const classifier_t *classifier)
{
    if (classifier == NULL) {
        return "unknown";
    }

    switch (classifier->backend) {
        case CLASSIFIER_BACKEND_ESPDL:
            return "espdl";
        case CLASSIFIER_BACKEND_MODEL_STUB:
            return "model-stub";
        case CLASSIFIER_BACKEND_MOCK:
        default:
            return "mock";
    }
}

bool classifier_backend_ready(const classifier_t *classifier)
{
    if (classifier == NULL) {
        return false;
    }

    switch (classifier->backend) {
        case CLASSIFIER_BACKEND_ESPDL:
            return classifier_backend_espdl_ready(classifier);
        case CLASSIFIER_BACKEND_MODEL_STUB:
            return classifier_backend_model_stub_ready(classifier);
        case CLASSIFIER_BACKEND_MOCK:
        default:
            return classifier_backend_mock_ready(classifier);
    }
}

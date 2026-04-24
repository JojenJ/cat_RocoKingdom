#include "classifier_backend.h"

#include "esp_random.h"

void classifier_backend_mock_init(classifier_t *classifier)
{
    if (classifier == NULL) {
        return;
    }
    classifier->prediction_counter = 0;
    classifier->backend = CLASSIFIER_BACKEND_MOCK;
    classifier->backend_ready = true;
    classifier->input_rgb565 = NULL;
    classifier->input_width = 0;
    classifier->input_height = 0;
}

void classifier_backend_mock_predict(classifier_t *classifier, classifier_result_t *result)
{
    if (classifier == NULL || result == NULL) {
        return;
    }

    uint32_t step = classifier->prediction_counter++;
    result->has_result = true;
    result->species = (CatSpecies)(step % CAT_SPECIES_COUNT);
    result->confidence = (uint8_t)(55 + (esp_random() % 42));
}

bool classifier_backend_mock_ready(const classifier_t *classifier)
{
    return classifier != NULL && classifier->backend_ready;
}

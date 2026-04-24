#pragma once

#include <stdbool.h>

typedef enum {
    INPUT_KEY_NONE = 0,
    INPUT_KEY_UP,
    INPUT_KEY_DOWN,
    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_CONFIRM,
    INPUT_KEY_BACK
} input_key_t;

typedef struct {
    bool adc_available;
    int raw;
    int voltage_mv;
    input_key_t decoded_key;
} input_debug_snapshot_t;

void input_hal_init(void);
input_key_t input_hal_poll(void);
bool input_hal_demo_enabled(void);
void input_hal_get_debug_snapshot(input_debug_snapshot_t *snapshot);

#pragma once

#include "driver/gpio.h"

/*
 * These defaults are placeholders based on common ESP32-S3-EYE style bring-up assumptions.
 * Replace them after verifying the real board schematic or BSP.
 */
#define CATDEX_BUTTON_UP_GPIO GPIO_NUM_NC
#define CATDEX_BUTTON_DOWN_GPIO GPIO_NUM_NC
#define CATDEX_BUTTON_CONFIRM_GPIO GPIO_NUM_0
#define CATDEX_BUTTON_BACK_GPIO GPIO_NUM_NC

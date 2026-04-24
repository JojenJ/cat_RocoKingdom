# CatDex

CatDex is an ESP-IDF based MVP for an ESP32-S3-EYE pet-dex style cat capture game.

This repository currently contains Stage 1:

- modular project skeleton
- menu/page state machine
- mock classifier
- mock capture flow
- NVS backed save storage
- text-mode display HAL placeholder

Stage 2 adds:

- ESP32-S3-EYE schematic-based board profile
- real ADC ladder keypad decoding on `GPIO1 / ADC1_CH0`
- explicit camera pin map from the motherboard schematic
- configurable hardware options in `menuconfig`
- `esp32-camera` component integration with real-camera fallback to mock status
- capture page background camera polling and richer system info diagnostics

Stage 3 starts with:

- official ESP32-S3-EYE BSP-based LCD pin assumptions folded into `board_profile`
- optional ST7789 `esp_lcd` backend with serial-log fallback
- lightweight on-panel text-band renderer for bring-up validation

## Build

1. Install ESP-IDF 5.x and export the environment.
2. Set target:

```bash
idf.py set-target esp32s3
```

3. Build:

```bash
idf.py build
```

4. Flash and monitor:

```bash
idf.py -p PORT flash monitor
```

## Notes

- This stage still uses a text display HAL placeholder so the project can compile before real LCD integration.
- Button GPIOs and LCD/camera wiring are intentionally abstracted behind `drivers/` and `camera/`.
- A timed demo input script is enabled by default so the app can demonstrate page transitions and mock capture without confirmed button pin mappings.
- The camera path now attempts real `esp32-camera` initialization first and falls back to mock mode if the daughterboard wiring or sensor reset/power pins differ from the current assumptions.
- The display path can now attempt a real ST7789 LCD init when `CatDex -> Use text log display backend only` is disabled in `menuconfig`.
- See [docs/esp32_s3_eye_stage2_notes.md](/c:/Users/wwwcz/Desktop/esp32code/cat_RocoKingdom/docs/esp32_s3_eye_stage2_notes.md) for the Stage 2 hardware notes extracted from the motherboard schematic.

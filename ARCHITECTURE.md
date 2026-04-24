# Roco Kingdom — Architecture Overview

## Hardware Target

| Component | Part |
|-----------|------|
| MCU | ESP32-S3 (dual-core, 160 MHz) |
| Board | ESP32-S3-EYE |
| Camera | OV2640, RGB565, QQVGA (160×120) |
| Display | ST7789 LCD, 240×240, SPI |
| Input | ADC ladder keypad on GPIO1 |
| Memory | 8 MB PSRAM, 8 MB Flash |

---

## Project Structure

```
cat_RocoKingdom/
├── main/
│   └── main.c                  # Entry point: NVS init → app_controller
├── components/
│   ├── ai/                     # AI inference component
│   │   ├── include/ai/
│   │   │   └── classifier.h    # Public API + debug buffer declarations
│   │   └── src/
│   │       ├── classifier.c              # Backend dispatcher
│   │       ├── classifier_backend.h      # Internal backend interface
│   │       ├── model_classifier_espdl.cpp # ESP-DL inference backend (real)
│   │       ├── model_classifier_stub.c   # Stub backend
│   │       └── mock_classifier.c         # Mock backend (no model)
│   ├── app/                    # Application controller
│   │   └── src/app_controller.c  # Page state machine + FreeRTOS tasks
│   ├── camera/                 # Camera service
│   │   └── src/camera_service.c  # OV2640 init, frame polling, preview buffer
│   ├── common/                 # Shared types and config
│   │   └── include/common/
│   │       ├── project_types.h   # CatSpecies, CapturedCat, GameSaveData
│   │       └── app_config.h      # Build-time constants
│   ├── drivers/                # Hardware abstraction
│   │   ├── display_hal         # ST7789 LCD driver (BSP-based)
│   │   ├── input_hal           # ADC keypad decoder
│   │   └── board_profile       # Pin map for ESP32-S3-EYE
│   ├── game/                   # Game logic
│   │   └── game_service        # Capture rules, species data, save management
│   ├── storage/                # Persistence
│   │   └── save_storage        # SPIFFS-backed NVS save + thumbnail storage
│   └── ui/                     # UI rendering
│       └── ui_manager          # Page renderer, view model
└── model/
    └── student96_w1_s3_ptq_espdl.espdl  # PTQ int8 MobileNetV2 (linked as rodata)
```

---

## AI Inference Pipeline

```
OV2640 sensor
  │  PIXFORMAT_RGB565, FRAMESIZE_QQVGA (160×120)
  ▼
camera_service_poll()
  │  esp_camera_fb_get() → nearest-neighbor downscale → preview_rgb565 buffer
  ▼
ai_task_fn()  [700 ms interval, Core 1]
  │  camera_service_get_preview_rgb565() → (px, 160, 120)
  │  classifier_set_input_rgb565()
  │  classifier_predict()
  ▼
classifier_backend_espdl_predict()
  │
  ├─ rgb565_to_rgb888_96x96()
  │    Center-square crop (120×120) + nearest-neighbor resize → 96×96
  │    RGB565 → RGB888: R=(px>>11&0x1F)*255/31, G=(px>>5&0x3F)*255/63, B=(px&0x1F)*255/31
  │
  ├─ ImageNet normalize + quantize → int8 input tensor
  │    norm = (pixel/255 - mean[ch]) / std[ch]
  │    mean = [0.485, 0.456, 0.406],  std = [0.229, 0.224, 0.225]
  │    int8 = round(norm / 2^exponent),  exponent = -6
  │
  ├─ s_model->run()   [ESP-DL, ~1.2 s on 160 MHz]
  │
  ├─ output tensor dequantize → float scores[12]
  │    int8 * 2^exponent
  │
  └─ softmax → top1 → classifier_result_t { species, confidence 0–100 }
```

### Model Details

| Property | Value |
|----------|-------|
| Architecture | MobileNetV2 (student, knowledge distillation) |
| Input | 96×96 RGB, ImageNet normalized |
| Output | 12-class logits |
| Format | ESP-DL PTQ int8 (.espdl), linked as flash rodata |
| Classes | Abyssinian, Bengal, Birman, Bombay, British Shorthair, Egyptian Mau, Maine Coon, Persian, Ragdoll, Russian Blue, Siamese, Sphynx |

### Training Preprocessing (eval/deploy)

```
Resize(112×112) → CenterCrop(96×96) → ToTensor → Normalize(mean, std)
```

Calibration used `Resize(96×96)` directly — end-side crop+resize matches this.

---

## FreeRTOS Task Layout

| Task | Core | Period | Responsibility |
|------|------|--------|----------------|
| `cam` | 1 | 33 ms | `camera_service_poll()` — frame capture only |
| `ai` | 1 | 700 ms | inference, writes `current_prediction` under `predict_mutex` |
| `render` | 0 | 100 ms | LCD blit (camera preview or model input debug view) |
| main loop | 0 | 20 ms | input polling, page state machine |

Shared state is protected by two mutexes: `frame_mutex` (camera buffer) and `predict_mutex` (prediction result).

---

## Page State Machine

```
MAIN_MENU
  ├─► CAPTURE          camera + AI active; UP key toggles model-input debug view
  │     ├─► CAPTURE_RESULT   on successful capture (conf ≥ 35%)
  │     └─► CAPTURE_FAIL     on low confidence or no result
  ├─► DEX
  ├─► BACKPACK
  │     └─► CAT_DETAIL
  ├─► SYSTEM_INFO
  └─► SETTINGS
```

---

## Debug Facilities (model_classifier_espdl.cpp)

| Flag | Default | Effect |
|------|---------|--------|
| `AI_DBG_LOG_INPUT` | 1 | Print input min/max/mean + first 12 bytes every 5 inferences |
| `AI_DBG_LOG_OUTPUT` | 1 | Print full 12-dim output vector, top3, softmax distribution |
| `AI_DBG_SHOW_INPUT` | 1 | Copy 96×96 model input to `g_ai_debug_input_rgb565` for LCD preview |
| `AI_DBG_FIXED_IMAGE` | 0 | Bypass camera; feed grey-ramp test pattern to isolate model vs camera issues |

**LCD model-input preview**: on the Capture page, press UP to toggle between camera preview and the actual 96×96 image fed to the model.

---

## Storage

- **Save data**: NVS-backed SPIFFS at partition `storage` (`/storage`)
- **Thumbnails**: 80×80 RGB565 per captured cat, keyed by `unique_id`
- **Schema version**: `CATDEX_SCHEMA_VERSION = 1`
- **Capacity**: up to 64 captured cats (`CATDEX_MAX_CAPTURED`)

---

## Build

```bash
# Prerequisites: ESP-IDF 5.x
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Key menuconfig options (`idf.py menuconfig` → CatDex):

| Option | Purpose |
|--------|---------|
| `CATDEX_CLASSIFIER_BACKEND_ESPDL` | Enable real ESP-DL inference |
| `CATDEX_MIN_CONFIDENCE_TO_CAPTURE` | Minimum softmax % to allow capture (default 35) |
| `CATDEX_DISPLAY_TEXT_LOG_ONLY` | Fall back to serial-log display (no LCD) |

---

## Known Limitations / Next Steps

- **Model confidence is low on real camera input (~20–34%)** due to domain gap between Oxford-IIIT Pet training data (studio photos) and OV2640 real-world capture. Recommended fix: fine-tune with real camera images or apply test-time augmentation.
- Model flash alignment warning (`FbsLoader: address not aligned with 16 bytes`) — fix by aligning the `.espdl` binary section in the linker script.
- Inference latency ~1.2 s at 160 MHz — consider enabling 240 MHz or using `dl::Model` with PSRAM weight caching.

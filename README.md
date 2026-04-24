# Roco Kingdom

A real-time cat breed classification game running entirely on an **ESP32-S3-EYE** — no cloud, no Wi-Fi. Point the camera at a cat, the on-device AI identifies the breed, and you capture it to your Pokédex-style collection.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3, dual-core 160 MHz, 8 MB PSRAM, 8 MB Flash |
| Board | ESP32-S3-EYE v2.2 |
| Camera | OV2640 daughterboard, RGB565, QQVGA (160×120) |
| Display | ST7789 LCD, 240×240, SPI (CLK=21 MOSI=47 DC=43 CS=44 BL=48) |
| Input | 4-key ADC ladder on GPIO1 (UP≈0.38V, DN≈0.82V, PLAY≈1.98V, MENU≈2.41V) |

---

## System Architecture

```
┌─────────────────────────────────────────────────────┐
│                    app_controller                    │
│  ┌──────────┐   ┌──────────┐   ┌─────────────────┐  │
│  │ cam task │   │  ai task │   │  render task    │  │
│  │ Core 1   │   │  Core 1  │   │  Core 0         │  │
│  │  33 ms   │   │  700 ms  │   │  100 ms         │  │
│  └────┬─────┘   └────┬─────┘   └────────┬────────┘  │
│       │frame_mutex   │predict_mutex      │           │
│  ┌────▼─────┐   ┌────▼─────┐   ┌────────▼────────┐  │
│  │  camera  │   │classifier│   │  display_hal    │  │
│  │ service  │   │ (esp-dl) │   │  ST7789 / log   │  │
│  └──────────┘   └──────────┘   └─────────────────┘  │
│                                                      │
│  main loop (Core 0, 20 ms): input_hal → page FSM    │
└─────────────────────────────────────────────────────┘
```

### FreeRTOS Tasks

| Task | Core | Period | Role |
|------|------|--------|------|
| `cam` | 1 | 33 ms | `esp_camera_fb_get()` → preview buffer |
| `ai` | 1 | 700 ms | inference → `current_prediction` |
| `render` | 0 | 100 ms | LCD blit (preview or model-input debug) |
| main loop | 0 | 20 ms | keypad poll, page state machine |

Shared state is protected by `frame_mutex` and `predict_mutex`.

---

## UI & Page State Machine

```
MAIN_MENU
  ├─► CAPTURE          live camera + AI; UP toggles model-input debug view
  │     ├─► CAPTURE_RESULT   conf ≥ 35% → cat added to collection
  │     └─► CAPTURE_FAIL     low confidence or no detection
  ├─► DEX              species encyclopedia (12 breeds)
  ├─► BACKPACK         captured cat list
  │     └─► CAT_DETAIL       stats, photo, skills
  ├─► SYSTEM_INFO
  └─► SETTINGS
```

The display HAL supports two backends selected at build time:
- **ST7789 real LCD** via ESP-IDF BSP (`esp_lcd`)
- **Serial log fallback** for bring-up without LCD hardware

---

## Camera → LCD Pipeline

```
OV2640 (RGB565, 160×120)
  └─► esp_camera_fb_get()
        └─► update_preview_from_fb()   nearest-neighbor downscale
              └─► preview_rgb565 buffer (up to 160×120)
                    └─► display_hal_blit_preview()   240×240 LCD upscale
```

The preview buffer and the AI input buffer are **independent** — the display path never stalls inference and vice versa.

---

## AI Inference Pipeline

```
preview_rgb565 (160×120)
  └─► rgb565_to_rgb888_96x96()
        center-square crop (120×120) → nearest-neighbor resize → 96×96 RGB888
  └─► ImageNet normalize
        R_norm = (R/255 − 0.485) / 0.229
        G_norm = (G/255 − 0.456) / 0.224
        B_norm = (B/255 − 0.406) / 0.225
  └─► quantize to int8
        int8 = round(norm / 2^−6),  clamped to [−128, 127]
  └─► esp-dl s_model->run()   ~1.2 s @ 160 MHz
  └─► dequantize output: int8 × 2^exponent → float scores[12]
  └─► softmax → top-1 → confidence (0–100%)
```

### Classifier Backends

| Backend | Purpose |
|---------|---------|
| `ESPDL` | Real PTQ int8 model, production |
| `MODEL_STUB` | Fixed dummy output, bring-up |
| `MOCK` | Random results, UI testing |

---

## Model: Training & Quantization

### Architecture
- **MobileNetV2** student model, knowledge distillation from a larger MobileNetV2 teacher
- Input: 96×96 RGB, 3 channels
- Output: 12-class logits
- Parameters: ~2.4 MB (PTQ int8 `.espdl`)

### Dataset
- **Oxford-IIIT Pet Dataset** — 12 cat breeds, ~500 images/class
- Classes: Abyssinian, Bengal, Birman, Bombay, British Shorthair, Egyptian Mau, Maine Coon, Persian, Ragdoll, Russian Blue, Siamese, Sphynx

### Training Preprocessing
```python
# Training (with augmentation)
Resize(112) → RandomResizedCrop(96, scale=(0.6,1.0)) → RandomHorizontalFlip()
→ RandomRotation(8) → ColorJitter(0.15, 0.15, 0.10) → ToTensor()
→ Normalize(mean=[0.485,0.456,0.406], std=[0.229,0.224,0.225])
→ RandomErasing(p=0.15)

# Eval / deploy reference
Resize(112) → CenterCrop(96) → ToTensor()
→ Normalize(mean=[0.485,0.456,0.406], std=[0.229,0.224,0.225])
```

### Quantization
- Tool: `esp-ppq` (PTQ, per-tensor)
- Calibration: Oxford-IIIT Pet val set, `Resize(96×96)` + ImageNet normalize
- Format: `.espdl`, linked as flash rodata (`__attribute__((aligned(16)))`)
- Input tensor: `int8`, exponent=`-6` (scale = 1/64)

### Export Pipeline
```
PyTorch .pt → ONNX → esp-ppq PTQ → .espdl → linked into firmware
```

---

## Storage

- **SPIFFS** partition `storage` (512 KB) at offset `0x430000`
- Save data: up to 64 captured cats, NVS-backed, schema v1
- Thumbnails: 80×80 RGB565 per cat, keyed by `unique_id`

---

## Debug Tools

On the Capture page, press **UP** to toggle between camera preview and the actual 96×96 image fed to the model — lets you visually verify crop, color, and brightness.

Serial log flags in [components/ai/src/model_classifier_espdl.cpp](components/ai/src/model_classifier_espdl.cpp):

| Flag | Default | Effect |
|------|---------|--------|
| `AI_DBG_LOG_INPUT` | 1 | Input min/max/mean + normalize range every 5 inferences |
| `AI_DBG_LOG_OUTPUT` | 1 | Full 12-dim output vector, top3, softmax distribution |
| `AI_DBG_SHOW_INPUT` | 1 | Copy model input to LCD-previewable RGB565 buffer |
| `AI_DBG_FIXED_IMAGE` | 0 | Bypass camera; feed grey-ramp test pattern |

---

## Build

```bash
# Requires ESP-IDF 5.x
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Key `menuconfig` options (CatDex menu):

| Option | Default | Purpose |
|--------|---------|---------|
| `CATDEX_CLASSIFIER_BACKEND_ESPDL` | y | Enable real ESP-DL inference |
| `CATDEX_MIN_CONFIDENCE_TO_CAPTURE` | 35 | Minimum softmax % to allow capture |
| `CATDEX_DISPLAY_TEXT_LOG_ONLY` | n | Serial-log display fallback |

---

## Known Limitations

- **Confidence ~20–34% on real camera input** — domain gap between Oxford-IIIT Pet studio photos and OV2640 real-world capture. Fix: fine-tune with real camera images.
- Flash alignment warning (`FbsLoader: address not aligned with 16 bytes`) — fix by aligning `.espdl` section in linker script.
- Inference latency ~1.2 s at 160 MHz — can be reduced by enabling 240 MHz CPU frequency.

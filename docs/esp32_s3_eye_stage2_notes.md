# ESP32-S3-EYE Stage 2 Notes

This stage upgrades CatDex from a pure mock board abstraction to a schematic-based
ESP32-S3-EYE motherboard profile.

## Confirmed From `SCH_ESP32-S3-EYE-MB_20211201_V2.2.pdf`

- 4-key keypad is an ADC ladder on `GPIO1 / ADC1_CH0`
- Key ladder target voltages:
  - `UP+` about `0.38V`
  - `DN-` about `0.82V`
  - `PLAY` about `1.98V`
  - `MENU` about `2.41V`
- Camera pins confirmed on the motherboard:
  - `SDA=GPIO4`
  - `SCL=GPIO5`
  - `VSYNC=GPIO6`
  - `HREF=GPIO7`
  - `XCLK=GPIO15`
  - `PCLK=GPIO13`
  - `Y9=GPIO16`
  - `Y8=GPIO17`
  - `Y7=GPIO18`
  - `Y6=GPIO12`
  - `Y5=GPIO10`
  - `Y4=GPIO8`
  - `Y3=GPIO9`
  - `Y2=GPIO11`
- LCD motherboard connector pins exported to the daughterboard:
  - `J9: GPIO48, GPIO47, GPIO44, GPIO21, GPIO43`
  - `J8: VDD_3V3, GPIO0, GPIO3, GPIO45, GPIO46`

## Intentionally Left Configurable

- Camera `RESET` GPIO
- Camera `PWDN` GPIO
- LCD daughterboard controller semantics

Reason:
The motherboard schematic confirms the connector nets, but not enough daughterboard
detail to assert the final controller wiring with confidence. These are exposed via
`sdkconfig` options for safe bring-up instead of hard-coding a guess.

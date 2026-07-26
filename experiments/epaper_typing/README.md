# Portrait partial-refresh typing demo

This Arduino sketch types:

> the quick brown fox jumps over the lazy dog.

one character at a time on a vertically oriented Waveshare 2.13-inch V4
e-paper display. Each visible typing step uses the panel's partial-refresh
waveform. After the complete phrase has been shown, the sketch performs a full
white refresh to remove ghosting and starts again. Text uses a 5 × 7 bitmap
font at 1× scale.

The sketch is self-contained and uses only the `SPI` library supplied by the
ESP32 Arduino core. It uses the same wiring as the `epaper_pattern` experiment:

| E-paper | ESP32-S3 pin |
| --- | --- |
| VCC | 3V3 / VCC3V3 |
| GND | GND |
| DIN | GPIO11 |
| CLK | GPIO12 |
| CS | GPIO10 |
| DC | GPIO9 |
| RST | GPIO8 |
| BUSY | GPIO13 |

Leave the HAT's `BS` selector at `0` for 4-line SPI. Disconnect USB and battery
power before changing wiring or reseating the display ribbon.

## Build and upload

```sh
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  --output-dir build/epaper_typing \
  experiments/epaper_typing

arduino-cli upload \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  --port /dev/cu.usbmodem5B910474881 \
  --input-dir build/epaper_typing \
  experiments/epaper_typing
```

Serial diagnostics are available at 115200 baud. The USB port may have a
different name on another computer.

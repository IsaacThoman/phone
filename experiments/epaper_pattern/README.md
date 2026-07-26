# 2.13-inch V4 e-paper pattern

This Arduino sketch draws a single black-and-white calibration pattern and
then puts the e-paper controller to sleep. The image remains visible without
power.

It targets:

- Waveshare ESP32-S3-SIM7670G-4G (16 MB flash / 8 MB OPI PSRAM)
- Waveshare 2.13-inch e-Paper HAT V4 (122 × 250, SSD1680)
- ESP32 Arduino core 3.x

The sketch is self-contained; it only uses the `SPI` library included with the
ESP32 Arduino core. Its initialization and full-refresh sequence follow
Waveshare's official `epd2in13_V4` example.

## Wiring

Wire by the printed signal labels, not wire color:

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

Disconnect USB and battery power before changing the wiring. Leave the camera
disconnected because it shares these GPIOs.

## Build and upload

Install the ESP32 Arduino core, then run:

```sh
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  --output-dir build/epaper_pattern \
  experiments/epaper_pattern

arduino-cli upload \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  --port /dev/cu.usbmodem5B910474881 \
  --input-dir build/epaper_pattern \
  experiments/epaper_pattern
```

The serial monitor runs at 115200 baud:

```sh
arduino-cli monitor --port /dev/cu.usbmodem5B910474881 --config baudrate=115200
```

The port shown above is the ESP32 USB-to-serial interface on the tested board.
It may differ on another computer. Do not select one of the SIM7670 modem's
four `usbmodem000000000001*` interfaces.

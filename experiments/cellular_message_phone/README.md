# Cellular message phone

Firmware for the Waveshare ESP32-S3-SIM7670G-4G and 2.13-inch V4 e-paper
display. Every 30 seconds it:

1. fetches `GET /api/message` over the SIM7670G cellular connection;
2. refreshes the e-paper panel only when the message revision changes; and
3. posts non-sensitive connection telemetry to `POST /api/device/status`.

The firmware does not send IMEI, ICCID, IMSI, phone number, or location. HTTPS
traffic is encrypted, but this prototype disables server-certificate
verification because no CA bundle is installed in the modem.

The modem is configured at boot for the Hologram SIM:

- APN: `hologram`
- username, password, and PIN: none
- automatic operator selection: enabled, allowing roaming

The display uses the same 122 × 250 geometry, `phone-5x7-v1` font, wrapping,
and centering rules as the web canvas.

## Wiring

The e-paper wiring matches the other experiments:

| E-paper | ESP32-S3 pin |
| --- | --- |
| DIN | GPIO11 |
| CLK | GPIO12 |
| CS | GPIO10 |
| DC | GPIO9 |
| RST | GPIO8 |
| BUSY | GPIO13 |

The onboard SIM7670G uses UART RX GPIO17 and TX GPIO18.

## Build and upload

```sh
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  --output-dir build/cellular_message_phone \
  experiments/cellular_message_phone

arduino-cli upload \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi' \
  --port /dev/cu.usbmodem5B910474881 \
  --input-dir build/cellular_message_phone \
  experiments/cellular_message_phone
```

Serial diagnostics are available at 115200 baud.

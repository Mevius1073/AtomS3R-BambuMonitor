# AtomS3R-BambuMonitor

LAN-based Bambu Lab printer status monitor for **M5Stack AtomS3R** (ESP32-S3, 0.85-inch 128x128 IPS) using local MQTT (TLS).

## Features

- Nozzle temperature (current / target)
- Bed temperature (current / target)
- Layer progress (e.g. `3 / 1300`)
- Progress bar with percentage
- Print state (IDLE / RUNNING / PAUSE / FINISH ...)

## Hardware

- M5Stack AtomS3R (SKU: C126)
- Bambu Lab printer with LAN access (A1 / A1 mini / P1P / P1S / X1 / X1C / X1E)

## Software

- Arduino IDE 2.x
- Board: ESP32 by Espressif (3.x recommended)
- Libraries: M5Unified, PubSubClient, ArduinoJson v7

## Setup

1. Copy `config.example.h` to `config.h`.
2. Fill in `WIFI_SSID`, `WIFI_PASSWORD`, `PRINTER_IP`, `PRINTER_SERIAL`, `ACCESS_CODE`.
   - Access Code: Printer Settings -> LAN Only (regenerated on every reboot)
   - Serial: Bambu Studio -> Device, or printed on the device label
3. In Arduino IDE, select board `M5Stack-AtomS3` (or `ESP32S3 Dev Module`).
4. Enable PSRAM: OPI PSRAM (8MB) and USB CDC On Boot.
5. Hold reset button for 2 seconds (until green LED) -> upload.

## Operation

Short press of the front button: send `pushall` request to refresh all fields.

## Notes

- AtomS3R Wi-Fi is 2.4 GHz only (IEEE 802.11 b/g/n).
- Bambu printers also use 2.4 GHz; both must be on the same 2.4 GHz network.
- The printer uses a self-signed TLS certificate; this code uses `setInsecure()` accordingly.
- For P1/A1 series, only changed fields are pushed. The sketch caches values and
  issues a `pushall` on (re)connect.
- MQTT buffer is sized to 40 KB because Bambu's full status payload can exceed 16 KB.

## Disclaimer

This project is **not affiliated with or endorsed by Bambu Lab**.
"Bambu Lab" is a trademark of its respective owner.

## License

MIT

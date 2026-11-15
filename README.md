# Screen Eyes firmware

ESP32-C3 Super Mini driving two GC9A01 round displays (240x240, SPI).

## Setup (Arduino IDE 2)

- Board package: **esp32 by Espressif Systems** (tested with 3.3.12)
- Board: **ESP32C3 Dev Module**, **USB CDC On Boot: Enabled** (stored in `screen_eyes/sketch.yaml`)
- Libraries: **LovyanGFX** (tested with 1.3.0), **NimBLE-Arduino** (tested with 2.5.2)

## Wiring

Both displays share the SPI bus; only CS is separate. Pin numbers are defined in `screen_eyes/config.h`.

| Display pin      | Left eye | Right eye | ESP32-C3 |
|------------------|:--------:|:---------:|----------|
| VCC              | ✔        | ✔         | 3V3      |
| GND              | ✔        | ✔         | GND      |
| SCL / SCK        | ✔        | ✔         | GPIO4    |
| SDA / MOSI       | ✔        | ✔         | GPIO6    |
| DC               | ✔        | ✔         | GPIO3    |
| RES / RST        | ✔        | ✔         | GPIO1    |
| CS               | ✔        |           | GPIO7    |
| CS               |          | ✔         | GPIO10   |
| BLK / BL         | optional | optional  | 3V3 or unconnected |

Pins to avoid: GPIO2, GPIO8, GPIO9 (strapping, GPIO8 = onboard LED), GPIO18/19 (USB).

## First run

Flash `screen_eyes`:

1. For 1.5 s the left display shows blue **LEFT**, the right one red **RIGHT**.
   If they are swapped, swap `PIN_CS_LEFT` / `PIN_CS_RIGHT` in `config.h`.
2. Then the animated eyes start: blinking, looking around.
   Serial Monitor (115200) prints FPS every 5 s.

Troubleshooting:

- Colors inverted (white background looks black) → toggle `cfg.invert` in `eye_display.h`.
- Red and blue swapped → toggle `cfg.rgb_order`.
- Garbage / noise on screen → lower `SPI_FREQ_HZ` in `config.h` (default 80 MHz) to 40 or 20 MHz, keep wires short.
- Nothing in Serial Monitor → check that USB CDC On Boot is Enabled.

## Control

Bluetooth LE (device name `ScreenEyes`) or Serial Monitor (115200 baud, line ending "Newline"), same text commands, e.g. `emo happy`, `fx hearts`, `look 0.8 -0.2`, `wink r`, `color 3080E0`, `lid C88A64`.

Full command list, BLE UUIDs and how to test with nRF Connect: [docs/protocol.md](docs/protocol.md).

## Code layout

| File | Purpose |
|---|---|
| `config.h` | pins, SPI speed |
| `eye_display.h` | GC9A01 driver setup (LovyanGFX), shared reset |
| `eye_types.h/.cpp` | emotions, effects, per-frame eye parameters |
| `eye_renderer.h/.cpp` | draws one eye into a sprite |
| `eye_animator.h/.cpp` | blinking, gaze, smooth emotion transitions |
| `commands.h/.cpp` | text command protocol |
| `ble_control.h/.cpp` | Bluetooth LE service (NimBLE), command queue |
| `screen_eyes.ino` | setup, main loop, Serial and BLE input |

The renderer, animator and commands have no Arduino dependencies, so they also run in the desktop preview.

## Desktop preview

`tools/preview/build.sh` renders the eyes on a PC using the same code: `tools/preview/out/gallery.png` (all emotions and effects) and `anim.gif` (scripted animation). Requires g++, Python 3 with Pillow, and LovyanGFX installed via Arduino IDE.

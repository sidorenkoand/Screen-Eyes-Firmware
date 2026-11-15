#pragma once

// ---------------------------------------------------------------------------
// Hardware configuration: ESP32-C3 Super Mini + 2x GC9A01 (240x240, SPI)
//
// Both displays share one SPI bus (SCK, MOSI, DC, RST).
// Each display has its own chip-select (CS) line.
//
// Avoid GPIO2, GPIO8, GPIO9 (strapping pins; GPIO8 also drives the onboard LED)
// and GPIO18/19 (native USB used for flashing and Serial).
// ---------------------------------------------------------------------------

// Shared SPI bus
constexpr int PIN_SCK  = 4;   // display pin: SCL / SCK / CLK
constexpr int PIN_MOSI = 6;   // display pin: SDA / MOSI / DIN
constexpr int PIN_DC   = 3;   // display pin: DC / RS
constexpr int PIN_RST  = 1;   // display pin: RES / RST (wired to both displays)

// Per-display chip select
constexpr int PIN_CS_LEFT  = 7;   // left eye  display pin: CS
constexpr int PIN_CS_RIGHT = 10;  // right eye display pin: CS

// Backlight (BLK / BL). -1 = not connected (most modules keep it on by default).
constexpr int PIN_BACKLIGHT = -1;

// SPI clock (ESP32-C3 maximum: 80 MHz). If you see noise or stripes on the
// displays (long or loose wires), use 40000000.
constexpr uint32_t SPI_FREQ_HZ = 80000000;

// Screen geometry
constexpr int SCREEN_SIZE = 240;

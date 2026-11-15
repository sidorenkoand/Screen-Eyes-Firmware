#pragma once

#include <LovyanGFX.hpp>
#include "config.h"

// One GC9A01 display. Two instances share the same SPI host;
// LovyanGFX initializes the bus only once, so this is safe.
//
// Reset is handled manually (resetDisplays) because the RST line is shared:
// letting each panel pulse it would reset the other one after it was set up.
class EyeDisplay : public lgfx::LGFX_Device {
public:
  explicit EyeDisplay(int pinCs) {
    {
      auto cfg = bus_.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = SPI_FREQ_HZ;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = true;   // no MISO
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_SCK;
      cfg.pin_mosi = PIN_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc   = PIN_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs   = pinCs;
      cfg.pin_rst  = -1;       // shared reset, see resetDisplays()
      cfg.pin_busy = -1;
      cfg.panel_width   = SCREEN_SIZE;
      cfg.panel_height  = SCREEN_SIZE;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable   = false;
      cfg.invert     = true;   // most GC9A01 modules need inverted colors
      cfg.rgb_order  = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;   // two panels on one bus
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }

private:
  lgfx::Bus_SPI      bus_;
  lgfx::Panel_GC9A01 panel_;
};

// Pulse the shared reset line once, before init() of either display.
inline void resetDisplays() {
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  delay(5);
  digitalWrite(PIN_RST, LOW);
  delay(20);
  digitalWrite(PIN_RST, HIGH);
  delay(150);
}

inline void initBacklight() {
  if (PIN_BACKLIGHT >= 0) {
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);
  }
}

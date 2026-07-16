#pragma once
#include <LovyanGFX.hpp>

#if defined(ARDUINO_ARCH_ESP32)
// ──────────────────────────────────────────────
// ESP32-C3 (original config)
// ──────────────────────────────────────────────
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX(void)
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.freq_write = 40000000;
            cfg.pin_miso = -1;
            cfg.pin_mosi = 7;
            cfg.pin_sclk = 6;
            cfg.pin_dc   = 2;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs  = 10;
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.memory_width  = 240;
            cfg.memory_height = 240;
            cfg.panel_width   = 240;
            cfg.panel_height  = 240;
            // Round panel clipping
            cfg.offset_rotation = 0;
            cfg.readable  = false;
            cfg.invert    = true;
            cfg.rgb_order = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 3;
            cfg.invert = false;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

#elif defined(ARDUINO_ARCH_ESP8266)
// ──────────────────────────────────────────────
// ESP8266 D1 Mini + GC9A01 round 240×240
//
// Pin mapping (change if your wiring differs):
//   MOSI  → D7  (GPIO13, hardware SPI)
//   SCLK  → D5  (GPIO14, hardware SPI)
//   CS    → D8  (GPIO15)
//   DC    → D2  (GPIO4)
//   RST   → D3  (GPIO0)  ⚠ GPIO0 is boot-strapping pin; use D4/D6 if flashing issues
//   BL    → D1  (GPIO5)
// ──────────────────────────────────────────────
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX(void)
    {
        {
            auto cfg = _bus.config();
            cfg.spi_mode   = SPI_MODE0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 1000000;
            cfg.pin_mosi   = 13;  // D7
            cfg.pin_miso   = -1;
            cfg.pin_sclk   = 14;  // D5
            cfg.pin_dc     = 4;   // D2
            cfg.spi_cs_mode = lgfx::SPI_CS_MODE::SPI_CS_MODE_0;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs     = 15;  // D8
            cfg.pin_rst    = 0;   // D3  (change to GPIO2/D4 if boot issues)
            cfg.pin_busy   = -1;

            // 240×240 round display
            cfg.memory_width  = 240;
            cfg.memory_height = 240;
            cfg.panel_width   = 240;
            cfg.panel_height  = 240;

            cfg.offset_rotation = 0;
            cfg.readable  = false;
            cfg.invert    = true;
            cfg.rgb_order = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 5;   // D1
            cfg.invert = false;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};
#else
#error "Unsupported architecture — use ESP32 or ESP8266"
#endif

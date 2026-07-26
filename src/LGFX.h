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
            auto& bus = _bus.config();
            bus.platform = 2;
            bus.spi_host = VSPI_HOST;
            bus.spi_mode = 0;
            bus.freq_write = 40000000;
            bus.freq_read = 6000000;
            bus.spi_3wire = true;
            bus.spi_half_duplex = true;
            bus.shared = true;

            bus.pin_sclk = 18;
            bus.pin_mosi = 19;
            bus.pin_miso = 8;
            bus.pin_cs = 7;
            bus.pin_dc = 6;
        }
        _bus.attachSPI(lgfx::getSPI(2));

        {
            auto& cfg = _panel.config();
            cfg.pin_reset = 10;
            cfg.pin_backlight = 21;
            cfg.backlight_on_active = false;
            cfg.memory_width = 240;
            cfg.memory_height = 240;
            cfg.panel_width = 240;
            cfg.panel_height = 240;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 2;
            cfg.read_color_565 = true;
            cfg.bus_shared = true;
        }

        _light.config();
        _light.pin_init();
        _light.pin_setter(0);
        _panel.attachLight(&_light);

        setPanel(&_panel);
    }
};

#elif defined(ARDUINO_ARCH_ESP8266)
// ──────────────────────────────────────────────
// ESP8266 NodeMCU V3 — GC9A01 240×240 round
// Bus_SPI config: freq_write, freq_read, spi_3wire, pin_sclk, pin_miso, pin_mosi, pin_dc, spi_mode
// Panel config: pin_cs, pin_rst, memory_*, panel_*, offset_*, read_color_565
// No Light_PWM on ESP8266 — backlight is tied to VCC on round panels.
// ──────────────────────────────────────────────
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI _bus;

public:
    LGFX(void)
    {
        // Bus configuration
        lgfx::Bus_SPI::config_t bus_cfg;
        bus_cfg.freq_write = 40000000; // 40MHz - stable for GC9A01 on ESP8266
        bus_cfg.freq_read  = 6000000;
        bus_cfg.spi_3wire  = true;
        bus_cfg.pin_sclk   = 14;   // SCL → D5
        bus_cfg.pin_mosi   = 13;   // SDA → D7
        bus_cfg.pin_miso   = 12;   // D6 (unused but valid)
        bus_cfg.pin_dc     = 4;    // DC → D2
        bus_cfg.spi_mode   = 0;
        _bus.config(bus_cfg);

        // Panel configuration
        lgfx::Panel_Device::config_t panel_cfg;
        panel_cfg.pin_cs = 15;     // CS → D8
        panel_cfg.pin_rst = 0;     // RST → D3
        panel_cfg.memory_width  = 240;
        panel_cfg.memory_height = 240;
        panel_cfg.panel_width   = 240;
        panel_cfg.panel_height  = 240;
        panel_cfg.offset_x = 0;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = 2;
        panel_cfg.invert = true;  // GC9A01 default is colour-inverted
        _panel.config(panel_cfg);

        _panel.setBus(&_bus);
        setPanel(&_panel);
    }
};
#endif

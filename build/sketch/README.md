#line 1 "/home/hermes/micro-radar/README.md"
# ESP8266 ADS-B Radar Display

An authentic PPI-style radar display for ADS-B data using an ESP8266 and GC9A01 round TFT display. This project connects to readsb/dump1090 servers to display aircraft positions with phosphor glow effects and smooth interpolation.

![Radar Display](images/radar-display.jpg)

## Features
- Authentic PPI radar display with enhanced phosphor glow effects
- Smooth aircraft interpolation between data updates
- Color-coded aircraft (green/gold for commercial, orange for military)
- Emergency squawk code alerts (7500, 7600, 7700, 1200) with flashing red/orange text, blip, and trail — cycles through multiple alerting aircraft
- Two phosphor color schemes: green (P1) and gold (P4) — toggleable via web UI or button
- **Two scan modes:** Angular Sweep (rotating beam) and Radial Ping (expanding sonar ring) — toggleable via web UI or button
- Aircraft trail dots showing track history behind moving targets
- Variable fade rates — strong signals persist longer than weak ones
- Live config reload — web UI changes apply instantly, no reboot required
- Physical button support for quick theme and scan mode toggling
- Configurable via web interface
- Compatible with readsb/dump1090 aircraft.json feeds

## Hardware Requirements
- ESP8266 NodeMCU (D1 Mini recommended)
- GC9A01 240x240 round TFT display
- [Round Mineral Glass Lens](https://www.aliexpress.com/item/1005002647300988.html) for authentic radar appearance
- Two momentary push buttons (optional)
- Micro USB cable for power

## Installation

### Easy Deployment (Pre-compiled Binary)
1. Download the latest release from [releases page](https://github.com/ACSmith1337/micro-radar/releases)
2. Connect your ESP8266 via USB
3. Flash `firmware.bin` using esptool:
   ```
   esptool.py --port /dev/ttyUSB0 write_flash 0x0 firmware.bin
   ```
4. Power cycle the device

### Building from Source
1. Install Arduino CLI:
   ```
   curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
   ```

2. Install required libraries:
   ```
   arduino-cli lib install "LovyanGFX" "ArduinoJson" "WiFiManager"
   ```

3. Install ESP8266 core:
   ```
   arduino-cli core update-index
   arduino-cli core install esp8266:esp8266
   ```

4. Build firmware:
   ```
   arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 .
   ```

5. Flash the firmware:
   ```
   arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:nodemcuv2 .
   ```

## Round Mineral Glass Lens
For an authentic radar appearance, we recommend using a Round Mineral Glass Lens over the display. This adds a professional look and feel similar to actual radar displays. You can purchase one from [AliExpress](https://www.aliexpress.com/item/1005002647300988.html).

## Configuration
After flashing, the device will create a WiFi access point named "MicroRadar-Setup". Connect to this network and navigate to `192.168.4.1` to configure:

- WiFi credentials for your network
- readsb/dump1090 server address and port
- Radar location coordinates
- Display settings (scan line, triangles, info text, trails, squawk alerts)
- Phosphor color scheme (green P1 or gold P4)
- Scan mode (Angular Sweep or Radial Ping)

All changes apply instantly — no reboot required. The configuration page is also accessible at the device's IP on your local network (e.g. `http://192.168.1.xxx`).

### Diagnostic Endpoint
The `/status` page reports runtime diagnostics:
```
http://<device-ip>/status
```
Returns free heap, max allocatable block, and other memory metrics. Useful for monitoring ESP8266 RAM health.

## Physical Buttons (Optional)
Wire two momentary push buttons to toggle settings without the web UI:

| Button | Pin | GPIO | Function |
|--------|-----|------|----------|
| Button 1 | D6 | GPIO12 | Toggle theme (Green ↔ Amber) |
| Button 2 | D4 | GPIO2 | Toggle scan mode (Angular ↔ Radial) |

Buttons connect between their GPIO pin and GND. Internal pull-ups are enabled.

## Wiring
Connect the GC9A01 display to the ESP8266:
```
Display Pin | ESP8266 Pin
-----------|------------
VCC        | 3.3V
GND        | GND
SCL        | D5 (GPIO14)
SDA        | D7 (GPIO13)
DC         | D2 (GPIO4)
CS         | D8 (GPIO15)
RST        | D3 (GPIO0)
BL         | D1 (GPIO5)
```

## Scan Modes

### Angular Sweep (Default)
A rotating beam sweeps clockwise around the display. Aircraft illuminate to full brightness when the beam passes over them, then gradually fade — mimicking a real PPI radar.

### Radial Ping
An expanding ring grows from the center outward, illuminating aircraft as it crosses their position. After reaching the edge, the screen clears and pauses before the next ping.

## Aircraft Fade Behavior
Aircraft blips fade after beam illumination using RSSI-based timing:
- **Strong signal** (RSSI > -30 dBm): ~8.8 seconds to full fade
- **Weak signal** (RSSI < -100 dBm): ~4.4 seconds to full fade
- Linear interpolation between these values based on received signal strength

This mirrors real PPI phosphor persistence — strong returns linger longer than weak ones.

## Troubleshooting
- If the display remains blank, check wiring connections
- If no aircraft appear, verify readsb/dump1090 server settings
- For connection issues, double-check WiFi credentials in the configuration portal
- Ensure the GPS coordinates in configuration match your radar location

## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments
- Based on the original micro-radar project by Anthony Sturdy
- Uses LovyanGFX library for display rendering
- Inspired by classic military PPI displays

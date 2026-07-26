# ESP8266 ADS-B Radar Display

An authentic PPI-style radar display for ADS-B data using an ESP8266 and GC9A01 round TFT display. This project connects to readsb/dump1090 servers to display aircraft positions with phosphor glow effects and smooth interpolation.

![Radar Display](images/radar-display.jpg)

## Features
- Authentic PPI radar display with phosphor glow effects
- Smooth aircraft interpolation between data updates
- Color-coded aircraft (blue for commercial, red for military)
- Configurable via web interface
- Compatible with readsb/dump1090 aircraft.json feeds

## Hardware Requirements
- ESP8266 NodeMCU (D1 Mini recommended)
- GC9A01 240x240 round TFT display
- [Round Mineral Glass Lens](https://www.aliexpress.com/item/1005002647300988.html) for authentic radar appearance
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
- Display settings (scan line, triangles, info text)

## Assembly Instructions
1. Connect the GC9A01 display to the ESP8266:
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

2. Place the Round Mineral Glass Lens over the display for authentic appearance

3. Mount in an appropriate enclosure with antenna placement consideration

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
#!/bin/bash
# ESP8266 ADS-B Radar Deployment Script

echo "ESP8266 ADS-B Radar Deployment"
echo "=============================="

# Check if firmware file exists
if [ ! -f "firmware.bin" ]; then
    echo "Error: firmware.bin not found!"
    echo "Please ensure you're running this script in the release directory"
    exit 1
fi

# Detect OS and provide appropriate flashing instructions
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Linux detected"
    echo "To flash the firmware, use one of the following commands:"
    echo ""
    echo "Using esptool.py (install with 'pip install esptool'):"
    echo "  esptool.py --port /dev/ttyUSB0 write_flash 0x0 firmware.bin"
    echo ""
    echo "Using platformio (install with 'pip install platformio'):"
    echo "  pio run --target upload --upload-port /dev/ttyUSB0"
    echo ""
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "macOS detected"
    echo "To flash the firmware, use one of the following commands:"
    echo ""
    echo "Using esptool.py (install with 'pip install esptool'):"
    echo "  esptool.py --port /dev/tty.SLAB_USBtoUART write_flash 0x0 firmware.bin"
    echo ""
    echo "Note: You may need to install a USB-to-Serial driver from Silicon Labs"
    echo ""
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
    echo "Windows detected"
    echo "To flash the firmware:"
    echo "1. Download and install ESP Flash Tool from Espressif"
    echo "2. Load firmware.bin at address 0x0"
    echo "3. Connect your ESP8266 via USB and click Start"
    echo ""
    echo "Alternatively, using esptool.py:"
    echo "  esptool.py --port COM3 write_flash 0x0 firmware.bin"
    echo ""
else
    echo "Unknown OS - please refer to your platform's ESP8266 flashing documentation"
    echo ""
fi

echo "After flashing:"
echo "1. Power cycle the device"
echo "2. Connect to the 'MicroRadar-Setup' WiFi network"
echo "3. Open http://192.168.4.1 in your browser"
echo "4. Configure your WiFi and readsb/dump1090 settings"
echo ""
echo "For hardware assembly instructions, see README.md"
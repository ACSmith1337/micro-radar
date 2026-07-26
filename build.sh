#!/bin/bash
#!/bin/bash
# ESP8266 Radar Firmware Build Script (no sudo/platformio required)

echo "[BUILD] Compiling radar firmware with Arduino CLI..."

# Use local arduino-cli
ARDUINO_CLI="/home/hermes/projects/adsb-radar/bin/arduino-cli"
if [ ! -f "$ARDUINO_CLI" ]; then
    echo "[BUILD] Error: arduino-cli not found at $ARDUINO_CLI"
    exit 1
fi

# Init project if needed
if [ ! -d "build" ]; then
    mkdir build
fi

# Copy local headers to sketch folder (fix include paths)
cp include/LGFX.h src/
cp include/WiFiManagerHelpers.h src/
cp include/JsonParser.h src/

# Compile for ESP8266 NodeMCU
"$ARDUINO_CLI" compile --fqbn esp8266:esp8266:nodemcuv2 --build-path ./build . || {
    echo "[BUILD] Compilation failed"
    exit 1
}

# Copy firmware
cp ./build/*.bin ./bin/firmware.bin
echo "[BUILD] Firmware ready: bin/firmware.bin ($(stat -c%s ./bin/firmware.bin) bytes)"
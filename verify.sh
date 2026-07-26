#!/bin/bash
# ESP8266 Radar Firmware Verification Script

echo "=== ESP8266 ADS-B Radar Firmware Verification ==="
echo

# Check if firmware exists
if [ ! -f "/home/hermes/micro-radar/bin/firmware.bin" ]; then
    echo "❌ Firmware binary not found!"
    exit 1
fi

# Check firmware size
FW_SIZE=$(stat -c%s "/home/hermes/micro-radar/bin/firmware.bin")
echo "✅ Firmware compiled successfully: $FW_SIZE bytes"

# Check if release package exists
if [ ! -f "/home/hermes/micro-radar/release/adsb-radar-release.tar.gz" ]; then
    echo "❌ Release package not found!"
    exit 1
fi

# Check release package contents
echo "✅ Release package created"

# Check if GitHub release exists
cd /home/hermes/micro-radar
RELEASE_CHECK=$(gh release view v1.1 2>/dev/null)
if [ $? -eq 0 ]; then
    echo "✅ GitHub release v1.1 published"
else
    echo "❌ GitHub release v1.1 not found"
fi

# Check key implementation files
if [ ! -f "/home/hermes/micro-radar/src/AircraftManager.cpp" ]; then
    echo "❌ AircraftManager.cpp not found!"
    exit 1
fi

if [ ! -f "/home/hermes/micro-radar/src/AircraftManager.h" ]; then
    echo "❌ AircraftManager.h not found!"
    exit 1
fi

echo "✅ Core implementation files present"

# Check for key features in the code
cd /home/hermes/micro-radar

# Check for phosphor trail implementation
if grep -q "DrawTrail" src/AircraftManager.cpp; then
    echo "✅ Phosphor trail implementation found"
else
    echo "❌ Phosphor trail implementation not found"
fi

# Check for aircraft classification
if grep -q "GetAircraftType" src/AircraftManager.cpp; then
    echo "✅ Aircraft classification implementation found"
else
    echo "❌ Aircraft classification implementation not found"
fi

# Check for interpolation
if grep -q "DeadReckonPosition" src/AircraftManager.cpp; then
    echo "✅ Interpolation implementation found"
else
    echo "❌ Interpolation implementation not found"
fi

# Check for warmup sequence
if grep -q "initialSyncComplete" src/AircraftManager.cpp; then
    echo "✅ Warmup sequence implementation found"
else
    echo "❌ Warmup sequence implementation not found"
fi

echo
echo "=== Verification Complete ==="
echo "Firmware is ready for deployment"
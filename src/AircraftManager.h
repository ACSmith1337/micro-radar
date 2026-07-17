#pragma once

#include <Arduino.h>
#include <map>
#include <vector>
#include <string>

// Include display library based on platform
#if defined(ARDUINO_ARCH_ESP32)
#include <LovyanGFX.hpp>

#elif defined(ARDUINO_ARCH_ESP8266)
#include "LGFX.h"  // ESP8266-compatible TFT_eSPI wrapper
#endif

#include "HttpRequestManager.h"
#include "ConfigurationWebServer.h"

// ─── Simple aircraft data (minimal memory footprint) ───
struct SimpleAircraft {
    String icao;
    float lat = 0.0f;
    float lon = 0.0f;
    float altitude = 0.0f;
    float heading = 0.0f;
    String squawk = "";
};

// ─── Interpolation storage ───
struct InterpPosition {
    float prevLat = 0.0f;
    float prevLon = 0.0f;
    float lat = 0.0f;
    float lon = 0.0f;
    bool hasPrev = false;
};

// ─── Drawing state per aircraft ───
struct DrawPosition {
    int x;
    int y;
    bool visible;
};

// ─── Aircraft classification ───
enum class AircraftType {
    COMMERCIAL,
    MILITARY,
    UNKNOWN
};

// ─── HTTP manager (forward) ───
struct HttpManager;
struct ConfigWebServer;

// ─── Aircraft manager: fetches, tracks, and renders aircraft on screen ───
class AircraftManager {
public:
    AircraftManager(ConfigurationWebServer& config, HttpRequestManager& httpClient, LGFX& display)
        : configServer(config), http(httpClient), tft(display) {}

    void Initialise();
    void Update();

#if defined(ARDUINO_ARCH_ESP8266)
    void Draw(LGFX& buf);
#endif

    // Legacy OpenSky support (stripped from UI)
    void OpenSky();

private:
    float lat = 0.0f;
    float lon = 0.0f;
    float rad = 0.0f;

    bool displayInfoText = false;
    bool displayTriangles = false;
    bool displayScanLine = true;

    uint32_t lastFetch = 0;
    uint32_t fetchInterval = 3000;

    std::map<String, SimpleAircraft> trackedAircraft;
    std::map<String, InterpPosition> prevPositions;
    std::map<String, DrawPosition> lastPositions;

    // Drawing
    void DrawRadarGrid() const;
    void DrawRadarFrame();
    void UpdateAircraftDisplay();
    void StorePreviousPositions();
    void ErasePosition(int x, int y) const;
    void DrawAircraftBlip(int x, int y, const SimpleAircraft& tracked) const;

    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;

    void FetchLocal();

    // External references
    ConfigurationWebServer& configServer;
    HttpRequestManager& http;

#if defined(ARDUINO_ARCH_ESP32)
    LGFX& tft;
#elif defined(ARDUINO_ARCH_ESP8266)
    LGFX& tft;
#endif
};

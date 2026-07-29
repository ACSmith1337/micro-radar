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
    float groundspeed = 0.0f;  // knots
    float seen = 0.0f;         // seconds since last message at fetch time
    float seenPos = 0.0f;      // seconds since last position update at fetch time
    String category = "";     // readsb category (e.g. A1..A7)
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

// ─── Drawing state per aircraft ──
struct DrawPosition {
    int x;
    int y;
    bool visible;
    uint8_t brightness = 24;  // PPI persistence: 24=max, decays to 0
};

// ─── Trail history: ring buffer of past positions ──
constexpr int TRAIL_HISTORY_MAX = 120;  // ~20 min at 10s fetch = 120 points
struct TrailPoint {
    int x;
    int y;
    uint32_t timestamp;  // millis() when recorded
};

struct TrailHistory {
    TrailPoint points[TRAIL_HISTORY_MAX];
    int count = 0;
    int head = 0;  // next write position
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

// ─── Scan modes ───
enum class ScanMode { ANGULAR, RADIAL };

// ─── Aircraft manager: fetches, tracks, and renders aircraft on screen ───
class AircraftManager {
public:
    AircraftManager(ConfigurationWebServer& config, HttpRequestManager& httpClient, LGFX& display)
        : configServer(config), http(httpClient), tft(display) {}

    void Initialise();
    void Update();
    void ReloadDisplayConfig();  // Live theme/mode reload without restart
    void ApplyThemeChange(bool amber);  // Direct theme toggle (no EEPROM read)
    void ApplyModeChange(bool radial);  // Direct mode toggle (no EEPROM read)
    bool IsAmber() const;
    bool IsRadial() const;

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
    bool displayTrailDots = false;  // Dotted trail behind aircraft vs heading vector
    bool alertSquawk = false;       // Flash emergency squawk alerts

    String ringLabelInner;
    String ringLabelMid;
    String ringLabelOuter;

    uint32_t lastFetch = 0;
    uint32_t fetchInterval = 3000;
    bool initialSyncComplete = false;
    uint32_t initialSyncLastAttempt = 0;
    uint32_t warmupStartMs = 0;
    bool warmupComplete = false;

    std::map<String, SimpleAircraft> trackedAircraft;
    std::map<String, InterpPosition> prevPositions;
    std::map<String, DrawPosition> lastPositions;
    std::map<String, TrailHistory> trailHistories;  // per-aircraft position history

    // Drawing
    void DrawRadarGrid() const;
    void DrawRadarLabels() const;
    void DrawRadarFrame();
    void DrawRadarPing(int cx, int cy, int r);
    void DrawTrail(int cx, int cy, int r, float headC, float headS);
    bool RefreshAircraft();
    void DecayAircraft();

    void UpdateAircraftDisplay();
    void StorePreviousPositions();
    void ErasePosition(int x, int y, uint8_t radius = 8) const;
    void DrawAircraftBlip(int x, int y, const SimpleAircraft& ac, uint8_t brightness = 24) const;
    uint16_t FadeColor(uint16_t base, uint8_t level) const;

    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;

    bool FetchLocal();

    // External references
    ConfigurationWebServer& configServer;
    HttpRequestManager& http;

#if defined(ARDUINO_ARCH_ESP32)
    LGFX& tft;
#elif defined(ARDUINO_ARCH_ESP8266)
    LGFX& tft;
#endif
};

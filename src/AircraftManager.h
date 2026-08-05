#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <string>

// Include display library based on platform
#include "LGFX.h"  // LGFX class definition (ESP32 and ESP8266)

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
    float rssi = 0.0f;         // signal strength (negative dBm, 0 = unknown)
    String category = "";     // readsb category (e.g. A1..A7)
    String squawk = "";
};

// ─── Drawing state per aircraft ──
struct DrawPosition {
    int x;
    int y;
    bool visible;
    uint8_t brightness = 24;  // PPI persistence: 24=max, decays to 0
    float rssi = 0.0f;        // last known RSSI (for ghost fade duration)
    float decayAccum = 0.0f;  // per-aircraft decay accumulator (avoids map lookup)
    uint32_t vanishedMs = 0;  // millis() when aircraft left the feed (0 = still tracked)
};

// ─── Airport marker ──
struct AirportMarker {
    float lat;
    float lon;
    int sx;        // screen x
    int sy;        // screen y
    bool onScreen;
    AirportMarker() : lat(0), lon(0), sx(0), sy(0), onScreen(false) {}
    AirportMarker(float l, float o, int x, int y, bool on) : lat(l), lon(o), sx(x), sy(y), onScreen(on) {}
};

// ─── Trail history: waypoint-compressed position history ──
// Only store points where direction changes meaningfully (>15°).
// Typical aircraft: 3-5 waypoints per session vs 120 raw points.
constexpr int TRAIL_WAYPOINTS_MAX = 16;
constexpr float TRAIL_TURN_THRESHOLD = 15.0f;  // degrees — minimum heading change to store a new waypoint
struct TrailWaypoint {
    int x;
    int y;
    uint32_t timestamp;  // millis() when recorded
};

struct TrailHistory {
    TrailWaypoint points[TRAIL_WAYPOINTS_MAX];
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

    // Force an immediate aircraft sync (called from web UI)
    static void RequestForceSync();
    static bool HasForceSyncRequested();
    static bool forceSyncRequested;

#if defined(ARDUINO_ARCH_ESP8266)
    void Draw(LGFX& buf);
#endif

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
    bool airportsFetched = false;
    uint32_t airportsFetchRetry = 0;    // millis of next retry
    bool fadeInComplete = false;        // Fade-in finished
    uint8_t fadeInRow = 0;              // Current reveal row during fade-in
    uint32_t lastFadeIn = 0;            // Last fade-in tick

    std::map<String, SimpleAircraft> trackedAircraft;
    std::map<String, DrawPosition> lastPositions;
    std::map<String, TrailHistory> trailHistories;  // per-aircraft position history

    // Airport markers
    std::vector<AirportMarker> airports;
    void FetchAirports(int timeout_ms = 30000);
    void DrawAirportMarkers() const;

    // Shared JSON document (avoids 3× 8KB BSS allocation)
    static StaticJsonDocument<8192> jsonDoc;

    // Drawing
    void DrawRadarGrid() const;
    void DrawRadarLabels() const;
    void DrawRadarFrame();
    void DrawRadarPing(int cx, int cy, int r);
    void DrawTrail(int cx, int cy, int r, float headC, float headS);
    bool RefreshAircraft();
    void DecayAircraft();
    void ErasePosition(int x, int y, uint8_t radius = 8) const;
    void DrawAircraftBlip(int x, int y, const SimpleAircraft& ac, uint8_t brightness = 24) const;
    void DrawAircraftBlip(int x, int y, const SimpleAircraft& ac, uint8_t brightness, uint16_t overrideColor) const;
    void UpdateAlertState(bool displayAlerts);
    void DrawAlertText(bool displayAlerts);
    void DrawAllAircraft(bool displayAlerts, const std::vector<String>& drawnThisFrame = {});
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;

    bool FetchLocal();
    bool FetchAdsblol();

    // Cached config values (avoids repeated Preferences reads per cycle)
    String cfgDataSource;
    String cfgReadsbHost;
    String cfgReadsbPort;
    String cfgReadsbPath;
    void CacheConfig();  // Read all config values from Preferences into cache

    // External references
    ConfigurationWebServer& configServer;
    HttpRequestManager& http;

#if defined(ARDUINO_ARCH_ESP32)
    LGFX& tft;
#elif defined(ARDUINO_ARCH_ESP8266)
    LGFX& tft;
#endif
};

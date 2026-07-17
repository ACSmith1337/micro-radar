#pragma once

#include <map>
#include <vector>

#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "LGFX.h"

// Lightweight aircraft record for local fetch (no blending needed at 3s refresh)
struct SimpleAircraft {
    double lat = 0;
    double lon = 0;
    double altitude = 0;
    double heading = 0;
    String icao;
    String squawk;
};

struct DrawPosition {
    int x = 0;
    int y = 0;
    bool visible = false;
};

class AircraftManager
{
private:
    double lat = 0.0;
    double lon = 0.0;
    double rad = 0.2;
    std::map<String, SimpleAircraft> trackedAircraft;

    bool displayInfoText = true;
    bool displayTriangles = true;
    bool displayScanLine = true;

    unsigned long fetchInterval = 3000;
    unsigned long lastFetch = 999999;

    ConfigurationWebServer& configServer;
    HttpRequestManager& http;
    LGFX& tft;

    std::map<String, DrawPosition> lastPositions;

    void DrawRadarGrid() const;
    void DrawRadarFrame();
    void DrawScanLineAt(float angle);
    void EraseScanLine(float angle);
    void ErasePosition(const String& icao, const DrawPosition& pos) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftBlip(int x, int y, const SimpleAircraft& tracked) const;
    void UpdateDisplay();
    void FetchLocal();

public:
    AircraftManager(ConfigurationWebServer& config, HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX& buf);
};

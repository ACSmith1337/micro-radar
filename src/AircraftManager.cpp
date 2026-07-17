#include "AircraftManager.h"

#include <ArduinoJson.h>

// ─── Phosphor CRT palette ───
constexpr uint16_t CLR_BG         = 0x0000;       // Pure black
constexpr uint16_t CLR_RING       = 0x04AF;       // Dim green (0x00,0x4A,0xF0 ≈ #009640)
constexpr uint16_t CLR_RING_BRIGHT= 0x052F;       // Bright ring green
constexpr uint16_t CLR_SCAN       = 0x07FF;       // Bright scan line green
constexpr uint16_t CLR_TRAIL      = 0x0154;       // Fading trail green
constexpr uint16_t CLR_BLIP       = 0xFFFF;       // White aircraft blip
constexpr uint16_t CLR_TEXT       = 0x052F;       // Dim green text
constexpr uint16_t CLR_CROSSHAIR  = 0x0286;       // Very dim green crosshair

void AircraftManager::Initialise()
{
    String storedLat = configServer.GetStoredString("latitude");
    String storedLon = configServer.GetStoredString("longitude");
    String storedRad = configServer.GetStoredString("radius");
    String storedInfoText = configServer.GetStoredString("infotext");
    String storedTriangles = configServer.GetStoredString("triangle");
    String storedScanLine = configServer.GetStoredString("scanline");
    String storedFetchInterval = configServer.GetStoredString("fetchinterval");

    lat = storedLat.toFloat();
    lon = storedLon.toFloat();
    rad = storedRad.toFloat();
    displayInfoText = (storedInfoText == "true");
    displayTriangles = (storedTriangles == "true");
    displayScanLine = (storedScanLine == "true");
    if (storedFetchInterval.length() > 0) {
        fetchInterval = storedFetchInterval.toFloat() * 1000.0f;
    }

    Serial.printf("[RADAR] Config: lat=%.6f lon=%.6f rad=%.6f scan=%d tri=%d info=%d interval=%lu\n",
                   lat, lon, rad, displayScanLine, displayTriangles, displayInfoText, fetchInterval);

    // Clear screen (removes WiFi text) + initial draw
    tft.fillScreen(CLR_BG);
    DrawRadarGrid();

    // Bearing labels — drawn once, outside the 112px scan erase circle
    tft.setTextColor(CLR_RING_BRIGHT);
    tft.setTextSize(1);
    tft.drawCentreString("N", 120, 4, 1);
    tft.drawCentreString("S", 120, 234, 1);
    tft.drawCentreString("E", 236, 116, 1);
    tft.drawCentreString("W", 4, 116, 1);
}

void AircraftManager::Update()
{
    // Radar animation at 30fps — black circle erase + grid + scan wedge
    static uint32_t lastScanDraw = 0;
    if (millis() - lastScanDraw >= 33) {
        DrawRadarFrame();
        lastScanDraw = millis();
    }

    // Fetch aircraft data on separate interval
    if (millis() - lastFetch >= fetchInterval) {
        FetchLocal();
        lastFetch = millis();
        UpdateDisplay();
    }
}

// Complete radar frame: clear scan area, draw grid, draw scan wedge
// Text labels drawn ONCE in Initialise() — outside the erase area
void AircraftManager::DrawRadarFrame()
{
    const int cx = 120, cy = 120;

    // Black circle erase — clears scan area + grid, preserves edge text
    tft.fillCircle(cx, cy, 112, CLR_BG);

    // Concentric rings
    tft.drawCircle(cx, cy, 110, CLR_RING);
    tft.drawCircle(cx, cy, 74,  CLR_RING);
    tft.drawCircle(cx, cy, 37,  CLR_RING);

    // Crosshairs
    tft.drawFastHLine(12, cy, 216, CLR_CROSSHAIR);
    tft.drawFastVLine(cx, 8, 224, CLR_CROSSHAIR);

    // Tick marks (every 30°) — precomputed directions
    float cos30 = 0.8660254f, sin30 = 0.5f;
    float cos60 = 0.5f, sin60 = 0.8660254f;
    float dirs[] = {
        0, -1, sin30, -cos30, cos60, -sin60,
        1, 0, sin30, cos30, cos60, sin60,
        0, 1, -sin30, cos30, -cos60, sin60,
        -1, 0, -sin30, -cos30, -cos60, -sin60
    };
    for (int i = 0; i < 12; i++) {
        float dx = dirs[i * 2], dy = dirs[i * 2 + 1];
        tft.drawLine(cx + (int)(dx * 106), cy + (int)(dy * 106),
                     cx + (int)(dx * 114), cy + (int)(dy * 114), CLR_RING);
    }

    // Bearing labels — redraw each frame
    tft.setTextColor(CLR_RING_BRIGHT);
    tft.setTextSize(1);
    tft.drawCentreString("N", 120, 10, 1);
    tft.drawCentreString("S", 120, 230, 1);
    tft.drawCentreString("E", 230, 116, 1);
    tft.drawCentreString("W", 10, 116, 1);

    // Scan wedge (clockwise from North)
    if (displayScanLine) {
        float angle = -(millis() / 400.0f);
        const int r = 110;
        float c = std::cos(angle), s = std::sin(angle);
        float da = 0.03f;

        for (int i = 0; i < 60; i++) {
            uint16_t color;
            if (i == 0) color = CLR_SCAN;
            else if (i < 5) color = 0x079F;
            else if (i < 20) color = 0x03AF;
            else color = CLR_TRAIL;

            int x1 = cx + (int)(c * r);
            int y1 = cy - (int)(s * r);

            // Incremental rotation (no trig in loop)
            float nc = c + s * da;
            float ns = s - c * da;

            int x2 = cx + (int)(nc * r);
            int y2 = cy - (int)(ns * r);

            tft.fillTriangle(cx, cy, x1, y1, x2, y2, color);

            c = nc;
            s = ns;
        }
    }
}

void AircraftManager::UpdateDisplay()
{
    // Grid + scan handled in Update() at 10fps — this only updates aircraft

    // Erase aircraft that are no longer tracked
    std::vector<String> toRemove;
    for (auto& [icao, lastPos] : lastPositions) {
        if (!trackedAircraft.count(icao)) {
            ErasePosition(icao, lastPos);
            toRemove.push_back(icao);
        }
    }
    for (auto& icao : toRemove) {
        lastPositions.erase(icao);
    }

    // Draw/update tracked aircraft
    for (auto& [icao, tracked] : trackedAircraft) {
        auto projected = ProjectCoordinateToScreen(tracked.lat, tracked.lon);
        int x = projected.first;
        int y = projected.second;
        bool visible = (x >= 1 && x < 239 && y >= 1 && y < 239);

        if (visible) {
            if (lastPositions.count(icao) > 0) {
                auto& lp = lastPositions[icao];
                if (lp.x != x || lp.y != y) {
                    ErasePosition(icao, lp);
                }
            }
            DrawAircraftBlip(x, y, tracked);
            lastPositions[icao] = {x, y, true};
        } else {
            if (lastPositions.count(icao) > 0 && lastPositions[icao].visible) {
                ErasePosition(icao, lastPositions[icao]);
            }
            lastPositions[icao] = {x, y, false};
        }
    }
}

void AircraftManager::Draw(LGFX& /*buf*/)
{
    // No-op — drawing is incremental in UpdateDisplay()
}

// Old-school CRT radar grid — drawn once in Initialise()
// Bearing labels are drawn in Initialise() outside the scan erase area
void AircraftManager::DrawRadarGrid() const
{
    const int cx = 120, cy = 120;

    // Dim crosshair lines
    tft.drawFastHLine(1, cy, 238, CLR_CROSSHAIR);
    tft.drawFastVLine(cx, 1, 238, CLR_CROSSHAIR);

    // Concentric range rings
    tft.drawCircle(cx, cy, 110, CLR_RING);
    tft.drawCircle(cx, cy, 74,  CLR_RING);
    tft.drawCircle(cx, cy, 37,  CLR_RING);
}

// ─── Scan wedge: incremental erase + draw at given angle ───
// Draws/fades a sector from center to radius with trailing phosphor effect
void AircraftManager::DrawScanLineAt(float angle)
{
    if (!displayScanLine) return;

    const int cx = 120, cy = 120;
    const int radius = 110;
    constexpr int TRAIL_LENGTH = 45;

    for (int i = 0; i <= TRAIL_LENGTH; i++) {
        float segAngle = angle - (i * 0.035f);

        uint16_t color;
        if (i == 0) {
            color = CLR_SCAN;
        } else if (i < 5) {
            color = 0x079F;
        } else if (i < 15) {
            color = 0x03AF;
        } else {
            color = CLR_TRAIL;
        }

        int x2 = cx + (int)(std::cos(segAngle) * radius);
        int y2 = cy - (int)(std::sin(segAngle) * radius);

        tft.drawLine(cx, cy, x2, y2, color);
    }
}

// Erase scan rays at previous angle by drawing background
void AircraftManager::EraseScanLine(float angle)
{
    if (!displayScanLine) return;

    const int cx = 120, cy = 120;
    const int radius = 110;
    constexpr int TRAIL_LENGTH = 45;

    for (int i = 0; i <= TRAIL_LENGTH; i++) {
        float segAngle = angle - (i * 0.035f);

        int x2 = cx + (int)(std::cos(segAngle) * radius);
        int y2 = cy - (int)(std::sin(segAngle) * radius);

        tft.drawLine(cx, cy, x2, y2, CLR_BG);
    }
}

void AircraftManager::ErasePosition(const String& /*icao*/, const DrawPosition& pos) const
{
    if (!pos.visible) return;
    // Erase blip by redrawing the grid area with background
    tft.fillCircle(pos.x, pos.y, 6, CLR_BG);
}

// ─── Aircraft blip with CRT phosphor glow ───
// Outer dim ring + bright center dot, like a real radar contact
void AircraftManager::DrawAircraftBlip(int x, int y, const SimpleAircraft& tracked) const
{
    if (displayTriangles) {
        // Directional indicator — small line showing heading
        float headingRad = tracked.heading * 3.14159f / 180.0f;
        int tipX = x + (int)(std::cos(headingRad) * 5.0f);
        int tipY = y - (int)(std::sin(headingRad) * 5.0f);
        tft.drawLine(x, y, tipX, tipY, CLR_BLIP);
    }

    // Phosphor glow: outer dim ring
    tft.drawCircle(x, y, 4, CLR_RING);
    // Bright center
    tft.fillCircle(x, y, 2, CLR_BLIP);
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    double dx = predLat - lat;
    double dy = (predLon - lon) * std::cos(lat * 3.14159 / 180.0);
    double bearing = std::atan2(dy, dx) * 180.0 / 3.14159;
    double dist = std::sqrt(dx * dx + dy * dy);
    float screenDist = (float)(dist / rad) * 110.0f;
    float screenX = 120.0f + (float)(screenDist * std::cos(bearing * 3.14159 / 180.0));
    float screenY = 120.0f - (float)(screenDist * std::sin(bearing * 3.14159 / 180.0));
    return std::make_pair((int)screenX, (int)screenY);
}

void AircraftManager::FetchLocal()
{
    String host = configServer.GetStoredString("readsbhost");
    if (host.isEmpty()) return;

    String port = configServer.GetStoredString("readsbport");
    if (port.isEmpty()) port = "8080";

    String path = configServer.GetStoredString("readsbpath");
    if (path.isEmpty()) path = "/data/aircraft.json";

    String url = "http://" + host + ":" + port + path;

    HttpResult result = http.Get(url);
    if (!result.success || result.response.length() == 0) {
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, result.response);
    if (err) {
        doc.clear();
        return;
    }

    auto arr = doc["aircraft"];
    if (!arr.is<JsonArray>()) {
        doc.clear();
        return;
    }

    std::map<String, SimpleAircraft> next;

    for (int i = 0; i < arr.size(); i++) {
        auto item = arr[i];
        const char* hexVal = item["hex"];
        if (!hexVal) continue;
        String icao(hexVal);
        if (icao.isEmpty()) continue;

        double latVal = item["lat"] | 0.0;
        double lonVal = item["lon"] | 0.0;
        if (latVal == 0 && lonVal == 0) continue;

        SimpleAircraft tracked;
        tracked.lat = latVal;
        tracked.lon = lonVal;
        tracked.altitude = item["alt_baro"] | 0.0;
        const char* squawkVal = item["squawk"];
        tracked.squawk = squawkVal ? squawkVal : "";
        tracked.icao = icao;
        double hdg = item["track"] | 0.0;
        if (isnan(hdg)) hdg = 0.0;
        tracked.heading = hdg;

        next[icao] = tracked;
    }

    doc.clear();
    trackedAircraft = next;
}

#include "AircraftManager.h"

#include <ArduinoJson.h>

// ─── Cold war radar phosphor palette ───
// Amber CRT look with subtle green accents
constexpr uint16_t CLR_BG          = 0x0000;       // Pure black phosphor off
constexpr uint16_t CLR_RING        = 0x3291;       // Dim amber phosphor ring
constexpr uint16_t CLR_RING_BRIGHT = 0x7FA0;       // Bright amber for labels
constexpr uint16_t CLR_SCAN        = 0x3FFF;       // Bright amber scan line
constexpr uint16_t CLR_TRAIL       = 0x0A28;       // Amber phosphor trail
constexpr uint16_t CLR_CROSSHAIR   = 0x2108;       // Very dim crosshair grid
constexpr uint16_t CLR_COMMERIAL   = 0x001F;       // Deep blue - commercial
constexpr uint16_t CLR_COMMERIAL_G = 0x0037;       // Blue phosphor glow
constexpr uint16_t CLR_MILITARY    = 0xF800;       // Red - military aircraft
constexpr uint16_t CLR_MILITARY_G  = 0x7808;       // Red phosphor glow
constexpr uint16_t CLR_UNKNOWN     = 0x3FFF;       // Amber - unknown type
constexpr uint16_t CLR_UNKNOWN_G   = 0x3291;       // Amber glow

// ─── Smooth animation timing ───
constexpr uint32_t SCAN_INTERVAL   = 33;           // 30fps scan animation
constexpr uint32_t AIRCRAFT_INTERVAL = 50;         // 20fps aircraft update
constexpr uint32_t FETCH_INTERVAL_DEFAULT = 3000;  // 3 second data refresh

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
    } else {
        fetchInterval = FETCH_INTERVAL_DEFAULT;
    }

    Serial.printf("[RADAR] Config: lat=%.6f lon=%.6f rad=%.6f scan=%d tri=%d info=%d interval=%lu\n",
                   lat, lon, rad, displayScanLine, displayTriangles, displayInfoText, fetchInterval);

    // Clear screen (removes WiFi text) + initial radar grid
    tft.fillScreen(CLR_BG);
    DrawRadarGrid();

    // Bearing labels - outside the scan erase circle
    tft.setTextColor(CLR_RING_BRIGHT);
    tft.setTextSize(1);
    tft.drawCentreString("N", 120, 4, 1);
    tft.drawCentreString("S", 120, 234, 1);
    tft.drawCentreString("E", 236, 116, 1);
    tft.drawCentreString("W", 4, 116, 1);
}

void AircraftManager::Update()
{
    // Scan animation at 30fps
    static uint32_t lastScanDraw = 0;
    if (millis() - lastScanDraw >= SCAN_INTERVAL) {
        DrawRadarFrame();
        lastScanDraw = millis();
    }

    // Smooth aircraft movement at 20fps - interpolate between fetches
    static uint32_t lastAircraftUpdate = 0;
    if (millis() - lastAircraftUpdate >= AIRCRAFT_INTERVAL) {
        UpdateAircraftDisplay();
        lastAircraftUpdate = millis();
    }

    // Fetch aircraft data on separate interval
    if (millis() - lastFetch >= fetchInterval) {
        // Store previous positions for interpolation
        StorePreviousPositions();
        FetchLocal();
        lastFetch = millis();
    }
}

// Complete radar frame: clear scan area, draw grid, draw scan wedge
void AircraftManager::DrawRadarFrame()
{
    const int cx = 120, cy = 120;

    // Black circle erase - clears scan area, preserves edge labels
    tft.fillCircle(cx, cy, 112, CLR_BG);

    // Concentric range rings - amber phosphor
    tft.drawCircle(cx, cy, 110, CLR_RING);
    tft.drawCircle(cx, cy, 74,  CLR_RING);
    tft.drawCircle(cx, cy, 37,  CLR_RING);

    // Crosshairs - very dim
    tft.drawFastHLine(12, cy, 216, CLR_CROSSHAIR);
    tft.drawFastVLine(cx, 8, 224, CLR_CROSSHAIR);

    // Tick marks (every 30°) - precomputed directions
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

    // Scan wedge (clockwise from North)
    if (displayScanLine) {
        float angle = -(millis() / 400.0f);
        const int r = 110;
        float c = std::cos(angle), s = std::sin(angle);
        float da = 0.03f;

        for (int i = 0; i < 60; i++) {
            uint16_t color;
            if (i == 0) color = CLR_SCAN;
            else if (i < 5) color = 0x3BEF;
            else if (i < 20) color = 0x1A79;
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

// Store current positions for interpolation before new fetch
void AircraftManager::StorePreviousPositions()
{
    for (auto& [icao, tracked] : trackedAircraft) {
        if (prevPositions.count(icao)) {
            auto& prev = prevPositions[icao];
            prev.prevLat = prev.lat;
            prev.prevLon = prev.lon;
            prev.hasPrev = true;
        }
    }
}

// Smooth aircraft display - interpolate between previous and current positions
void AircraftManager::UpdateAircraftDisplay()
{
    // Calculate interpolation factor (0 = start of interval, 1 = end)
    float t = std::min(1.0f, (float)(millis() - lastFetch) / fetchInterval);

    // Erase aircraft that are no longer tracked
    std::vector<String> toRemove;
    for (auto& [icao, lastPos] : lastPositions) {
        if (!trackedAircraft.count(icao) && !prevPositions.count(icao)) {
            ErasePosition(lastPos.x, lastPos.y);
            toRemove.push_back(icao);
        }
    }
    for (auto& icao : toRemove) {
        lastPositions.erase(icao);
    }

    // Draw/update tracked aircraft with smooth interpolation
    for (auto& [icao, tracked] : trackedAircraft) {
        // Interpolate position
        float drawLat = tracked.lat;
        float drawLon = tracked.lon;

        if (prevPositions.count(icao)) {
            auto& prev = prevPositions[icao];
            if (prev.hasPrev) {
                // Linear interpolation
                drawLat = prev.prevLat + (tracked.lat - prev.prevLat) * t;
                drawLon = prev.prevLon + (tracked.lon - prev.prevLon) * t;
            }
        }

        auto projected = ProjectCoordinateToScreen(drawLat, drawLon);
        int x = projected.first;
        int y = projected.second;
        bool visible = (x >= 1 && x < 239 && y >= 1 && y < 239);

        if (visible) {
            if (lastPositions.count(icao) > 0) {
                auto& lp = lastPositions[icao];
                if (lp.x != x || lp.y != y) {
                    ErasePosition(lp.x, lp.y);
                }
            }
            DrawAircraftBlip(x, y, tracked);
            lastPositions[icao] = {x, y, true};
        } else {
            if (lastPositions.count(icao) > 0 && lastPositions[icao].visible) {
                ErasePosition(lastPositions[icao].x, lastPositions[icao].y);
            }
            lastPositions[icao] = {x, y, false};
        }
    }
}

void AircraftManager::Draw(LGFX& /*buf*/)
{
    // No-op - drawing is incremental in UpdateAircraftDisplay()
}

// Old-school CRT radar grid - drawn once in Initialise()
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

void AircraftManager::ErasePosition(int x, int y) const
{
    // Erase blip area with background
    tft.fillCircle(x, y, 6, CLR_BG);
}

// Determine aircraft type by squawk code
// Military: 1200, 4000-4999, 6000-6999, 7000-7777
// Commercial: normal squawks 0000-3999 (excluding military)
static AircraftType GetAircraftType(const SimpleAircraft& ac)
{
    if (ac.squawk.isEmpty()) return AircraftType::UNKNOWN;

    int squawk = ac.squawk.toInt();

    // Military squawk patterns
    if (squawk == 1200 ||  // General aviation/military
        (squawk >= 4000 && squawk <= 4999) ||  // Military
        (squawk >= 6000 && squawk <= 6999) ||  // Military
        squawk >= 7000) {  // Emergency (also military)
        return AircraftType::MILITARY;
    }

    // Emergency squawks - show as military red
    if (squawk == 7500 || squawk == 7600 || squawk == 7700) {
        return AircraftType::MILITARY;
    }

    return AircraftType::COMMERCIAL;
}

// Aircraft blip with phosphor glow effect
void AircraftManager::DrawAircraftBlip(int x, int y, const SimpleAircraft& tracked) const
{
    AircraftType type = GetAircraftType(tracked);

    uint16_t color, glowColor;
    switch (type) {
        case AircraftType::COMMERCIAL:
            color = CLR_COMMERIAL;
            glowColor = CLR_COMMERIAL_G;
            break;
        case AircraftType::MILITARY:
            color = CLR_MILITARY;
            glowColor = CLR_MILITARY_G;
            break;
        default:
            color = CLR_UNKNOWN;
            glowColor = CLR_UNKNOWN_G;
            break;
    }

    if (displayTriangles) {
        // Directional indicator - heading arrow
        float headingRad = tracked.heading * 3.14159f / 180.0f;
        int tipX = x + (int)(std::cos(headingRad) * 6.0f);
        int tipY = y - (int)(std::sin(headingRad) * 6.0f);
        tft.drawLine(x, y, tipX, tipY, color);

        // Small filled circle at base
        tft.fillCircle(x, y, 1, glowColor);
    } else {
        // Phosphor glow effect - outer ring + bright center
        tft.drawCircle(x, y, 4, glowColor);
        tft.fillCircle(x, y, 2, color);
    }
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    // Guard against zero/negative radius (prevents INF/NaN)
    if (rad <= 0.001f) {
        return std::make_pair(999, 999); // Offscreen
    }

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

    // Cap response size to prevent heap exhaustion on ESP8266
    if (result.response.length() > 8192) {
        Serial.println("[FETCH] Response too large, discarding");
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
    constexpr int MAX_AIRCRAFT = 30; // Prevent memory exhaustion in dense airspace

    for (int i = 0; i < arr.size() && next.size() < MAX_AIRCRAFT; i++) {
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

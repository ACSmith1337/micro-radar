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

    // Full screen clear — grid drawn each frame in UpdateDisplay()
    tft.fillScreen(CLR_BG);
}

void AircraftManager::Update()
{
    if (millis() - lastFetch >= fetchInterval) {
        FetchLocal();
        lastFetch = millis();
        UpdateDisplay();
    }
}

void AircraftManager::UpdateDisplay()
{
    // ── Draw the grid every frame (fast, no clearing) ──
    DrawRadarGrid();

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

    DrawScanLine();
}

void AircraftManager::Draw(LGFX& /*buf*/)
{
    // No-op — drawing is incremental in UpdateDisplay()
}

// ─── Old-school CRT radar grid ───
// Hollow range rings, tick marks, bearing labels, dim crosshairs
void AircraftManager::DrawRadarGrid() const
{
    const int cx = 120, cy = 120;

    // Dim crosshair lines (barely visible, like CRT grid lines)
    tft.drawFastHLine(1, cy, 238, CLR_CROSSHAIR);
    tft.drawFastVLine(cx, 1, 238, CLR_CROSSHAIR);

    // Concentric range rings — outermost is brightest (like real PPI)
    tft.drawCircle(cx, cy, 110, CLR_RING);        // Outer ring
    tft.drawCircle(cx, cy, 74,  CLR_RING);        // Middle ring
    tft.drawCircle(cx, cy, 37,  CLR_RING);        // Inner ring

    // Tick marks on outer ring (every 30°)
    for (int angle = 0; angle < 360; angle += 30) {
        float rad = angle * 3.14159f / 180.0f;
        int x1 = cx + (int)(std::cos(rad) * 106);
        int y1 = cy - (int)(std::sin(rad) * 106);
        int x2 = cx + (int)(std::cos(rad) * 114);
        int y2 = cy - (int)(std::sin(rad) * 114);
        tft.drawLine(x1, y1, x2, y2, CLR_RING);
    }

    // Bearing labels: N, E, S, W
    tft.setTextColor(CLR_RING_BRIGHT);
    tft.setTextSize(1);
    tft.drawCentreString("N", cx, cy - 124, 1);
    tft.drawCentreString("S", cx, cy + 116, 1);
    tft.drawCentreString("E", cx + 124, cy + 4, 1);
    tft.drawCentreString("W", cx - 124, cy + 4, 1);
}

// ─── Scan wedge with fading trail ───
// Draws a sector behind the sweep line with decreasing brightness,
// mimicking the phosphor trail effect on real CRT radar
void AircraftManager::DrawScanLine()
{
    if (!displayScanLine) return;

    const int cx = 120, cy = 120;
    const int radius = 110;
    float angle = millis() / 400.0f;

    // Number of trail segments
    constexpr int TRAIL_LENGTH = 30;

    for (int i = TRAIL_LENGTH; i >= 0; i--) {
        // Each segment is 1 degree behind the lead
        float segAngle = angle - (i * 0.01745f); // 1° in radians

        // Brightness fades from bright at leading edge to near-dark at tail
        uint16_t color;
        if (i == 0) {
            color = CLR_SCAN; // Brightest at leading edge
        } else if (i < 8) {
            color = 0x07BF; // Bright-green fading
        } else if (i < 20) {
            color = 0x03AF; // Mid-green trail
        } else {
            color = CLR_TRAIL; // Dim trail
        }

        // Draw two lines at this angle to form a thin wedge
        float innerRad = 0.3f; // 0.3° inner arc
        float outerRad = radius;

        // Leading edge of this segment
        int x1 = cx + (int)(std::cos(segAngle) * innerRad);
        int y1 = cy - (int)(std::sin(segAngle) * innerRad);
        int x2 = cx + (int)(std::cos(segAngle) * outerRad);
        int y2 = cy - (int)(std::sin(segAngle) * outerRad);

        // Truncate at screen bounds
        tft.drawLine(x1, y1, x2, y2, color);
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

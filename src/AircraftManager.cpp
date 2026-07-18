#include "AircraftManager.h"

#include <ArduinoJson.h>
#include <cmath>

// ─── P1 green phosphor CRT (RGB565 — muted, darker green) ──
// RGB565 = RRRR RGGG GGGB BBBB → pure green = xxx0 xxx1 111x xxxx
// Muted green: lower G values, slight red for CRT warmth
constexpr uint16_t CLR_BG          = 0x0000;       // Black
constexpr uint16_t CLR_RING        = 0x0140;       // Dim muted green (R=0 G=10 B=0)
constexpr uint16_t CLR_RING_BRIGHT = 0x02E0;       // Brighter green labels
constexpr uint16_t CLR_SCAN        = 0x0520;       // Dark green scan line (R=0 G=42 B=0)
constexpr uint16_t CLR_GLOW        = 0x0320;       // Scan glow
constexpr uint16_t CLR_TRAIL       = 0x0120;       // Phosphor fade
constexpr uint16_t CLR_CROSSHAIR   = 0x00A0;       // Barely visible
constexpr uint16_t CLR_COMMERIAL   = 0x001F;       // Deep blue
constexpr uint16_t CLR_MILITARY    = 0xF800;       // Red
constexpr uint16_t CLR_UNKNOWN     = 0x0520;       // Dark green

// ─── Timing ───
constexpr uint32_t SCAN_INTERVAL   = 33;           // ~30fps
constexpr uint32_t ROTATION_MS     = 6000;         // 1 full sweep = 6s
constexpr uint32_t FETCH_DEFAULT   = ROTATION_MS;  // fetch at each rotation
constexpr int      MAX_AIRCRAFT    = 30;           // heap protection
constexpr int      MAX_RESP_BYTES  = 8192;         // heap protection
constexpr float    SCAN_SPEED      = (6.28318f / ROTATION_MS);  // 1 rev / 6s

// ── Trail: 30° visual, 32° total with 2° black safety margin ──
// 16 segments × 2° each = 32° total wedge.
// Outer 8 segments are black, inner 8 are green gradient.
constexpr int   TRAIL_SEGMENTS    = 16;
constexpr float TRAIL_STEP_DEG    = 2.0f;           // 2° per segment
// Precomputed cos(32°) and sin(32°) for tail calculation
constexpr float TRAIL_TAIL_COS    = 0.8480481f;     // cos(32°)
constexpr float TRAIL_TAIL_SIN    = 0.5299193f;     // sin(32°)

// Phosphor green gradient: 16 steps from black to dark green (0x0520)
// Outer 8 steps are pure black (0x0000) — they erase old trail residue.
// Inner 8 steps fade from dim green to bright scan tip.
constexpr uint16_t TRAIL_GRADIENT[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,  // Black erase (8 steps = 16°)
    0x0060, 0x00A0, 0x0120, 0x01C0,                                    // Phosphor fade-in (4 steps)
    0x0280, 0x0360, 0x0460, 0x0520                                     // Bright scan glow (4 steps)
};  // 16 entries = TRAIL_SEGMENTS

// ── Precomputed tick directions (30° increments) ──
constexpr const float TICK_DIRS[] = {
     0, -1,  0.5f, -0.8660f,  0.8660f, -0.5f,
     1,  0,  0.5f,  0.8660f,  0.8660f,  0.5f,
     0,  1, -0.5f,  0.8660f, -0.8660f,  0.5f,
    -1,  0, -0.5f, -0.8660f, -0.8660f, -0.5f
};

// ─── Incremental scan state ───
struct ScanState {
    float angle = 0.0f;     // Current sweep angle (radians, clockwise from North)
    float c = 1.0f;         // cos(angle) — maintained incrementally
    float s = 0.0f;         // sin(angle) — maintained incrementally
};

static ScanState scanState;

// ─── Incremental trig: rotate (c,s) to increase angle by delta radians ──
// Increasing angle = clockwise on screen (N→E→S→W)
// cos(θ+δ) = c - s·δ,  sin(θ+δ) = s + c·δ
static inline void RotateAngle(float &c, float &s, float delta)
{
    float nc = c - s * delta;
    float ns = s + c * delta;
    c = nc;
    s = ns;
}

// ─── Renormalise to prevent incremental drift ───
static inline void Renormalise(float &c, float &s)
{
    float mag = sqrt(c * c + s * s);
    c /= mag;
    s /= mag;
}

// ─── Store current positions for interpolation before new fetch ──
static void StorePrev(const std::map<String, SimpleAircraft>& tracked,
                      std::map<String, InterpPosition>& prev)
{
    for (auto& [icao, ac] : tracked) {
        auto& p = prev[icao];
        p.prevLat = ac.lat;
        p.prevLon = ac.lon;
        p.hasPrev = true;
    }
}

// ════════════════════════════════════════════════════════════

// ─── Aircraft type detection ───
// Military squawks: 4000–4999, 7000+, emergencies
static AircraftType GetAircraftType(const SimpleAircraft& ac)
{
    if (ac.squawk.isEmpty()) {
        return AircraftType::COMMERCIAL;
    }
    int sq = ac.squawk.toInt();
    if ((sq >= 4000 && sq <= 4999) ||  // Military (Europe)
        sq >= 7000) {                   // Emergency / military
        return AircraftType::MILITARY;
    }
    return AircraftType::COMMERCIAL;
}

// ════════════════════════════════════════════════════════════

void AircraftManager::Initialise()
{
    // ── Load config ──
    lat             = configServer.GetStoredString("latitude").toFloat();
    lon             = configServer.GetStoredString("longitude").toFloat();
    rad             = configServer.GetStoredString("radius").toFloat();
    displayInfoText = configServer.GetStoredString("infotext") == "true";
    displayTriangles = configServer.GetStoredString("triangle") == "true";
    displayScanLine = configServer.GetStoredString("scanline") != "false";

    String si = configServer.GetStoredString("fetchinterval");
    fetchInterval = si.length() > 0 ? si.toFloat() * 1000.0f : FETCH_DEFAULT;

    Serial.printf("[RADAR] lat=%.6f lon=%.6f rad=%.2f nm\n", lat, lon, rad);
    if (rad <= 0.001f) {
        Serial.println("[RADAR] WARNING: radius not set — no aircraft will appear");
    }

    // ── Reset scan state ──
    scanState = {0.0f, 1.0f, 0.0f};

    // ── Clear screen + draw grid ONCE (static elements) ──
    tft.fillScreen(CLR_BG);
    DrawRadarGrid();
}

void AircraftManager::Update()
{
    static uint32_t lastRotation = 0;

    // ── Rotation boundary: fetch data + redraw all blips at full brightness ──
    if (millis() - lastRotation >= ROTATION_MS) {
        lastRotation = millis();
        RefreshAircraft();
    }

    // ── Scan animation at ~30fps ──
    static uint32_t lastScan = 0;
    if (millis() - lastScan >= SCAN_INTERVAL) {
        DrawRadarFrame();
        lastScan = millis();
    }

    // ── PPI phosphor decay: dim blips once per second ──
    // 5 brightness levels → blips fade over ~5 seconds, gone by next rotation
    static uint32_t lastDecay = 0;
    if (millis() - lastDecay >= 1000) {
        DecayAircraft();
        lastDecay = millis();
    }
}

// ── Called once per rotation: fetch data, project, redraw all blips at full brightness ──
void AircraftManager::RefreshAircraft()
{
    // Fetch fresh data
    StorePrev(trackedAircraft, prevPositions);
    FetchLocal();
    lastFetch = millis();

    // Erase blips that are no longer tracked
    std::vector<String> gone;
    for (auto& [icao, lp] : lastPositions) {
        if (!trackedAircraft.count(icao)) {
            if (lp.visible) ErasePosition(lp.x, lp.y, 8);
            gone.push_back(icao);
        }
    }
    for (auto& icao : gone) lastPositions.erase(icao);

    // Redraw all tracked aircraft at full brightness
    for (auto& [icao, ac] : trackedAircraft) {
        auto proj = ProjectCoordinateToScreen(ac.lat, ac.lon);
        int x = proj.first, y = proj.second;
        bool on = (x > 0 && x < 239 && y > 0 && y < 239);

        if (on) {
            // Erase old position if it moved
            if (lastPositions.count(icao) && lastPositions[icao].visible) {
                ErasePosition(lastPositions[icao].x, lastPositions[icao].y, 8);
            }
            DrawAircraftBlip(x, y, ac, 5);  // full brightness
            lastPositions[icao] = {x, y, true, 5};
        } else {
            if (lastPositions.count(icao) && lastPositions[icao].visible) {
                ErasePosition(lastPositions[icao].x, lastPositions[icao].y, 8);
            }
            lastPositions[icao] = {x, y, false, 0};
        }
    }
}

// ── Called each scan frame: decay blip brightness, erase if faded out ──
void AircraftManager::DecayAircraft()
{
    std::vector<String> faded;
    for (auto& [icao, lp] : lastPositions) {
        if (!lp.visible || lp.brightness <= 1) continue;

        lp.brightness--;

        if (lp.brightness == 0) {
            // Fully faded — erase with black
            ErasePosition(lp.x, lp.y, 4);
            faded.push_back(icao);
        } else {
            // Redraw at new lower brightness
            if (trackedAircraft.count(icao)) {
                auto& ac = trackedAircraft.at(icao);
                DrawAircraftBlip(lp.x, lp.y, ac, lp.brightness);
            }
        }
    }
    for (auto& icao : faded) {
        auto& lp = lastPositions[icao];
        lp.brightness = 0;
        lp.visible = false;
    }
}

// ── Incremental scan: thin wedge per frame, smooth phosphor trail ──
// Clockwise on screen = decreasing angle (Y is inverted)
void AircraftManager::DrawRadarFrame()
{
    if (!displayScanLine) return;

    const int cx = 120, cy = 120;
    const int r = 102;             // Trail tip radius — 8px inside outer ring (110)
    const int erase_r = r + 2;     // 104 — still 6px inside ring

    // ── Advance scan angle CW on screen by 1° per frame ──
    constexpr float DEG1 = 0.0174533f;
    RotateAngle(scanState.c, scanState.s, -DEG1);

    // Renormalise every ~180 frames to prevent incremental drift
    static int normCount = 0;
    if (++normCount >= 180) {
        Renormalise(scanState.c, scanState.s);
        normCount = 0;
    }

    float headC = scanState.c;
    float headS = scanState.s;

    // ── Bright scan line (1° wedge at leading edge) ──
    float prevC = headC + headS * DEG1;
    float prevS = headS - headC * DEG1;
    tft.fillTriangle(cx, cy,
        cx + (int)(headC * r), cy - (int)(headS * r),
        cx + (int)(prevC * r), cy - (int)(prevS * r),
        CLR_SCAN);

    // ── Erase tail: 33° behind head (30° trail + 3° margin) ──
    constexpr float ERASE_COS = 0.83867f;  // cos(33°)
    constexpr float ERASE_SIN = 0.54464f;  // sin(33°)
    float eraseC = headC * ERASE_COS + headS * ERASE_SIN;
    float eraseS = headS * ERASE_COS - headC * ERASE_SIN;
    float eraseNextC = eraseC + eraseS * DEG1;
    float eraseNextS = eraseS - eraseC * DEG1;
    tft.fillTriangle(cx, cy,
        cx + (int)(eraseC * erase_r), cy - (int)(eraseS * erase_r),
        cx + (int)(eraseNextC * erase_r), cy - (int)(eraseNextS * erase_r),
        CLR_BG);

    // ── Redraw phosphor trail (16 thin segments, smooth gradient) ──
    DrawTrail(cx, cy, r, headC, headS);

    // ── Restore static indicators overwritten by sweep ──
    DrawRadarGrid();

    // ── Bearing labels: redraw every frame so trail never erases them ──
    tft.setTextColor(CLR_RING_BRIGHT);
    tft.setTextSize(1);
    tft.drawCentreString("N", cx, 0, 1);
    tft.drawCentreString("S", cx, 236, 1);
    tft.drawCentreString("W", 236, cy - 3, 1);
    tft.drawCentreString("E", 4, cy - 3, 1);

    // ── Redraw aircraft blips every frame so sweep does not erase targets ──
    for (const auto& [icao, lp] : lastPositions) {
        if (!lp.visible || lp.brightness == 0) continue;
        auto it = trackedAircraft.find(icao);
        if (it == trackedAircraft.end()) continue;
        DrawAircraftBlip(lp.x, lp.y, it->second, lp.brightness);
    }
}

// ── Draw phosphor trail: 32° behind scan line, 16 thin segments ──
void AircraftManager::DrawTrail(int cx, int cy, int r, float headC, float headS)
{
    // Trail starts 32° behind head
    float tailC = headC * TRAIL_TAIL_COS + headS * TRAIL_TAIL_SIN;
    float tailS = headS * TRAIL_TAIL_COS - headC * TRAIL_TAIL_SIN;

    // ── Clear full wedge to black first ──
    tft.fillTriangle(cx, cy,
        cx + (int)(headC * r),   cy - (int)(headS * r),
        cx + (int)(tailC * r),   cy - (int)(tailS * r),
        CLR_BG);

    // Rotate each segment forward toward head (clockwise = +angle)
    constexpr float STEP = 0.0349066f;  // 2° in radians
    float segC = tailC;
    float segS = tailS;

    for (int i = 0; i < TRAIL_SEGMENTS; i++) {
        RotateAngle(segC, segS, STEP);
        uint16_t color = TRAIL_GRADIENT[TRAIL_SEGMENTS - 1 - i];
        tft.fillTriangle(cx, cy,
            cx + (int)(segC * r),   cy - (int)(segS * r),
            cx + (int)(tailC * r),  cy - (int)(tailS * r),
            color);
        tailC = segC;
        tailS = segS;
    }
}

// ── Static grid: rings, ticks, crosshairs ──
// Drawn ONCE in Initialise() — never redraw during animation
void AircraftManager::DrawRadarGrid() const
{
    const int cx = 120, cy = 120;

    // Concentric range rings
    tft.drawCircle(cx, cy, 110, CLR_RING);
    tft.drawCircle(cx, cy, 74,  CLR_RING);
    tft.drawCircle(cx, cy, 37,  CLR_RING);

    // Crosshairs
    tft.drawFastHLine(1, cy, 238, CLR_CROSSHAIR);
    tft.drawFastVLine(cx, 1, 238, CLR_CROSSHAIR);

    // Tick marks every 30° (12 ticks)
    for (int i = 0; i < 12; i++) {
        float dx = TICK_DIRS[i * 2], dy = TICK_DIRS[i * 2 + 1];
        tft.drawLine(cx + (int)(dx * 106), cy + (int)(dy * 106),
                     cx + (int)(dx * 114), cy + (int)(dy * 114), CLR_RING);
    }

    // ── North tick: longer mark extending outside ring ──
    tft.drawLine(cx, cy - 106, cx, cy - 116, CLR_RING_BRIGHT);
}

void AircraftManager::Draw(LGFX& /*buf*/)
{
    // No-op — rendering is incremental in RefreshAircraft()
}

// ── Fade a base color toward black by brightness level (1-5) ──
// Level 5 = full brightness, level 1 = dim ghost
uint16_t AircraftManager::FadeColor(uint16_t base, uint8_t level) const
{
    if (level <= 0) return CLR_BG;
    if (level >= 5) return base;

    // Extract RGB565 components, scale down by level/5
    uint16_t r5 = (base >> 11) & 0x1F;
    uint16_t g6 = (base >> 5) & 0x3F;
    uint16_t b5 = base & 0x1F;

    uint16_t fr = (r5 * level) / 5;
    uint16_t fg = (g6 * level) / 5;
    uint16_t fb = (b5 * level) / 5;

    return (fr << 11) | (fg << 5) | fb;
}

void AircraftManager::ErasePosition(int x, int y, uint8_t radius) const
{
    tft.fillCircle(x, y, radius, CLR_BG);
}

// ── Draw aircraft blip with PPI brightness scaling ──
void AircraftManager::DrawAircraftBlip(int x, int y, const SimpleAircraft& ac, uint8_t brightness) const
{
    AircraftType type = GetAircraftType(ac);
    uint16_t baseColor;
    switch (type) {
        case AircraftType::MILITARY:  baseColor = CLR_MILITARY;  break;
        case AircraftType::COMMERCIAL: baseColor = CLR_COMMERIAL; break;
        default:                      baseColor = CLR_UNKNOWN;   break;
    }

    uint16_t color = FadeColor(baseColor, brightness);

    if (displayTriangles) {
        // Heading arrow — scale length with brightness
        float hRad = ac.heading * 0.0174533f;
        float scale = 0.4f + (brightness * 0.12f);  // 0.52 to 1.0
        int len = (int)(10.0f * scale);
        int tx = x + (int)(cos(hRad) * len);
        int ty = y - (int)(sin(hRad) * len);
        tft.drawLine(x, y, tx, ty, color);
        // Arrowhead dot only at higher brightness
        if (brightness >= 3) {
            tft.fillCircle(tx, ty, 2, color);
        }
    } else {
        // Blip — scale radius with brightness (1px dim ghost to 3px full)
        int radius = 1 + (brightness >= 4 ? 2 : (brightness >= 2 ? 1 : 0));
        tft.fillCircle(x, y, radius, color);
    }
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float lat2, float lon2) const
{
    if (rad <= 0.001f) return {999, 999};

    double dx = lat2 - lat;
    double dy = (lon2 - lon) * cos(lat * 0.0174533);
    double dist = sqrt(dx * dx + dy * dy);

    float screenDist = (float)(dist / rad) * 110.0f;
    double bearing = atan2(dy, dx);

    int sx = 120 + (int)(screenDist * cos(bearing));
    int sy = 120 - (int)(screenDist * sin(bearing));
    return {sx, sy};
}

void AircraftManager::FetchLocal()
{
    String host = configServer.GetStoredString("readsbhost");
    if (host.isEmpty()) {
        static int warnCount = 0;
        if (++warnCount <= 3) Serial.println("[FETCH] No readsb host configured");
        return;
    }

    String port = configServer.GetStoredString("readsbport");
    if (port.isEmpty()) port = "8080";

    String path = configServer.GetStoredString("readsbpath");
    if (path.isEmpty()) path = "/data/aircraft.json";

    String url = "http://" + host + ":" + port + path;
    Serial.printf("[FETCH] GET %s\n", url.c_str());

    HttpResult result = http.Get(url);
    if (!result.success) {
        Serial.printf("[FETCH] FAILED: code=%d err=%s\n", result.statusCode, result.errorMessage.c_str());
        return;
    }
    Serial.printf("[FETCH] Got %d bytes\n", result.response.length());
    if (result.response.length() == 0) {
        Serial.println("[FETCH] Empty response");
        return;
    }
    if (result.response.length() > MAX_RESP_BYTES) {
        Serial.printf("[FETCH] Response %d bytes > %d cap, discarding\n",
                       result.response.length(), MAX_RESP_BYTES);
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, result.response);
    if (err) {
        Serial.printf("[FETCH] JSON parse error: %s\n", err.c_str());
        doc.clear();
        return;
    }

    auto arr = doc["aircraft"];
    if (!arr.is<JsonArray>()) {
        Serial.println("[FETCH] No 'aircraft' array in JSON");
        doc.clear();
        return;
    }

    std::map<String, SimpleAircraft> next;
    for (int i = 0; i < arr.size() && next.size() < MAX_AIRCRAFT; i++) {
        auto item = arr[i];
        const char* hexVal = item["hex"];
        if (!hexVal) continue;
        String icao(hexVal);
        if (icao.isEmpty()) continue;

        double latVal = item["lat"] | 0.0;
        double lonVal = item["lon"] | 0.0;
        if (latVal == 0 && lonVal == 0) continue;

        SimpleAircraft ac;
        ac.icao      = icao;
        ac.lat       = latVal;
        ac.lon       = lonVal;
        ac.altitude  = item["alt_baro"] | 0.0;
        ac.heading   = item["track"] | 0.0;
        if (isnan(ac.heading)) ac.heading = 0.0;
        const char* sq = item["squawk"];
        ac.squawk    = sq ? sq : "";

        next[icao] = ac;
    }

    doc.clear();
    trackedAircraft = next;
    Serial.printf("[FETCH] Tracked %d aircraft\n", trackedAircraft.size());
}

// ── Legacy stub ──
void AircraftManager::OpenSky() {}

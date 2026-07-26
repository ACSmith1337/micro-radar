#include "AircraftManager.h"

#include <ArduinoJson.h>
#include <algorithm>
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
constexpr uint16_t CLR_COMMERIAL   = 0x07E0;       // Civilian green
constexpr uint16_t CLR_MILITARY    = 0xF800;       // Red
constexpr uint16_t CLR_UNKNOWN     = 0x0520;       // Dark green

// ─── Timing ───
constexpr uint32_t SCAN_INTERVAL   = 40;           // ~25fps for smoother sweep on ESP8266
constexpr uint32_t ROTATION_MS     = 6000;         // 1 full sweep = 6s
constexpr uint32_t FETCH_DEFAULT   = ROTATION_MS;  // fetch at each rotation
constexpr int      MAX_AIRCRAFT    = 24;           // draw/load protection
constexpr int      MAX_RESP_BYTES  = 8192;         // heap protection
constexpr float    SCAN_SPEED      = (6.2831853f / ROTATION_MS);  // exact 1 rev / 6s
constexpr uint8_t  AIRCRAFT_ERASE_RADIUS = 22;     // clears icon + heading vector fully

// Ring geometry (outer is max range; inner rings are ~66% and ~33%).
constexpr int      RING_OUTER_PX   = 110;
constexpr int      RING_MID_PX     = (RING_OUTER_PX * 2) / 3;
constexpr int      RING_INNER_PX   = (RING_OUTER_PX * 1) / 3;

// ── Trail: 30° visual, 32° total with 2° black safety margin ──
// 10 segments spanning 32° total wedge.
// Tail-side segments are black, head-side segments are green.
constexpr int   TRAIL_SEGMENTS    = 10;
constexpr float TRAIL_STEP_DEG    = (32.0f / TRAIL_SEGMENTS);
// Precomputed cos(32°) and sin(32°) for tail calculation
constexpr float TRAIL_TAIL_COS    = 0.8480481f;     // cos(32°)
constexpr float TRAIL_TAIL_SIN    = 0.5299193f;     // sin(32°)

// Phosphor green gradient for 10 segments: black tail → green head.
constexpr uint16_t TRAIL_GRADIENT[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000,                                   // Hard black tail erase (7)
    0x00C0, 0x01C0, 0x0320                            // Subtle green near head (3)
};  // 10 entries = TRAIL_SEGMENTS

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

static String FormatRangeNm(float nm)
{
    if (nm < 10.0f) {
        int tenths = (int)(nm * 10.0f + 0.5f);
        int whole = tenths / 10;
        int frac = tenths % 10;
        return String(whole) + "." + String(frac) + "nm";
    }
    return String((int)(nm + 0.5f)) + "nm";
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
    String maxRangeNmStr = configServer.GetStoredString("maxrange");
    if (!maxRangeNmStr.isEmpty()) {
        rad = maxRangeNmStr.toFloat() / 60.0f;
    } else {
        // Backward compatibility with old config key (degrees).
        rad = configServer.GetStoredString("radius").toFloat();
    }
    displayInfoText = configServer.GetStoredString("infotext") == "true";
    displayTriangles = configServer.GetStoredString("triangle") == "true";
    displayScanLine = configServer.GetStoredString("scanline") != "false";

    // Force ADS-B fetch cadence to one update per full revolution.
    fetchInterval = FETCH_DEFAULT;

    // Range ring labels in NM (outer=max range, mid≈66%, inner≈33%).
    const float outerNm = rad * 60.0f;
    ringLabelInner = FormatRangeNm(outerNm * ((float)RING_INNER_PX / (float)RING_OUTER_PX));
    ringLabelMid   = FormatRangeNm(outerNm * ((float)RING_MID_PX   / (float)RING_OUTER_PX));
    ringLabelOuter = FormatRangeNm(outerNm);

    Serial.printf("[RADAR] lat=%.6f lon=%.6f rad=%.2f deg (%.1f NM outer)\n", lat, lon, rad, outerNm);
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

    // ── Called once per rotation: fetch one ADS-B frame ──
    if (millis() - lastRotation >= fetchInterval) {
        lastRotation = millis();
        RefreshAircraft();
    }

    // ── Scan animation (cadence-locked, ~25fps target) ──
    // Use scheduled ticks instead of "set last=now" to reduce frame jitter.
    static uint32_t nextScan = 0;
    uint32_t now = millis();
    if (nextScan == 0) nextScan = now;
    if ((int32_t)(now - nextScan) >= 0) {
        DrawRadarFrame();
        nextScan += SCAN_INTERVAL;
        // If we fell far behind (WiFi/fetch stall), resync cleanly.
        if ((uint32_t)(now - nextScan) > (SCAN_INTERVAL * 4)) {
            nextScan = now + SCAN_INTERVAL;
        }
    }

    // ── PPI phosphor decay: dim blips once per second ──
    // 5 brightness levels → blips fade over ~5 seconds, gone by next rotation
    static uint32_t lastDecay = 0;
    if (millis() - lastDecay >= 1000) {
        DecayAircraft();
        lastDecay = millis();
    }
}

// ── Called once per rotation: fetch data + update projected positions ──
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
            if (lp.visible) ErasePosition(lp.x, lp.y, AIRCRAFT_ERASE_RADIUS);
            gone.push_back(icao);
        }
    }
    for (auto& icao : gone) lastPositions.erase(icao);

    // Update all tracked aircraft positions.
    // PPI behavior: brightness is refreshed only when the sweep touches the blip.
    for (auto& [icao, ac] : trackedAircraft) {
        auto proj = ProjectCoordinateToScreen(ac.lat, ac.lon);
        int x = proj.first, y = proj.second;
        bool on = (x > 0 && x < 239 && y > 0 && y < 239);

        if (on) {
            // Erase old position if it moved and was visible
            if (lastPositions.count(icao) && lastPositions[icao].visible) {
                ErasePosition(lastPositions[icao].x, lastPositions[icao].y, AIRCRAFT_ERASE_RADIUS);
            }
            uint8_t b = lastPositions.count(icao) ? lastPositions[icao].brightness : 0;
            lastPositions[icao] = {x, y, true, b};
        } else {
            if (lastPositions.count(icao) && lastPositions[icao].visible) {
                ErasePosition(lastPositions[icao].x, lastPositions[icao].y, AIRCRAFT_ERASE_RADIUS);
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
        if (!lp.visible || lp.brightness == 0) continue;

        lp.brightness--;

        if (lp.brightness == 0) {
            // Fully faded — erase with black
            ErasePosition(lp.x, lp.y, 10);
            faded.push_back(icao);
        } else {
            // Redraw at new lower brightness
            if (trackedAircraft.count(icao)) {
                auto& ac = trackedAircraft.at(icao);
                // Clear previous larger/brighter vector first so heading line truly fades.
                ErasePosition(lp.x, lp.y, 22);
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
    const int r = 119;             // Scan/trail to near panel edge
    const int erase_r = 121;       // slight overdraw to kill edge residue

    // ── Advance scan angle by elapsed time (exact 360° per ROTATION_MS) ──
    constexpr float DEG1 = 0.0174533f; // visual beam width
    static uint32_t lastStepMs = 0;
    uint32_t nowMs = millis();
    uint32_t dtMs = (lastStepMs == 0) ? SCAN_INTERVAL : (nowMs - lastStepMs);
    lastStepMs = nowMs;
    if (dtMs > 250) dtMs = SCAN_INTERVAL; // clamp long stalls/reconnects

    float delta = SCAN_SPEED * (float)dtMs;
    float prevHeadC = scanState.c;
    float prevHeadS = scanState.s;
    RotateAngle(scanState.c, scanState.s, -delta);

    // Renormalise every ~180 frames to prevent incremental drift
    static int normCount = 0;
    if (++normCount >= 180) {
        Renormalise(scanState.c, scanState.s);
        normCount = 0;
    }

    float headC = scanState.c;
    float headS = scanState.s;

    // ── Beam geometry: hard-black erase behind head, then bright head only ──
    // Erase tracks measured delta so no green residue survives frame jitter.

    // Dynamic tail erase: lag and width scale with current frame step.
    float eraseLag = delta + (DEG1 * 1.5f);
    float eraseWidth = delta + (DEG1 * 2.0f);
    if (eraseLag < (DEG1 * 2.0f)) eraseLag = DEG1 * 2.0f;
    if (eraseWidth < (DEG1 * 2.0f)) eraseWidth = DEG1 * 2.0f;
    if (eraseLag > (DEG1 * 10.0f)) eraseLag = DEG1 * 10.0f;
    if (eraseWidth > (DEG1 * 10.0f)) eraseWidth = DEG1 * 10.0f;

    float eraseC = headC, eraseS = headS;
    RotateAngle(eraseC, eraseS, eraseLag);
    float eraseNextC = eraseC, eraseNextS = eraseS;
    RotateAngle(eraseNextC, eraseNextS, eraseWidth);

    tft.fillTriangle(cx, cy,
        cx + (int)(eraseC * erase_r), cy - (int)(eraseS * erase_r),
        cx + (int)(eraseNextC * erase_r), cy - (int)(eraseNextS * erase_r),
        CLR_BG);

    // Bright scan line (1° wedge at leading edge)
    float headNextC = headC + headS * DEG1;
    float headNextS = headS - headC * DEG1;
    tft.fillTriangle(cx, cy,
        cx + (int)(headC * r), cy - (int)(headS * r),
        cx + (int)(headNextC * r), cy - (int)(headNextS * r),
        CLR_SCAN);

    // Bridge large frame steps near edge to remove outer-ring skipping.
    // Draw only in outer third (66%-100%) to keep center beam narrow.
    int bridgeSteps = (int)(delta / DEG1);
    if (bridgeSteps > 1) {
        if (bridgeSteps > 6) bridgeSteps = 6;
        const int bridgeInnerR = (r * 66) / 100;
        float bridgeC = headC;
        float bridgeS = headS;
        for (int i = 1; i < bridgeSteps; i++) {
            RotateAngle(bridgeC, bridgeS, DEG1);
            tft.drawLine(
                cx + (int)(bridgeC * bridgeInnerR), cy - (int)(bridgeS * bridgeInnerR),
                cx + (int)(bridgeC * r),            cy - (int)(bridgeS * r),
                CLR_SCAN);
            // Pin the beam tip at outer edge so endpoint does not strobe/skip.
            tft.fillCircle(
                cx + (int)(bridgeC * r), cy - (int)(bridgeS * r),
                1, CLR_SCAN);
        }

        // Clear trailing bridge remnants in the erase sector to prevent green leftovers.
        float clearC = headC;
        float clearS = headS;
        RotateAngle(clearC, clearS, eraseLag);
        for (int i = 1; i < bridgeSteps; i++) {
            RotateAngle(clearC, clearS, DEG1);
            tft.drawLine(
                cx + (int)(clearC * bridgeInnerR), cy - (int)(clearS * bridgeInnerR),
                cx + (int)(clearC * erase_r),      cy - (int)(clearS * erase_r),
                CLR_BG);
            tft.fillCircle(
                cx + (int)(clearC * erase_r), cy - (int)(clearS * erase_r),
                1, CLR_BG);
        }
    }

    // ── Restore static indicators overwritten by sweep (throttled) ──
    // Redrawing every frame can starve ESP8266; ~8Hz is sufficient.
    static uint8_t gridDiv = 0;
    if ((++gridDiv % 3) == 0) {
        DrawRadarGrid();
    }

    // ── Bearing labels: redraw every frame so trail never erases them ──
    tft.setTextColor(CLR_RING_BRIGHT);
    tft.setTextSize(1);
    tft.drawCentreString("N", cx, 2, 1);
    tft.drawCentreString("S", cx, 228, 1);
    tft.drawCentreString("E", 236, cy - 3, 1);
    tft.drawCentreString("W", 4, cy - 3, 1);

    // Range labels on each ring (north axis)
    tft.setTextColor(CLR_RING);
    tft.drawString(ringLabelOuter, cx + 6, cy - RING_OUTER_PX + 4, 1);
    tft.drawString(ringLabelMid,   cx + 6, cy - RING_MID_PX   + 4, 1);
    tft.drawString(ringLabelInner, cx + 6, cy - RING_INNER_PX + 4, 1);

    // ── PPI behavior: when beam touches a blip, refresh to full brightness ──
    // Use dynamic tolerance from actual frame step and test current+previous head.
    float touchHalfAngle = delta + (DEG1 * 2.0f);
    if (touchHalfAngle < (DEG1 * 4.0f)) touchHalfAngle = DEG1 * 4.0f;
    if (touchHalfAngle > (DEG1 * 12.0f)) touchHalfAngle = DEG1 * 12.0f;
    float beamTouchCos = cosf(touchHalfAngle);
    for (auto& [icao, lp] : lastPositions) {
        if (!lp.visible) continue;
        if (!trackedAircraft.count(icao)) continue;

        int vx = lp.x - cx;
        int vy = cy - lp.y; // screen Y inverted
        float d2 = (float)(vx * vx + vy * vy);
        if (d2 < 16.0f) continue; // skip near center jitter

        float invD = 1.0f / sqrtf(d2);
        float ux = vx * invD;
        float uy = vy * invD;
        float dotNow  = ux * headC     + uy * headS;
        float dotPrev = ux * prevHeadC + uy * prevHeadS;
        float dot = (dotNow > dotPrev) ? dotNow : dotPrev;

        if (dot >= beamTouchCos) {
            lp.brightness = 5;
            DrawAircraftBlip(lp.x, lp.y, trackedAircraft.at(icao), 5);
        }
    }

    // ── Redraw aircraft blips at reduced rate to lower scan jitter ──
    static uint8_t blipDiv = 0;
    if ((++blipDiv % 3) == 0) {
        for (const auto& [icao, lp] : lastPositions) {
            if (!lp.visible || lp.brightness == 0) continue;
            auto it = trackedAircraft.find(icao);
            if (it == trackedAircraft.end()) continue;
            DrawAircraftBlip(lp.x, lp.y, it->second, lp.brightness);
        }
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

    // Rotate each segment forward toward head
    constexpr float STEP = TRAIL_STEP_DEG * 0.0174533f;
    float segC = tailC;
    float segS = tailS;
    const float tailStartC = tailC;
    const float tailStartS = tailS;

    for (int i = 0; i < TRAIL_SEGMENTS; i++) {
        RotateAngle(segC, segS, STEP);
        uint16_t color = TRAIL_GRADIENT[i];
        tft.fillTriangle(cx, cy,
            cx + (int)(segC * r),   cy - (int)(segS * r),
            cx + (int)(tailC * r),  cy - (int)(tailS * r),
            color);
        tailC = segC;
        tailS = segS;
    }

    // Hard-black guard wedge over oldest trail region (kills tail-end green flash)
    float guardC = tailStartC;
    float guardS = tailStartS;
    constexpr float TAIL_GUARD_STEP = 0.0174533f * 10.0f; // 10°
    RotateAngle(guardC, guardS, TAIL_GUARD_STEP);
    tft.fillTriangle(cx, cy,
        cx + (int)(tailStartC * (r + 2)), cy - (int)(tailStartS * (r + 2)),
        cx + (int)(guardC * (r + 2)),     cy - (int)(guardS * (r + 2)),
        CLR_BG);

    // Force tail tip to black to prevent edge flash on low-res triangle joins
    tft.fillCircle(cx + (int)(tailStartC * r), cy - (int)(tailStartS * r), 5, CLR_BG);
}

// ── Static grid: rings, ticks, crosshairs ──
// Drawn ONCE in Initialise() — never redraw during animation
void AircraftManager::DrawRadarGrid() const
{
    const int cx = 120, cy = 120;

    // Concentric range rings
    tft.drawCircle(cx, cy, RING_OUTER_PX, CLR_RING);
    tft.drawCircle(cx, cy, RING_MID_PX,   CLR_RING);
    tft.drawCircle(cx, cy, RING_INNER_PX, CLR_RING);

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
        // Larger symbol + longer heading indicator
        // ADS-B track is degrees clockwise from North.
        // Screen vector: x=sin(track), y=-cos(track)
        float hRad = ac.heading * 0.0174533f;
        float scale = 0.45f + (brightness * 0.13f);  // 0.58 to 1.10
        int len = (int)(18.0f * scale);              // 10px to 19px
        int coreRadius = 2 + (brightness >= 4 ? 2 : (brightness >= 2 ? 1 : 0));
        tft.fillCircle(x, y, coreRadius, color);
        int tx = x + (int)(sin(hRad) * len);
        int ty = y - (int)(cos(hRad) * len);
        tft.drawLine(x, y, tx, ty, color);
        // No tip dot: keep a single aircraft core marker + heading vector.
    } else {
        // Blip size doubled (2px dim ghost to 6px full)
        int radius = 2 + (brightness >= 4 ? 4 : (brightness >= 2 ? 2 : 0));
        tft.fillCircle(x, y, radius, color);
    }
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float lat2, float lon2) const
{
    if (rad <= 0.001f) return {999, 999};

    constexpr double DEG2RAD_F64 = 0.017453292519943295;
    constexpr double RAD2DEG_F64 = 57.29577951308232;

    const double lat1r = (double)lat * DEG2RAD_F64;
    const double lon1r = (double)lon * DEG2RAD_F64;
    const double lat2r = (double)lat2 * DEG2RAD_F64;
    const double lon2r = (double)lon2 * DEG2RAD_F64;

    const double dlat = lat2r - lat1r;
    const double dlon = lon2r - lon1r;

    // Great-circle central angle (radians).
    const double sinHLat = sin(dlat * 0.5);
    const double sinHLon = sin(dlon * 0.5);
    double a = sinHLat * sinHLat + cos(lat1r) * cos(lat2r) * sinHLon * sinHLon;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    const double central = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

    const double distDeg = central * RAD2DEG_F64;
    const float screenDist = (float)((distDeg / (double)rad) * (double)RING_OUTER_PX);
    if (screenDist <= 1e-6f) return {120, 120};

    // Initial bearing from north, clockwise.
    const double y = sin(dlon) * cos(lat2r);
    const double x = cos(lat1r) * sin(lat2r) - sin(lat1r) * cos(lat2r) * cos(dlon);
    const double brg = atan2(y, x);

    // Screen mapping: +X east (sin), -Y north (cos).
    int sx = 120 + (int)(screenDist * sin(brg));
    int sy = 120 - (int)(screenDist * cos(brg));
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

    // Keep nearest in-range aircraft so low-priority targets (e.g. helicopters)
    // are not dropped just because they appear later in the JSON list.
    std::vector<std::pair<double, SimpleAircraft>> candidates;
    candidates.reserve(arr.size());

    for (int i = 0; i < arr.size(); i++) {
        auto item = arr[i];
        const char* hexVal = item["hex"];
        if (!hexVal) continue;
        String icao(hexVal);
        if (icao.isEmpty()) continue;

        double latVal = item["lat"] | 0.0;
        double lonVal = item["lon"] | 0.0;
        if (latVal == 0.0 && lonVal == 0.0) continue;

        // Coarse in-range filter in degree-space.
        double dLat = latVal - (double)lat;
        double dLon = (lonVal - (double)lon) * cos((double)lat * 0.0174533);
        double distDegApprox = sqrt(dLat * dLat + dLon * dLon);
        if (rad > 0.001f && distDegApprox > (double)rad) continue;

        SimpleAircraft ac;
        ac.icao      = icao;
        ac.lat       = latVal;
        ac.lon       = lonVal;
        ac.altitude  = item["alt_baro"] | 0.0;
        ac.heading   = item["track"] | 0.0;
        if (isnan(ac.heading)) ac.heading = 0.0;
        const char* sq = item["squawk"];
        ac.squawk    = sq ? sq : "";

        candidates.push_back({distDegApprox, ac});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::map<String, SimpleAircraft> next;
    for (size_t i = 0; i < candidates.size() && i < MAX_AIRCRAFT; i++) {
        const auto& ac = candidates[i].second;
        next[ac.icao] = ac;
    }

    doc.clear();
    trackedAircraft = next;
    Serial.printf("[FETCH] In-range=%d tracked=%d (cap=%d)\n",
                  (int)candidates.size(), (int)trackedAircraft.size(), MAX_AIRCRAFT);
}

// ── Legacy stub ──
void AircraftManager::OpenSky() {}

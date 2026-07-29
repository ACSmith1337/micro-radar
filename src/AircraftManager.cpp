#include "AircraftManager.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>

// ─── Phosphor palettes (RGB565) ───
// Green P1 phosphor (P1 is actually yellowish-green CRT)
constexpr uint16_t CLR_RING_G        = 0x0140;       // R:0  G:10 B:0
constexpr uint16_t CLR_RING_BRIGHT_G = 0x07E0;       // R:0  G:63 B:0 (max brightness labels)
constexpr uint16_t CLR_SCAN_G        = 0x07E0;       // R:0  G:63 B:0
constexpr uint16_t CLR_GLOW_G        = 0x05A0;       // R:0  G:42 B:0
constexpr uint16_t CLR_CROSSHAIR_G   = 0x00A0;       // R:0  G:5  B:0
constexpr uint16_t CLR_COMMERIAL_G   = 0x05E0;       // R:0  G:47 B:0
constexpr uint16_t CLR_GLOW_COMM_G   = 0x03E0;       // R:0  G:31 B:0

// Gold P4 phosphor
constexpr uint16_t CLR_RING_A        = 0x5200;       // R:10 G:32 B:0 (dark gold ring)
constexpr uint16_t CLR_RING_BRIGHT_A = 0xE720;       // R:28 G:44 B:0 (bright gold labels)
constexpr uint16_t CLR_SCAN_A        = 0xF720;       // R:31 G:44 B:0 (gold scan line)
constexpr uint16_t CLR_GLOW_A        = 0x7380;       // R:14 G:28 B:0
constexpr uint16_t CLR_CROSSHAIR_A   = 0x0000;       // Invisible (no crosshairs in amber)
constexpr uint16_t CLR_COMMERIAL_A   = 0xE720;       // Bright gold aircraft (match labels)
constexpr uint16_t CLR_GLOW_COMM_A   = 0x6180;       // R:12 G:20 B:0

// Shared
constexpr uint16_t CLR_BG            = 0x0000;
constexpr uint16_t CLR_MILITARY      = 0xF610;       // Orange (not red)
constexpr uint16_t CLR_GLOW_MIL      = 0xF008;       // Dim orange glow
constexpr uint16_t CLR_UNKNOWN       = 0x0520;
constexpr uint16_t CLR_ALERT         = 0xF800;       // Red - emergency squawk only

// ─── Timing ───
constexpr uint32_t SCAN_INTERVAL     = 30;           // ~33fps for smoother sweep
constexpr uint32_t ROTATION_MS       = 10000;        // 1 full sweep = 10s (configurable at runtime)
constexpr uint32_t FETCH_DEFAULT     = ROTATION_MS;  // fetch at each rotation
constexpr uint32_t DECAY_INTERVAL_MS = 350;          // decay tick rate
constexpr uint32_t WARMUP_MS         = 10000;        // startup warm-up screen
constexpr int      MAX_AIRCRAFT      = 24;           // draw/load protection
constexpr int      MAX_RESP_BYTES    = 8192;         // heap protection
constexpr float    SCAN_SPEED        = (6.2831853f / ROTATION_MS);
constexpr uint8_t  AIRCRAFT_ERASE_RADIUS = 18;  // Cover icon + glow + trail dots
constexpr uint8_t  BRIGHTNESS_MAX    = 24;
// Variable fade: strong signals last ~9.5s (24 steps * 350ms * 1.12), weak ~5.5s (24 steps * 350ms * 0.66)
// Decay step = 1 for strong (quality > 0.5), 2 for weak (quality <= 0.5)

// Ring geometry
constexpr int      RING_OUTER_PX     = 110;
constexpr int      RING_MID_PX       = (RING_OUTER_PX * 2) / 3;
constexpr int      RING_INNER_PX     = (RING_OUTER_PX * 1) / 3;

// ── Trail: 6° black erase wedge (no gradient shadow) ──
// 2 black segments behind the beam clear old pixels.
// No gradient ramp = no trailing shadow hiding aircraft.
constexpr int   TRAIL_SEGMENTS    = 2;
constexpr float TRAIL_STEP_DEG    = 3.0f;

// Green phosphor gradient: 2 black erase, no shadow
constexpr uint16_t TRAIL_GRADIENT_G[] = { 0x0000, 0x0000 };

// Amber phosphor gradient: 2 black erase, no shadow
constexpr uint16_t TRAIL_GRADIENT_A[] = { 0x0000, 0x0000 };

// ── Precomputed tick directions (30° increments) ──
constexpr const float TICK_DIRS[] = {
     0, -1,  0.5f, -0.8660f,  0.8660f, -0.5f,
     1,  0,  0.5f,  0.8660f,  0.8660f,  0.5f,
     0,  1, -0.5f,  0.8660f, -0.8660f,  0.5f,
    -1,  0, -0.5f, -0.8660f, -0.8660f, -0.5f
};

// ─── Colour helpers ───
static inline uint16_t PalRing(bool amber)        { return amber ? CLR_RING_A        : CLR_RING_G; }
static inline uint16_t PalRingBright(bool amber)  { return amber ? CLR_RING_BRIGHT_A  : CLR_RING_BRIGHT_G; }
static inline uint16_t PalScan(bool amber)        { return amber ? CLR_SCAN_A         : CLR_SCAN_G; }
static inline uint16_t PalGlow(bool amber)        { return amber ? CLR_GLOW_A         : CLR_GLOW_G; }
static inline uint16_t PalCrosshair(bool amber)   { return amber ? CLR_CROSSHAIR_A    : CLR_CROSSHAIR_G; }
static inline uint16_t PalCommercial(bool amber)  { return amber ? CLR_COMMERIAL_A    : CLR_COMMERIAL_G; }
static inline uint16_t PalGlowComm(bool amber)    { return amber ? CLR_GLOW_COMM_A    : CLR_GLOW_COMM_G; }
static inline const uint16_t* PalTrailGradient(bool amber) { return amber ? TRAIL_GRADIENT_A : TRAIL_GRADIENT_G; }

// ─── Incremental scan state ───
struct ScanState {
    float angle = 0.0f;
    float c = 1.0f;
    float s = 0.0f;
};

static ScanState scanState;
static bool useAmber = false;  // Phosphor colour palette
static ScanMode currentMode = ScanMode::ANGULAR;

// ─── Radial ping state ───
static uint8_t pingRadius = 0;
static uint8_t pingPhase = 0;  // 0=expand, 1=pause
static uint32_t pingLastTime = 0;
constexpr uint32_t PING_EXPAND_MS = 2500;  // ~2.5s expand
constexpr uint32_t PING_PAUSE_MS = 3000;   // ~3s pause
constexpr uint8_t PING_MAX_RADIUS = 160;   // Off-screen (240x240 display)

// ─── Incremental trig ───
static inline void RotateAngle(float &c, float &s, float delta)
{
    float nc = c - s * delta;
    float ns = s + c * delta;
    c = nc;
    s = ns;
}

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

enum class TargetGlyph {
    FIXED_WING,
    HELICOPTER,
    HEAVY,
    UNKNOWN
};

// ─── Aircraft type detection ───
static AircraftType GetAircraftType(const SimpleAircraft& ac)
{
    if (ac.squawk.isEmpty()) {
        return AircraftType::COMMERCIAL;
    }
    int sq = ac.squawk.toInt();
    if ((sq >= 4000 && sq <= 4999) ||
        sq >= 7000) {
        return AircraftType::MILITARY;
    }
    return AircraftType::COMMERCIAL;
}

// ─── Squawk alert detection ───
static bool IsAlertSquawk(const SimpleAircraft& ac)
{
    if (ac.squawk.isEmpty()) return false;
    int sq = ac.squawk.toInt();
    return (sq == 7500 || sq == 7600 || sq == 7700 || sq == 1200);
}

static TargetGlyph GetTargetGlyph(const SimpleAircraft& ac)
{
    if (ac.category == "A7") return TargetGlyph::HELICOPTER;
    if (ac.category == "A5" || ac.category == "A6") return TargetGlyph::HEAVY;
    if (ac.groundspeed > 1.0f) return TargetGlyph::FIXED_WING;
    return TargetGlyph::UNKNOWN;
}

static float ComputeDataAgeSec(const SimpleAircraft& ac, uint32_t msSinceFetch)
{
    float ageAtFetch = ac.seen;
    if (ac.seenPos > ageAtFetch) ageAtFetch = ac.seenPos;
    return ageAtFetch + ((float)msSinceFetch / 1000.0f);
}

static float ComputeQuality01(float ageSec)
{
    if (ageSec <= 2.0f) return 1.0f;
    if (ageSec >= 28.0f) return 0.0f;
    float q = 1.0f - ((ageSec - 2.0f) / 26.0f);
    if (q < 0.0f) q = 0.0f;
    if (q > 1.0f) q = 1.0f;
    return q;
}

static void DeadReckonPosition(const SimpleAircraft& ac, uint32_t msSinceFetch, float& outLat, float& outLon)
{
    outLat = ac.lat;
    outLon = ac.lon;
    if (ac.groundspeed < 2.0f) return;

    float dt = ((float)msSinceFetch / 1000.0f) + ac.seenPos;
    if (dt <= 0.05f || dt > 35.0f) return;

    float trk = ac.heading * 0.0174533f;
    float speedNmPerSec = ac.groundspeed / 3600.0f;
    float distNm = speedNmPerSec * dt;

    float dNorthNm = cosf(trk) * distNm;
    float dEastNm = sinf(trk) * distNm;
    float dLatDeg = dNorthNm / 60.0f;
    float cosLat = cosf(ac.lat * 0.0174533f);
    if (cosLat < 0.01f) cosLat = 0.01f;
    float dLonDeg = dEastNm / (60.0f * cosLat);

    outLat = ac.lat + dLatDeg;
    outLon = ac.lon + dLonDeg;
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
        rad = configServer.GetStoredString("radius").toFloat();
    }
    displayInfoText = configServer.GetStoredString("infotext") == "true";
    displayTriangles = configServer.GetStoredString("triangle") == "true";
    displayScanLine = configServer.GetStoredString("scanline") != "false";
    displayTrailDots = configServer.GetStoredString("trails") == "true";
    alertSquawk = configServer.GetStoredString("squawkalert") == "true";
    useAmber = configServer.GetStoredString("phosphor") == "amber";

    fetchInterval = FETCH_DEFAULT;

    const float outerNm = rad * 60.0f;
    ringLabelInner = FormatRangeNm(outerNm * ((float)RING_INNER_PX / (float)RING_OUTER_PX));
    ringLabelMid   = FormatRangeNm(outerNm * ((float)RING_MID_PX   / (float)RING_OUTER_PX));
    ringLabelOuter = FormatRangeNm(outerNm);

    Serial.printf("[RADAR] lat=%.6f lon=%.6f rad=%.2f deg (%.1f NM outer)\n", lat, lon, rad, outerNm);
    if (rad <= 0.001f) {
        Serial.println("[RADAR] WARNING: radius not set — no aircraft will appear");
    }

    scanState = {0.0f, 1.0f, 0.0f};
    initialSyncComplete = false;
    initialSyncLastAttempt = 0;
    warmupStartMs = millis();
    warmupComplete = false;

    tft.fillScreen(CLR_BG);
    DrawRadarGrid();
    tft.setTextColor(PalRingBright(useAmber));
    tft.setTextSize(1);
    tft.drawCentreString("SYNC READSB...", 120, 112, 1);
}

// ── Live config reload (no restart) ──
void AircraftManager::ReloadDisplayConfig()
{
    bool newAmber = configServer.GetStoredString("phosphor") == "amber";
    if (newAmber != useAmber) {
        useAmber = newAmber;
        Serial.printf("[RADAR] Theme changed to %s\n", useAmber ? "amber" : "green");
    }

    String newMode = configServer.GetStoredString("scanmode");
    if (newMode.isEmpty()) newMode = "angular";
    bool newRadial = newMode == "radial";
    if (newRadial && currentMode != ScanMode::RADIAL) {
        currentMode = ScanMode::RADIAL;
        pingRadius = 0;
        pingPhase = 0;
        pingLastTime = millis();
        Serial.println("[RADAR] Scan mode: radial ping");
    } else if (!newRadial && currentMode != ScanMode::ANGULAR) {
        currentMode = ScanMode::ANGULAR;
        scanState = {0.0f, 1.0f, 0.0f};
        Serial.println("[RADAR] Scan mode: angular sweep");
    }

    // Redraw grid with new palette
    tft.fillScreen(CLR_BG);
    DrawRadarGrid();
    DrawRadarLabels();
}

// ── Apply single setting change (avoids EEPROM re-read race) ──
void AircraftManager::ApplyThemeChange(bool amber)
{
    if (amber != useAmber) {
        useAmber = amber;
        Serial.printf("[RADAR] Theme: %s\n", useAmber ? "amber" : "green");
    }
    tft.fillScreen(CLR_BG);
    DrawRadarGrid();
    DrawRadarLabels();
}

void AircraftManager::ApplyModeChange(bool radial)
{
    if (radial && currentMode != ScanMode::RADIAL) {
        currentMode = ScanMode::RADIAL;
        pingRadius = 0;
        pingPhase = 0;
        pingLastTime = millis();
        Serial.println("[RADAR] Scan mode: radial ping");
    } else if (!radial && currentMode != ScanMode::ANGULAR) {
        currentMode = ScanMode::ANGULAR;
        scanState = {0.0f, 1.0f, 0.0f};
        Serial.println("[RADAR] Scan mode: angular sweep");
    }
    tft.fillScreen(CLR_BG);
    DrawRadarGrid();
    DrawRadarLabels();
}

// ── Common label drawing ──
void AircraftManager::DrawRadarLabels() const
{
    const int cx = 120, cy = 120;
    tft.setTextColor(PalRingBright(useAmber));
    tft.setTextSize(1);
    tft.drawCentreString("N", cx, 2, 1);
    tft.drawCentreString("N", cx + 1, 2, 1);
    tft.drawCentreString("S", cx, 228, 1);
    tft.drawCentreString("S", cx + 1, 228, 1);
    tft.drawCentreString("E", 236, cy - 3, 1);
    tft.drawCentreString("E", 237, cy - 3, 1);
    tft.drawCentreString("W", 4, cy - 3, 1);
    tft.drawCentreString("W", 5, cy - 3, 1);
    tft.drawString(ringLabelOuter, cx + 6, cy - RING_OUTER_PX + 4, 1);
    tft.drawString(ringLabelMid,   cx + 6, cy - RING_MID_PX   + 4, 1);
    tft.drawString(ringLabelInner, cx + 6, cy - RING_INNER_PX + 4, 1);
}

bool AircraftManager::IsAmber() const { return useAmber; }
bool AircraftManager::IsRadial() const { return currentMode == ScanMode::RADIAL; }

void AircraftManager::Update()
{
    static uint32_t lastRotation = 0;

    // ── Startup sequence ──
    if (!initialSyncComplete) {
        uint32_t now = millis();

        static uint32_t lastBlink = 0;
        static bool blinkState = false;
        
        if ((now - lastBlink) >= 500) {
            blinkState = !blinkState;
            lastBlink = now;
            
            if (blinkState) {
                tft.setTextColor(PalRingBright(useAmber), CLR_BG);
                tft.setTextSize(2);
                tft.drawCentreString("RADAR WARMUP", 120, 108, 1);
            } else {
                tft.fillScreen(CLR_BG);
                DrawRadarGrid();
                tft.setTextColor(PalRingBright(useAmber));
                tft.setTextSize(1);
                tft.drawCentreString("N", 120, 4, 1);
                tft.drawCentreString("N", 121, 4, 1);
                tft.drawCentreString("S", 120, 230, 1);
                tft.drawCentreString("S", 121, 230, 1);
                tft.drawCentreString("E", 230, 116, 1);
                tft.drawCentreString("E", 231, 116, 1);
                tft.drawCentreString("W", 10, 116, 1);
                tft.drawCentreString("W", 11, 116, 1);
            }
        }

        if ((now - initialSyncLastAttempt) >= 1500 || (now - warmupStartMs) >= 10000) {
            initialSyncLastAttempt = now;
            if (RefreshAircraft() || (now - warmupStartMs) >= 10000) {
                initialSyncComplete = true;
                lastRotation = now;
                tft.fillScreen(CLR_BG);
                DrawRadarGrid();
                if ((now - warmupStartMs) >= 10000) {
                    Serial.println("[RADAR] Warmup timeout, starting sweep without sync");
                } else {
                    Serial.println("[RADAR] Initial sync complete, starting sweep");
                }
            }
        }
        return;
    }

    // ── Fetch once per rotation (or more often after failures) ──
    if (millis() - lastRotation >= fetchInterval) {
        lastRotation = millis();
        if (!RefreshAircraft()) {
            // Fetch failed - retry immediately, then fetch more often next cycle
            delay(500);
            RefreshAircraft();
            fetchInterval = 5000;  // Fetch every 5s until success
        } else {
            fetchInterval = FETCH_DEFAULT;  // Back to normal 10s
        }
    }

    // ── Scan animation ──
    static uint32_t nextScan = 0;
    uint32_t now = millis();
    if (nextScan == 0) nextScan = now;
    if ((int32_t)(now - nextScan) >= 0) {
        DrawRadarFrame();
        nextScan += SCAN_INTERVAL;
        if ((uint32_t)(now - nextScan) > (SCAN_INTERVAL * 4)) {
            nextScan = now + SCAN_INTERVAL;
        }
    }

    // ── PPI phosphor decay ──
    static uint32_t lastDecay = 0;
    if (millis() - lastDecay >= DECAY_INTERVAL_MS) {
        DecayAircraft();
        lastDecay = millis();
    }
}

// ── Called once per rotation ──
bool AircraftManager::RefreshAircraft()
{
    StorePrev(trackedAircraft, prevPositions);
    bool fetchOk = FetchLocal();
    if (!fetchOk) {
        Serial.println("[FETCH] Network error - keeping existing aircraft");
        return false;
    }
    lastFetch = millis();

    std::vector<String> gone;
    for (auto& [icao, lp] : lastPositions) {
        if (!trackedAircraft.count(icao)) {
            if (lp.visible) ErasePosition(lp.x, lp.y, AIRCRAFT_ERASE_RADIUS);
            gone.push_back(icao);
        }
    }
    for (auto& icao : gone) lastPositions.erase(icao);

    for (auto& [icao, ac] : trackedAircraft) {
        auto proj = ProjectCoordinateToScreen(ac.lat, ac.lon);
        int x = proj.first, y = proj.second;
        bool on = (x > 0 && x < 239 && y > 0 && y < 239);

        if (on) {
            if (lastPositions.count(icao) && lastPositions[icao].visible) {
                ErasePosition(lastPositions[icao].x, lastPositions[icao].y, AIRCRAFT_ERASE_RADIUS);
            }
            uint8_t b = lastPositions.count(icao) ? lastPositions[icao].brightness : (BRIGHTNESS_MAX / 2);
            lastPositions[icao] = {x, y, true, b};
        } else {
            if (lastPositions.count(icao) && lastPositions[icao].visible) {
                ErasePosition(lastPositions[icao].x, lastPositions[icao].y, AIRCRAFT_ERASE_RADIUS);
            }
            lastPositions[icao] = {x, y, false, 0};
        }
    }

    return true;
}

// ── Decay blip brightness (variable rate based on signal quality) ──
void AircraftManager::DecayAircraft()
{
    std::vector<String> faded;
    for (auto& [icao, lp] : lastPositions) {
        if (!lp.visible || lp.brightness == 0) continue;

        // Strong signals (quality > 0.5) decay 1 step, weak decay 2 steps
        uint8_t step = 1;
        if (trackedAircraft.count(icao)) {
            float ageSec = ComputeDataAgeSec(trackedAircraft.at(icao), millis() - lastFetch);
            float quality = ComputeQuality01(ageSec);
            if (quality <= 0.5f) step = 2;
        }

        if (lp.brightness < step) lp.brightness = 0;
        else lp.brightness -= step;

        if (lp.brightness == 0) {
            ErasePosition(lp.x, lp.y, AIRCRAFT_ERASE_RADIUS);
            faded.push_back(icao);
        } else {
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

// ── Incremental scan frame ──
void AircraftManager::DrawRadarFrame()
{
    if (!displayScanLine) return;

    const int cx = 120, cy = 120;
    const int r = 119;

    // ── Mode switch: angular sweep vs radial ping ──
    if (currentMode == ScanMode::RADIAL) {
        DrawRadarPing(cx, cy, r);
        return;
    }

    // ── Advance scan angle ──
    constexpr float DEG1 = 0.0174533f;
    static uint32_t lastStepMs = 0;
    uint32_t nowMs = millis();
    uint32_t dtMs = (lastStepMs == 0) ? SCAN_INTERVAL : (nowMs - lastStepMs);
    lastStepMs = nowMs;
    if (dtMs > 250) dtMs = SCAN_INTERVAL;

    float delta = SCAN_SPEED * (float)dtMs;
    float prevHeadC = scanState.c;
    float prevHeadS = scanState.s;
    RotateAngle(scanState.c, scanState.s, -delta);

    static int normCount = 0;
    if (++normCount >= 180) {
        Renormalise(scanState.c, scanState.s);
        normCount = 0;
    }

    float headC = scanState.c;
    float headS = scanState.s;

    // ── Phosphor trail (clears + fades behind beam) ──
    DrawTrail(cx, cy, r, headC, headS);

    // ── Bright scan line ──
    // Use RING_OUTER_PX so beam doesn't overshoot the outer ring
    const int beamR = RING_OUTER_PX;
    float headNextC = headC + headS * DEG1;
    float headNextS = headS - headC * DEG1;
    tft.fillTriangle(cx, cy,
        cx + (int)(headC * beamR), cy - (int)(headS * beamR),
        cx + (int)(headNextC * beamR), cy - (int)(headNextS * beamR),
        PalScan(useAmber));

    // ── Clean beam edge (erase overshoot beyond outer ring) ──
    for (int e = beamR; e <= r; e++) {
        tft.drawLine(
            cx + (int)(headC * e), cy - (int)(headS * e),
            cx + (int)(headNextC * e), cy - (int)(headNextS * e),
            CLR_BG);
    }

    // ── Grid: redraw every frame so trail never erases it ──
    DrawRadarGrid();

    // ── Bearing labels ──
    tft.setTextColor(PalRingBright(useAmber));
    tft.setTextSize(1);
    tft.drawCentreString("N", cx, 2, 1);
    tft.drawCentreString("N", cx + 1, 2, 1);
    tft.drawCentreString("S", cx, 228, 1);
    tft.drawCentreString("S", cx + 1, 228, 1);
    tft.drawCentreString("E", 236, cy - 3, 1);
    tft.drawCentreString("E", 237, cy - 3, 1);
    tft.drawCentreString("W", 4, cy - 3, 1);
    tft.drawCentreString("W", 5, cy - 3, 1);

    // ── Range labels ──
    tft.drawString(ringLabelOuter, cx + 6, cy - RING_OUTER_PX + 4, 1);
    tft.drawString(ringLabelMid,   cx + 6, cy - RING_MID_PX   + 4, 1);
    tft.drawString(ringLabelInner, cx + 6, cy - RING_INNER_PX + 4, 1);

    // ── Update projected positions (dead-reckoning) ──
    uint32_t sinceFetchMs = millis() - lastFetch;
    for (auto& [icao, lp] : lastPositions) {
        auto it = trackedAircraft.find(icao);
        if (it == trackedAircraft.end()) continue;

        float predLat = it->second.lat;
        float predLon = it->second.lon;
        DeadReckonPosition(it->second, sinceFetchMs, predLat, predLon);

        auto proj = ProjectCoordinateToScreen(predLat, predLon);
        int nx = proj.first;
        int ny = proj.second;
        bool on = (nx > 0 && nx < 239 && ny > 0 && ny < 239);

        // Erase old position if aircraft moved (even by 1 pixel)
        if (lp.visible && (!on || nx != lp.x || ny != lp.y)) {
            ErasePosition(lp.x, lp.y, AIRCRAFT_ERASE_RADIUS);
        }

        if (on) {
            lp.x = nx;
            lp.y = ny;
            lp.visible = true;
        } else {
            lp.visible = false;
            lp.brightness = 0;
        }
    }

    // ── PPI beam-hit refresh ──
    // When the beam touches a target, illuminate it to full brightness (like real PPI)
    float touchHalfAngle = delta + (DEG1 * 2.0f);
    if (touchHalfAngle < (DEG1 * 4.0f)) touchHalfAngle = DEG1 * 4.0f;
    if (touchHalfAngle > (DEG1 * 12.0f)) touchHalfAngle = DEG1 * 12.0f;
    float beamTouchCos = cosf(touchHalfAngle);
    for (auto& [icao, lp] : lastPositions) {
        if (!lp.visible) continue;
        if (!trackedAircraft.count(icao)) continue;

        int vx = lp.x - cx;
        int vy = cy - lp.y;
        float d2 = (float)(vx * vx + vy * vy);
        if (d2 < 16.0f) continue;

        float invD = 1.0f / sqrtf(d2);
        float ux = vx * invD;
        float uy = vy * invD;
        float dotNow  = ux * headC     + uy * headS;
        float dotPrev = ux * prevHeadC + uy * prevHeadS;
        float dot = (dotNow > dotPrev) ? dotNow : dotPrev;

        if (dot >= beamTouchCos) {
            lp.brightness = BRIGHTNESS_MAX;
            DrawAircraftBlip(lp.x, lp.y, trackedAircraft.at(icao), lp.brightness);
        }
    }

    // ── Redraw aircraft blips every frame (prevents ghost trails) ──
    for (const auto& [icao, lp] : lastPositions) {
        if (!lp.visible || lp.brightness == 0) continue;
        auto it = trackedAircraft.find(icao);
        if (it == trackedAircraft.end()) continue;
        DrawAircraftBlip(lp.x, lp.y, it->second, lp.brightness);
    }

    // ── Squawk alert: flash emergency squawk text ──
    if (alertSquawk) {
        static uint32_t lastAlertBlink = 0;
        static bool alertVisible = false;
        static String alertText;
        static String alertIcao;

        String currentAlert;
        String currentIcao;
        for (const auto& [icao, ac] : trackedAircraft) {
            if (IsAlertSquawk(ac)) {
                currentAlert = "SQUAWK " + ac.squawk;
                currentIcao = icao;
                break;
            }
        }

        if (currentIcao != alertIcao) {
            alertText = currentAlert;
            alertIcao = currentIcao;
            alertVisible = true;
            lastAlertBlink = millis();
        }

        if (alertVisible && !alertText.isEmpty()) {
            uint32_t now = millis();
            if (now - lastAlertBlink >= 400) {
                alertVisible = !alertVisible;
                lastAlertBlink = now;
            }
            if (alertVisible) {
                tft.setTextColor(CLR_ALERT, CLR_BG);
                tft.setTextSize(1);
                tft.drawCentreString(alertText.c_str(), cx, 220, 1);
            } else {
                tft.setTextColor(PalRingBright(useAmber), CLR_BG);
                tft.drawCentreString(alertText.c_str(), cx, 220, 1);
            }
        }
    }
}

// ── Draw phosphor trail ──
void AircraftManager::DrawTrail(int cx, int cy, int r, float headC, float headS)
{
    constexpr float STEP = TRAIL_STEP_DEG * 0.0174533f;
    const uint16_t* gradient = PalTrailGradient(useAmber);

    float prevC = headC;
    float prevS = headS;

    for (int i = 0; i < TRAIL_SEGMENTS; i++) {
        float segC = prevC * cosf(STEP) - prevS * sinf(STEP);
        float segS = prevS * cosf(STEP) + prevC * sinf(STEP);
        uint16_t color = gradient[i];
        tft.fillTriangle(cx, cy,
            cx + (int)(prevC * r), cy - (int)(prevS * r),
            cx + (int)(segC * r),  cy - (int)(segS * r),
            color);
        prevC = segC; prevS = segS;
    }
}

// ── Radial ping (sonar mode) ──
void AircraftManager::DrawRadarPing(int cx, int cy, int r)
{
    (void)r;
    uint32_t now = millis();
    if (pingLastTime == 0) pingLastTime = now;

    uint32_t elapsed = now - pingLastTime;

    // Track previous ring radius to erase it (no full screen clear)
    static uint8_t prevRadius = 255;

    switch (pingPhase) {
        case 0: { // EXPAND: ring grows from center off screen
            float progress = (float)elapsed / (float)PING_EXPAND_MS;
            if (progress > 1.0f) progress = 1.0f;
            pingRadius = (uint8_t)(progress * PING_MAX_RADIUS);

            // Hit detection: illuminate aircraft when ring crosses their distance
            for (auto& [icao, lp] : lastPositions) {
                if (!lp.visible) continue;
                if (!trackedAircraft.count(icao)) continue;
                int vx = lp.x - cx;
                int vy = cy - lp.y;
                float dist = sqrtf((float)(vx * vx + vy * vy));
                if (dist > 0 && abs((int)dist - pingRadius) <= 3) {
                    lp.brightness = BRIGHTNESS_MAX;
                }
            }

            if (elapsed >= PING_EXPAND_MS) {
                pingPhase = 1;
                pingLastTime = now;
            }
            break;
        }
        case 1: { // PAUSE: blank grid, waiting for next ping
            if (elapsed >= PING_PAUSE_MS) {
                pingPhase = 0;
                pingRadius = 0;
                pingLastTime = now;
            }
            break;
        }
    }

    // ── Draw: erase previous ring, then draw grid + labels + ring ──
    if (prevRadius > 0 && prevRadius <= PING_MAX_RADIUS + 2) {
        tft.drawCircle(cx, cy, prevRadius, CLR_BG);
        tft.drawCircle(cx, cy, prevRadius + 1, CLR_BG);
        tft.drawCircle(cx, cy, prevRadius + 2, CLR_BG);
    }

    // Redraw grid + labels every frame so ring erase doesn't corrupt them
    DrawRadarGrid();
    DrawRadarLabels();

    // ── Draw single ring at current radius (3px thick, no persistence) ──
    if (pingPhase == 0 && pingRadius > 0) {
        uint16_t ringColor = PalScan(useAmber);
        tft.drawCircle(cx, cy, pingRadius, ringColor);
        tft.drawCircle(cx, cy, pingRadius + 1, ringColor);
        tft.drawCircle(cx, cy, pingRadius + 2, ringColor);
        prevRadius = pingRadius;
    } else {
        prevRadius = 255;
    }

    // ── Draw aircraft blips ──
    for (const auto& [icao, lp] : lastPositions) {
        if (!lp.visible || lp.brightness == 0) continue;
        auto it = trackedAircraft.find(icao);
        if (it == trackedAircraft.end()) continue;
        DrawAircraftBlip(lp.x, lp.y, it->second, lp.brightness);
    }
}

// ── Static grid ──
void AircraftManager::DrawRadarGrid() const
{
    const int cx = 120, cy = 120;
    uint16_t ringClr = PalRing(useAmber);
    uint16_t crosshairClr = PalCrosshair(useAmber);

    tft.drawCircle(cx, cy, RING_OUTER_PX, ringClr);
    tft.drawCircle(cx, cy, RING_MID_PX,   ringClr);
    tft.drawCircle(cx, cy, RING_INNER_PX, ringClr);

    tft.drawFastHLine(1, cy, 238, crosshairClr);
    tft.drawFastVLine(cx, 1, 238, crosshairClr);

    for (int i = 0; i < 12; i++) {
        if (i == 0 || i == 3 || i == 6 || i == 9) continue;
        float dx = TICK_DIRS[i * 2], dy = TICK_DIRS[i * 2 + 1];
        tft.drawLine(cx + (int)(dx * 106), cy + (int)(dy * 106),
                     cx + (int)(dx * 114), cy + (int)(dy * 114), ringClr);
    }

    tft.drawLine(cx, cy - 106, cx, cy - 116, PalRingBright(useAmber));
}

void AircraftManager::Draw(LGFX& /*buf*/)
{
}

// ── Fade color toward black ──
uint16_t AircraftManager::FadeColor(uint16_t base, uint8_t level) const
{
    if (level <= 0) return CLR_BG;
    if (level >= BRIGHTNESS_MAX) return base;

    uint16_t r5 = (base >> 11) & 0x1F;
    uint16_t g6 = (base >> 5) & 0x3F;
    uint16_t b5 = base & 0x1F;

    uint16_t fr = (r5 * level) / BRIGHTNESS_MAX;
    uint16_t fg = (g6 * level) / BRIGHTNESS_MAX;
    uint16_t fb = (b5 * level) / BRIGHTNESS_MAX;

    return (fr << 11) | (fg << 5) | fb;
}

void AircraftManager::ErasePosition(int x, int y, uint8_t radius) const
{
    tft.fillCircle(x, y, radius, CLR_BG);

    const int cx = 120, cy = 120;
    uint16_t ringClr = PalRing(useAmber);
    uint16_t crosshairClr = PalCrosshair(useAmber);
    tft.drawCircle(cx, cy, RING_OUTER_PX, ringClr);
    tft.drawCircle(cx, cy, RING_MID_PX,   ringClr);
    tft.drawCircle(cx, cy, RING_INNER_PX, ringClr);
    tft.drawFastHLine(1, cy, 238, crosshairClr);
    tft.drawFastVLine(cx, 1, 238, crosshairClr);
}

// ── Draw aircraft blip ──
void AircraftManager::DrawAircraftBlip(int x, int y, const SimpleAircraft& ac, uint8_t brightness) const
{
    AircraftType type = GetAircraftType(ac);
    TargetGlyph glyph = GetTargetGlyph(ac);

    uint32_t sinceFetchMs = millis() - lastFetch;
    float ageSec = ComputeDataAgeSec(ac, sinceFetchMs);
    float quality = ComputeQuality01(ageSec);
    if (quality <= 0.03f) return;

    // Brightness maps linearly: BRIGHTNESS_MAX = full bright (scan line level), 0 = black
    uint8_t effective = (uint8_t)((float)brightness * (0.25f + 0.75f * quality));
    if (effective < 1) effective = 1;
    if (effective > BRIGHTNESS_MAX) effective = BRIGHTNESS_MAX;

    // Use scan line color as the base (full bright), fade toward aircraft color as it decays
    uint16_t baseColor;
    uint16_t glowColor;
    switch (type) {
        case AircraftType::MILITARY:
            baseColor = CLR_MILITARY;
            glowColor = CLR_GLOW_MIL;
            break;
        case AircraftType::COMMERCIAL:
            baseColor = PalScan(useAmber);  // Full bright = scan line color
            glowColor = PalGlow(useAmber);
            break;
        default:
            baseColor = CLR_UNKNOWN;
            glowColor = CLR_UNKNOWN;
            break;
    }

    uint16_t color = FadeColor(baseColor, effective);

    float hRad = ac.heading * 0.0174533f;

    // ── Trail dots (dotted path behind aircraft) ──
    // Draw BEFORE glow and icon so they sit underneath
    if (displayTrailDots && ac.groundspeed > 5.0f && effective > BRIGHTNESS_MAX * 0.1f) {
        float outerNm = rad * 60.0f;
        if (outerNm < 0.5f) outerNm = 0.5f;
        float trailNm = ac.groundspeed * (30.0f / 3600.0f);
        float trailPx = (trailNm / outerNm) * (float)RING_OUTER_PX;
        if (trailPx > 24.0f) trailPx = 24.0f;
        if (trailPx >= 3.0f) {
            for (int d = 1; d <= 6; d++) {
                float frac = (float)d / 6.0f;
                int dx = x - (int)(sin(hRad) * trailPx * frac);
                int dy = y + (int)(cos(hRad) * trailPx * frac);
                if (dx > 1 && dx < 238 && dy > 1 && dy < 238) {
                    uint16_t dc = FadeColor(baseColor, (uint8_t)(effective * (0.6f - 0.08f * frac)));
                    tft.drawPixel(dx, dy, dc);
                    tft.drawPixel(dx + 1, dy, dc);
                }
            }
        }
    } else {
        // Velocity vector: 15s look-ahead
        float outerNm = rad * 60.0f;
        if (outerNm < 0.5f) outerNm = 0.5f;
        float leadNm = ac.groundspeed * (15.0f / 3600.0f);
        float leadPx = (leadNm / outerNm) * (float)RING_OUTER_PX;
        if (leadPx > 14.0f) leadPx = 14.0f;
        if (leadPx >= 2.0f) {
            int vx = x + (int)(sin(hRad) * leadPx);
            int vy = y - (int)(cos(hRad) * leadPx);
            tft.drawLine(x, y, vx, vy, FadeColor(baseColor, (uint8_t)(effective * 0.6f)));
        }
    }

    // Phosphor glow (drawn after trail dots, before icon)
    if (effective > BRIGHTNESS_MAX * 0.2f) {
        uint16_t glowFaded = FadeColor(glowColor, (uint8_t)(effective * 0.5f));
        tft.fillCircle(x, y, 3, glowFaded);
    }

    // Class glyphs
    switch (glyph) {
        case TargetGlyph::HELICOPTER: {
            const int rr = 5;
            tft.drawCircle(x, y, rr, color);
            tft.drawLine(x - rr + 1, y - rr + 1, x + rr - 1, y + rr - 1, color);
            tft.drawLine(x - rr + 1, y + rr - 1, x + rr - 1, y - rr + 1, color);
            break;
        }
        case TargetGlyph::HEAVY: {
            const int size = 6;
            tft.fillTriangle(x, y - size, x - size, y + size, x + size, y + size, color);
            break;
        }
        case TargetGlyph::FIXED_WING:
        default: {
            const int size = 5;
            tft.fillTriangle(x, y - size, x - size, y + size, x + size, y + size, color);
            break;
        }
    }

    // Heading cue for helicopters only
    if (glyph == TargetGlyph::HELICOPTER) {
        int tx = x + (int)(sin(hRad) * 10);
        int ty = y - (int)(cos(hRad) * 10);
        tft.drawLine(x, y, tx, ty, FadeColor(baseColor, (uint8_t)(effective * 0.85f)));
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

    const double sinHLat = sin(dlat * 0.5);
    const double sinHLon = sin(dlon * 0.5);
    double a = sinHLat * sinHLat + cos(lat1r) * cos(lat2r) * sinHLon * sinHLon;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    const double central = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

    const double distDeg = central * RAD2DEG_F64;
    const float screenDist = (float)((distDeg / (double)rad) * (double)RING_OUTER_PX);
    if (screenDist <= 1e-6f) return {120, 120};

    const double y = sin(dlon) * cos(lat2r);
    const double x = cos(lat1r) * sin(lat2r) - sin(lat1r) * cos(lat2r) * cos(dlon);
    const double brg = atan2(y, x);

    int sx = 120 + (int)(screenDist * sin(brg));
    int sy = 120 - (int)(screenDist * cos(brg));
    return {sx, sy};
}

bool AircraftManager::FetchLocal()
{
    String host = configServer.GetStoredString("readsbhost");
    if (host.isEmpty()) {
        static int warnCount = 0;
        if (++warnCount <= 3) Serial.println("[FETCH] No readsb host configured");
        return false;
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
        return false;
    }
    Serial.printf("[FETCH] Got %d bytes\n", result.response.length());
    if (result.response.length() == 0) {
        Serial.println("[FETCH] Empty response");
        return false;
    }
    if (result.response.length() > MAX_RESP_BYTES) {
        Serial.printf("[FETCH] Response %d bytes > %d cap, discarding\n",
                       result.response.length(), MAX_RESP_BYTES);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, result.response);
    if (err) {
        Serial.printf("[FETCH] JSON parse error: %s\n", err.c_str());
        doc.clear();
        return false;
    }

    auto arr = doc["aircraft"];
    if (!arr.is<JsonArray>()) {
        Serial.println("[FETCH] No 'aircraft' array in JSON");
        doc.clear();
        return false;
    }

    std::vector<std::pair<double, SimpleAircraft>> candidates;
    candidates.reserve(arr.size());

    int droppedNoPos = 0;
    for (size_t i = 0; i < arr.size(); i++) {
        auto item = arr[i];
        const char* hexVal = item["hex"];
        if (!hexVal) continue;
        String icao(hexVal);
        if (icao.isEmpty()) continue;

        double latVal = item["lat"] | 0.0;
        double lonVal = item["lon"] | 0.0;
        if (latVal == 0.0 && lonVal == 0.0) {
            droppedNoPos++;
            continue;
        }

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
        ac.groundspeed = item["gs"] | 0.0;
        if (isnan(ac.groundspeed) || ac.groundspeed < 0.0f) ac.groundspeed = 0.0f;
        ac.seen = item["seen"] | 0.0;
        if (isnan(ac.seen) || ac.seen < 0.0f) ac.seen = 0.0f;
        ac.seenPos = item["seen_pos"] | ac.seen;
        if (isnan(ac.seenPos) || ac.seenPos < 0.0f) ac.seenPos = ac.seen;
        const char* cat = item["category"];
        ac.category = cat ? cat : "";
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
    Serial.printf("[FETCH] In-range=%d tracked=%d (cap=%d) dropped_no_pos=%d\n",
                  (int)candidates.size(), (int)trackedAircraft.size(), MAX_AIRCRAFT, droppedNoPos);
    return true;
}

// ── Legacy stub ──
void AircraftManager::OpenSky() {}

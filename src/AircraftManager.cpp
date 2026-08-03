#include "AircraftManager.h"

#include "ConfigurationWebServer.h"
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
constexpr uint16_t CLR_MILITARY      = 0xFD20;       // Bright orange (R:31 G:21 B:0)
constexpr uint16_t CLR_GLOW_MIL      = 0xF608;       // Dim orange glow
constexpr uint16_t CLR_UNKNOWN       = 0x0520;
constexpr uint16_t CLR_ALERT         = 0xF800;       // Red - emergency squawk only

// ─── Timing ───
constexpr uint32_t SCAN_INTERVAL     = 30;           // ~33fps for smoother sweep
constexpr uint32_t ROTATION_MS       = 10000;        // 1 full sweep = 10s (configurable at runtime)
constexpr uint32_t FETCH_DEFAULT     = ROTATION_MS;  // fetch at each rotation
constexpr uint32_t DECAY_INTERVAL_MS = 350;          // decay tick rate
constexpr uint32_t WARMUP_MS         = 10000;        // startup warm-up screen
constexpr int      MAX_AIRCRAFT      = 48;           // draw/load protection
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
    if (mag < 1e-6f) { c = 1.0f; s = 0.0f; return; }
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

    GridLog(String("[RADAR] lat=").c_str());
    if (rad <= 0.001f) {
        GridLog("[RADAR] WARNING: radius not set — no aircraft will appear");
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
    // ── Reload range (affects ring labels + aircraft filtering) ──
    String newMaxRangeNmStr = configServer.GetStoredString("maxrange");
    if (!newMaxRangeNmStr.isEmpty()) {
        rad = newMaxRangeNmStr.toFloat() / 60.0f;
    } else {
        rad = configServer.GetStoredString("radius").toFloat();
    }
    const float outerNm = rad * 60.0f;
    ringLabelInner = FormatRangeNm(outerNm * ((float)RING_INNER_PX / (float)RING_OUTER_PX));
    ringLabelMid   = FormatRangeNm(outerNm * ((float)RING_MID_PX   / (float)RING_OUTER_PX));
    ringLabelOuter = FormatRangeNm(outerNm);
    Serial.printf("[RADAR] Range updated: %.1f NM outer\n", outerNm);

    // ── Reload theme ──
    bool newAmber = configServer.GetStoredString("phosphor") == "amber";
    if (newAmber != useAmber) {
        useAmber = newAmber;
        Serial.printf("[RADAR] Theme changed to %s\n", useAmber ? "amber" : "green");
    }

    // ── Reload scan mode ──
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

    // ── Reload display toggles ──
    displayInfoText = configServer.GetStoredString("infotext") == "true";
    displayTriangles = configServer.GetStoredString("triangle") == "true";
    displayScanLine = configServer.GetStoredString("scanline") != "false";
    displayTrailDots = configServer.GetStoredString("trails") == "true";
    alertSquawk = configServer.GetStoredString("squawkalert") == "true";

    // ── Redraw everything with new config ──
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

// ── Force sync (static, called from web UI) ──
bool AircraftManager::forceSyncRequested = false;

void AircraftManager::RequestForceSync()
{
    forceSyncRequested = true;
    GridLog("[RADAR] Force sync requested");
}

bool AircraftManager::HasForceSyncRequested()
{
    return forceSyncRequested;
}

void AircraftManager::Update()
{
    static uint32_t lastRotation = 0;

    // ── Startup sequence ──
    if (!initialSyncComplete) {
        uint32_t now = millis();

        // Show warmup text once on entry
        static bool warmupShown = false;
        if (!warmupShown) {
            warmupShown = true;
            tft.fillScreen(CLR_BG);
            DrawRadarGrid();
            DrawRadarLabels();
            tft.setTextColor(PalRingBright(useAmber), CLR_BG);
            tft.setTextSize(2);
            tft.drawCentreString("RADAR WARMUP", 120, 108, 1);
        }

        if ((now - initialSyncLastAttempt) >= 1500 || (now - warmupStartMs) >= 10000) {
            initialSyncLastAttempt = now;
            if (RefreshAircraft() || (now - warmupStartMs) >= 10000) {
                initialSyncComplete = true;
                lastRotation = now;
                tft.fillScreen(CLR_BG);
                DrawRadarGrid();
                if ((now - warmupStartMs) >= 10000) {
                    GridLog("[RADAR] Warmup timeout, starting sweep without sync");
                } else {
                    GridLog("[RADAR] Initial sync complete, starting sweep");
                }
            }
        }
        return;
    }

    // ── Fetch once per rotation (or more often after failures) ──
    bool forceSync = AircraftManager::HasForceSyncRequested();
    if (millis() - lastRotation >= fetchInterval || forceSync) {
        if (forceSync) {
            AircraftManager::forceSyncRequested = false;
            GridLog("[RADAR] Executing force sync");
        }
        lastRotation = millis();
        if (!RefreshAircraft()) {
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
    String dataSource = configServer.GetStoredString("datasource");
    if (dataSource.isEmpty()) dataSource = "local";

    bool fetchOk = false;
    if (dataSource == "local") {
        fetchOk = FetchLocal();
    } else if (dataSource == "adsblol") {
        // ADSB.lol: enforce 60s minimum between fetches (rate limit / memory)
        // Skip gate on first fetch (lastFetch == 0 means never fetched)
        constexpr uint32_t ADSBLol_FETCH_MIN = 60000;
        if (lastFetch > 0 && millis() - lastFetch < ADSBLol_FETCH_MIN) {
            return true; // skip — too soon
        }
        fetchOk = FetchAdsblol();
    } else {
        Serial.printf("[FETCH] Unknown datasource: %s\n", dataSource.c_str());
        return false;
    }

    if (!fetchOk) {
        GridLog("[FETCH] Network error - keeping existing aircraft");
        return false;
    }
    lastFetch = millis();

    // Update positions from API data. Only update x/y coordinates — do NOT
    // touch brightness or visible state. Visibility is controlled solely by beam hits and decay.
    // Aircraft persist as long as the feeder reports them.

    for (auto& [icao, ac] : trackedAircraft) {
        auto proj = ProjectCoordinateToScreen(ac.lat, ac.lon);
        int x = proj.first, y = proj.second;
        bool on = (x > 0 && x < 239 && y > 0 && y < 239);

        // ── Record trail history ──
        if (on && displayTrailDots) {
            auto& hist = trailHistories[icao];
            hist.points[hist.head].x = x;
            hist.points[hist.head].y = y;
            hist.points[hist.head].timestamp = millis();
            hist.head = (hist.head + 1) % TRAIL_HISTORY_MAX;
            if (hist.count < TRAIL_HISTORY_MAX) hist.count++;
        }

        auto lpIt = lastPositions.find(icao);
        if (on) {
            // New aircraft start at half brightness — beam will set to full on contact
            // Existing aircraft keep their current brightness and visible state
            if (lpIt == lastPositions.end()) {
                lastPositions[icao] = {x, y, true, BRIGHTNESS_MAX / 2, ac.rssi};
            } else {
                lpIt->second.x = x;
                lpIt->second.y = y;
                lpIt->second.rssi = ac.rssi;
            }
        } else {
            // Off-screen — update position but don't kill visibility
            // Dead reckoning will handle position between syncs
            if (lpIt != lastPositions.end()) {
                lpIt->second.x = x;
                lpIt->second.y = y;
                lpIt->second.rssi = ac.rssi;
            } else {
                lastPositions[icao] = {x, y, false, 0, ac.rssi};
            }
        }
    }

    return true;
}

// ── Decay blip brightness (RSSI-based: 4.4s weak → 8.8s strong) ──
// Tracked aircraft decay at their fixed position. Beam recharges on contact.
// Ghosts (left the feed) also decay, then are removed when faded.
void AircraftManager::DecayAircraft()
{
    for (auto& [icao, lp] : lastPositions) {
        // Skip fully faded ghosts — they'll be cleaned up
        if (!lp.visible && lp.brightness == 0 && !trackedAircraft.count(icao)) continue;

        // Skip invisible aircraft — wait for beam to recharge
        if (!lp.visible) continue;

        // RSSI-based fade duration
        float rssi = lp.rssi;
        float fadeDuration = 6.6f;
        if (rssi < 0.0f) {
            float rssiNorm = (rssi + 90.0f) / 60.0f;
            if (rssiNorm < 0.0f) rssiNorm = 0.0f;
            if (rssiNorm > 1.0f) rssiNorm = 1.0f;
            fadeDuration = 4.4f + 4.4f * rssiNorm;
        }

        float steps = fadeDuration / 0.350f;
        float stepSize = (float)BRIGHTNESS_MAX / steps;

        float& acc = decayAccumulators[icao];
        acc += stepSize;
        while (acc >= 1.0f) {
            acc -= 1.0f;
            if (lp.brightness > 0) lp.brightness--;
        }

        if (lp.brightness == 0) {
            ErasePosition(lp.x, lp.y, AIRCRAFT_ERASE_RADIUS);
            lp.visible = false;
            decayAccumulators.erase(icao);
        } else {
            // Draw decaying aircraft — brightness drives the visual fade
            auto acIt = trackedAircraft.find(icao);
            if (acIt != trackedAircraft.end()) {
                DrawAircraftBlip(lp.x, lp.y, acIt->second, lp.brightness);
            } else {
                SimpleAircraft ghost;
                ghost.category = "";
                ghost.squawk = "";
                DrawAircraftBlip(lp.x, lp.y, ghost, lp.brightness);
            }
        }
    }

    // Remove ghosts that have fully faded
    std::vector<String> gone;
    for (auto& [icao, lp] : lastPositions) {
        if (!lp.visible && lp.brightness == 0 && !trackedAircraft.count(icao)) {
            gone.push_back(icao);
        }
    }
    for (auto& icao : gone) {
        lastPositions.erase(icao);
        trailHistories.erase(icao);
    }
}

// ── Shared alert state accessible from both scan modes ──
struct AlertGlobals {
    static bool blinkOn;
    static char icaoBuf[8];
    static char textBuf[32];
    static bool active;
};
bool AlertGlobals::blinkOn = true;
char AlertGlobals::icaoBuf[8] = {0};
char AlertGlobals::textBuf[32] = {0};
bool AlertGlobals::active = false;

// ── Update alert state (call once per frame from both scan modes) ──
void AircraftManager::UpdateAlertState(bool displayAlerts)
{
    static uint32_t lastBlink = 0;
    static uint32_t lastCycle = 0;
    static int idx = 0;
    static char pool[16][8];
    constexpr uint32_t CYCLE_MS = 3000;

    int count = 0;
    if (displayAlerts) {
        for (const auto& [k, ac] : trackedAircraft) {
            if (IsAlertSquawk(ac)) {
                if (count < 16) {
                    strncpy(pool[count], k.c_str(), 7);
                    pool[count][7] = '\0';
                    count++;
                }
            }
        }
    }

    if (count > 0) {
        uint32_t now = millis();
        char* cur;
        if (count == 1) {
            cur = pool[0];
        } else {
            if (now - lastCycle >= CYCLE_MS) {
                idx = (idx + 1) % count;
                lastCycle = now;
                AlertGlobals::blinkOn = true;
                lastBlink = now;
            }
            cur = pool[idx];
        }
        if (strcmp(AlertGlobals::icaoBuf, cur) != 0) {
            strncpy(AlertGlobals::icaoBuf, cur, 7);
            AlertGlobals::icaoBuf[7] = '\0';
            const char* icaoC = AlertGlobals::icaoBuf;
            SimpleAircraft* ac = nullptr;
            for (auto& [k, v] : trackedAircraft) {
                if (strncmp(k.c_str(), icaoC, 8) == 0) { ac = &v; break; }
            }
            if (ac) snprintf(AlertGlobals::textBuf, sizeof(AlertGlobals::textBuf), "SQUAWK %s", ac->squawk.c_str());
        }
        AlertGlobals::active = true;
    } else {
        if (AlertGlobals::active) {
            tft.fillRect(80, 214, 80, 12, CLR_BG);
            AlertGlobals::active = false;
        }
        AlertGlobals::textBuf[0] = '\0';
        AlertGlobals::icaoBuf[0] = '\0';
        idx = 0;
    }

    if (AlertGlobals::textBuf[0] != '\0') {
        uint32_t now = millis();
        if (now - lastBlink >= 400) {
            AlertGlobals::blinkOn = !AlertGlobals::blinkOn;
            lastBlink = now;
        }
    }
}

// ── Draw alert text (call from both scan modes) ──
void AircraftManager::DrawAlertText(bool displayAlerts)
{
    if (displayAlerts && AlertGlobals::textBuf[0] != '\0') {
        uint16_t col = AlertGlobals::blinkOn ? CLR_ALERT : CLR_RING_BRIGHT_A;
        tft.setTextColor(col, CLR_BG);
        tft.setTextSize(1);
        tft.drawCentreString(AlertGlobals::textBuf, 120, 220, 1);
    }
}

// ── Draw all aircraft blips with alert flash support ──
void AircraftManager::DrawAllAircraft(bool displayAlerts)
{
    for (const auto& [icao, lp] : lastPositions) {
        if (!lp.visible || lp.brightness == 0) continue;
        auto it = trackedAircraft.find(icao);
        if (it == trackedAircraft.end()) continue;
        if (displayAlerts && AlertGlobals::icaoBuf[0] != '\0' && strncmp(icao.c_str(), AlertGlobals::icaoBuf, 8) == 0) {
            uint16_t flashCol = AlertGlobals::blinkOn ? CLR_ALERT : CLR_RING_BRIGHT_A;
            DrawAircraftBlip(lp.x, lp.y, it->second, lp.brightness, flashCol);
        } else {
            DrawAircraftBlip(lp.x, lp.y, it->second, lp.brightness);
        }
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
    const int beamR = RING_OUTER_PX;
    float headNextC = headC + headS * DEG1;
    float headNextS = headS - headC * DEG1;
    tft.fillTriangle(cx, cy,
        cx + (int)(headC * beamR), cy - (int)(headS * beamR),
        cx + (int)(headNextC * beamR), cy - (int)(headNextS * beamR),
        PalScan(useAmber));

    // ── Clean beam edge ──
    for (int e = beamR + 1; e <= beamR + 2; e++) {
        int x1 = cx + (int)(headC * e);
        int y1 = cy - (int)(headS * e);
        int x2 = cx + (int)(headNextC * e);
        int y2 = cy - (int)(headNextS * e);
        if ((x1 < 0 || x1 >= 240 || y1 < 0 || y1 >= 240) &&
            (x2 < 0 || x2 >= 240 || y2 < 0 || y2 >= 240)) continue;
        x1 = max(0, min(239, x1)); y1 = max(0, min(239, y1));
        x2 = max(0, min(239, x2)); y2 = max(0, min(239, y2));
        tft.drawLine(x1, y1, x2, y2, CLR_BG);
    }

    // ── Grid + labels ──
    DrawRadarGrid();
    DrawRadarLabels();

    // ── PPI beam-hit refresh ──
    float touchHalfAngle = delta + (DEG1 * 2.0f);
    if (touchHalfAngle < (DEG1 * 4.0f)) touchHalfAngle = DEG1 * 4.0f;
    if (touchHalfAngle > (DEG1 * 12.0f)) touchHalfAngle = DEG1 * 12.0f;
    float beamTouchCos = cosf(touchHalfAngle);
    for (auto& [icao, lp] : lastPositions) {
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
            lp.visible = true;
            decayAccumulators[icao] = 0.0f;
            auto it = trackedAircraft.find(icao);
            if (it != trackedAircraft.end()) {
                DrawAircraftBlip(lp.x, lp.y, it->second, lp.brightness);
            } else {
                SimpleAircraft ghost;
                ghost.category = "";
                ghost.squawk = "";
                DrawAircraftBlip(lp.x, lp.y, ghost, lp.brightness);
            }
        }
    }

    // ── Alert update + draw ──
    UpdateAlertState(alertSquawk);
    DrawAllAircraft(alertSquawk);
    DrawAlertText(alertSquawk);
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

            // Hit detection: recharge all aircraft when ring crosses their distance
            for (auto& [icao, lp] : lastPositions) {
                int vx = lp.x - cx;
                int vy = cy - lp.y;
                float dist = sqrtf((float)(vx * vx + vy * vy));
                if (dist > 0 && abs((int)dist - pingRadius) <= 3) {
                    lp.brightness = BRIGHTNESS_MAX;
                    lp.visible = true;  // Re-illuminate faded aircraft
                    decayAccumulators[icao] = 0.0f;  // Reset decay accumulator
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

    // ── Alert update + draw ──
    UpdateAlertState(alertSquawk);
    DrawAllAircraft(alertSquawk);
    DrawAlertText(alertSquawk);
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
// Precomputed LUT: 25 levels (0..24) × 256 possible base colors = too big
// Instead: per-component LUT. Each RGB565 component is 0..63.
// fadeLUT[64][25] maps (component_value, brightness_level) → faded component
// But that's 64×25 = 1600 entries × 1 byte = 1.6KB in flash — worth it.
// Simpler: just scale component by level/BRIGHTNESS_MAX using a small multiplier table.
// fadeScale[level] = (level * 256 + BRIGHTNESS_MAX/2) / BRIGHTNESS_MAX  (fixed-point ×256)
// result = (component * fadeScale[level]) >> 8  — one multiply, one shift
static const uint8_t fadeScale[25] = {
    0, 10, 21, 32, 43, 53, 64, 75, 85, 96, 107, 117, 128, 139, 149, 160, 171, 181, 192, 203, 213, 224, 235, 245, 256
};

static inline uint16_t FadeColor(uint16_t base, uint8_t level)
{
    if (level <= 0) return CLR_BG;
    if (level >= BRIGHTNESS_MAX) return base;
    uint8_t s = fadeScale[level];
    uint16_t r5 = (base >> 11) & 0x1F;
    uint16_t g6 = (base >> 5) & 0x3F;
    uint16_t b5 = base & 0x1F;
    uint16_t fr = (r5 * s) >> 8;
    uint16_t fg = (g6 * s) >> 8;
    uint16_t fb = (b5 * s) >> 8;
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
    DrawAircraftBlip(x, y, ac, brightness, 0);
}

void AircraftManager::DrawAircraftBlip(int x, int y, const SimpleAircraft& ac, uint8_t brightness, uint16_t overrideColor) const
{
    AircraftType type = GetAircraftType(ac);
    TargetGlyph glyph = GetTargetGlyph(ac);

    // Brightness maps linearly: BRIGHTNESS_MAX = full bright (scan line level), 0 = black
    // Decay controls fade — no quality gate on drawing
    uint8_t effective = brightness;
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

    uint16_t color;
    if (overrideColor) {
        color = overrideColor;
    } else {
        color = FadeColor(baseColor, effective);
    }

    float hRad = ac.heading * 0.0174533f;

    // ── Trail (dashed line through position history) ──
    // Draw BEFORE glow and icon so they sit underneath
    if (displayTrailDots) {
        auto histIt = trailHistories.find(ac.icao);
        if (histIt != trailHistories.end()) {
            const auto& hist = histIt->second;
            if (hist.count >= 2) {
                uint32_t now = millis();
                // Build list of valid trail points (not expired)
                struct TrailPt { int x, y; uint8_t bright; };
                TrailPt pts[TRAIL_HISTORY_MAX];
                int pCount = 0;
                for (int n = 0; n < hist.count; n++) {
                    int idx = (hist.head - hist.count + n + TRAIL_HISTORY_MAX) % TRAIL_HISTORY_MAX;
                    const auto& tp = hist.points[idx];
                    float ageSec = (float)(now - tp.timestamp) / 1000.0f;
                    float fade = 1.0f - (ageSec / 600.0f);
                    if (fade <= 0.0f) continue;
                    uint8_t trailBright = (uint8_t)(effective * fade * 0.6f);
                    if (trailBright < 1) continue;
                    pts[pCount++] = {tp.x, tp.y, trailBright};
                }
                // Draw dashed line: draw every other segment
                for (int i = 0; i < pCount - 1; i += 2) {
                    uint16_t dc;
                    if (overrideColor) {
                        dc = FadeColor(overrideColor, pts[i].bright);
                    } else {
                        dc = FadeColor(baseColor, pts[i].bright);
                    }
                    tft.drawLine(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, dc);
                }
            }
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
        GridLog("[FETCH] FAILED");
        return false;
    }
    Serial.printf("[FETCH] Got %d bytes\n", result.response.length());
    if (result.response.length() == 0) {
        GridLog("[FETCH] Empty response");
        return false;
    }
    if (result.response.length() > MAX_RESP_BYTES) {
        Serial.printf("[FETCH] Response %d bytes > %d cap, discarding\n",
                       result.response.length(), MAX_RESP_BYTES);
        return false;
    }

    // Use static document with fixed capacity to avoid heap fragmentation
    static StaticJsonDocument<8192> doc;
    doc.clear();
    DeserializationError err = deserializeJson(doc, result.response);
    if (err) {
        GridLog("[FETCH] JSON parse error");
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
    int droppedStale = 0;
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
        ac.rssi      = item["rssi"] | 0.0f;

        // Drop stale aircraft (seen_pos > 30s at source)
        if (ac.seenPos > 30.0f) {
            droppedStale++;
            continue;
        }

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

    // Merge: update existing aircraft with new data, add new ones
    // Don't remove aircraft not in this fetch — they persist until feeder stops reporting
    for (auto& [icao, ac] : next) {
        trackedAircraft[icao] = ac;
    }

    GridLog("[FETCH] OK");
    return true;
}

// ── Fetch from ADSB.lol API (streaming — no buffer cap) ──
bool AircraftManager::FetchAdsblol()
{
#if defined(ARDUINO_ARCH_ESP8266)
    if (lat == 0.0f || lon == 0.0f) {
        static int warnCount = 0;
        if (++warnCount <= 3) Serial.println("[FETCH] ADSB.lol: lat/lon not configured");
        return false;
    }
    if (rad <= 0.001f) {
        static int warnCount = 0;
        if (++warnCount <= 3) Serial.println("[FETCH] ADSB.lol: range not configured");
        return false;
    }

    int rangeNm = (int)(rad * 60.0f + 0.5f);
    if (rangeNm < 1) rangeNm = 1;
    String url = "http://api.adsb.lol/v2/lat/" + String(lat, 6) + "/lon/" + String(lon, 6) + "/dist/" + String(rangeNm);
    Serial.printf("[FETCH] ADSB.lol GET %s\n", url.c_str());

    HttpStreamResult stream = http.StreamGet(url);
    if (!stream.success) {
        Serial.printf("[FETCH] ADSB.lol FAILED: code=%d err=%s\n", stream.statusCode, stream.errorMessage.c_str());
        return false;
    }

    // Use static document with fixed capacity to avoid heap fragmentation
    static StaticJsonDocument<8192> doc;
    doc.clear();
    DeserializationError err = deserializeJson(doc, *stream.client);
    stream.client->stop();
    delete stream.client;

    if (err) {
        Serial.printf("[FETCH] ADSB.lol JSON parse error: %s\n", err.c_str());
        doc.clear();
        return false;
    }
    Serial.printf("[FETCH] ADSB.lol doc size=%d\n", doc.memoryUsage());

    auto arr = doc["ac"];
    if (!arr.is<JsonArray>()) {
        Serial.println("[FETCH] ADSB.lol No 'ac' array in JSON");
        doc.clear();
        return false;
    }

    std::vector<std::pair<double, SimpleAircraft>> candidates;
    candidates.reserve(arr.size());

    int droppedNoPos = 0;
    int droppedStale = 0;
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
        ac.rssi      = item["rssi"] | 0.0f;

        // Drop stale aircraft (seen_pos > 30s at source)
        if (ac.seenPos > 30.0f) {
            droppedStale++;
            continue;
        }

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

    // Merge: update existing aircraft with new data, add new ones
    // Don't remove aircraft not in this fetch — they persist until feeder stops reporting
    for (auto& [icao, ac] : next) {
        trackedAircraft[icao] = ac;
    }

    GridLog("[FETCH] ADSB.lol OK");
    return true;
#else
    (void)lat; (void)lon; (void)rad;
    Serial.println("[FETCH] ADSB.lol: streaming not available on this platform");
    return false;
#endif
}

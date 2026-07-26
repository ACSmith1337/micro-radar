#line 1 "/home/hermes/micro-radar/src/AircraftManager.cpp"
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
constexpr uint16_t CLR_SCAN        = 0x07FF;       // Bright green scan line for visible PPI sweep (max brightness)
constexpr uint16_t CLR_GLOW        = 0x05A0;       // Scan glow
constexpr uint16_t CLR_TRAIL       = 0x0320;       // Phosphor fade
constexpr uint16_t CLR_CROSSHAIR   = 0x00A0;       // Barely visible
constexpr uint16_t CLR_COMMERIAL   = 0x05E0;       // Civilian green (darker than scan line)
constexpr uint16_t CLR_MILITARY    = 0xF800;       // Red
constexpr uint16_t CLR_UNKNOWN     = 0x0520;       // Dark green
constexpr uint16_t CLR_GLOW_COMM   = 0x03E0;       // Commercial aircraft glow
constexpr uint16_t CLR_GLOW_MIL    = 0xFC00;       // Military aircraft glow

// ─── Timing ───
constexpr uint32_t SCAN_INTERVAL   = 30;           // ~33fps for smoother sweep on ESP8266 (increased from 40ms)
constexpr uint32_t ROTATION_MS     = 10000;        // 1 full sweep = 10s
constexpr uint32_t FETCH_DEFAULT   = ROTATION_MS;  // fetch at each rotation
constexpr uint32_t DECAY_INTERVAL_MS = 250;        // 24 levels @ 250ms = ~6s fade, more responsive (decreased from 300ms)
constexpr uint32_t WARMUP_MS       = 10000;        // startup warm-up screen before first sync
constexpr int      MAX_AIRCRAFT    = 24;           // draw/load protection
constexpr int      MAX_RESP_BYTES  = 8192;         // heap protection
constexpr float    SCAN_SPEED      = (6.2831853f / ROTATION_MS);  // exact 1 rev / 10s
constexpr uint8_t  AIRCRAFT_ERASE_RADIUS = 14;     // tighter erase to reduce background disturbance
constexpr uint8_t  BRIGHTNESS_MAX  = 24;           // smoother phosphor persistence ceiling

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
// Enhanced gradient with more noticeable steps for realistic phosphor bloom
constexpr uint16_t TRAIL_GRADIENT[] = {
    0x0000, 0x0000, 0x0020, 0x0040,
    0x00A0, 0x0140, 0x0220, 0x0360,
    0x04E0, 0x06C0
};  // 10 entries = TRAIL_SEGMENTS — visible black→green phosphor ramp

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

enum class TargetGlyph {
    FIXED_WING,
    HELICOPTER,
    HEAVY,
    UNKNOWN
};

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
    // Fresh <=2s. Fade confidence toward zero by ~28s.
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

    // Use position age + local elapsed time so the plot reflects actual stale offset.
    float dt = ((float)msSinceFetch / 1000.0f) + ac.seenPos;
    if (dt <= 0.05f || dt > 35.0f) return;

    float trk = ac.heading * 0.0174533f;
    float speedNmPerSec = ac.groundspeed / 3600.0f;
    float distNm = speedNmPerSec * dt;

    // Local tangent projection (fast, stable for short dt)
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

    // ── Reset scan/startup state ──
    scanState = {0.0f, 1.0f, 0.0f};
    initialSyncComplete = false;
    initialSyncLastAttempt = 0;
    warmupStartMs = millis();
    warmupComplete = false;

    // ── Clear screen + draw grid + startup status ──
    tft.fillScreen(CLR_BG);
    DrawRadarGrid();
    tft.setTextColor(CLR_RING_BRIGHT);
    tft.setTextSize(1);
    tft.drawCentreString("SYNC READSB...", 120, 112, 1);
}

void AircraftManager::Update()
{
    static uint32_t lastRotation = 0;

    // ── Startup sequence: warm-up display, then gate on first valid readsb sync ──
    if (!initialSyncComplete) {
        uint32_t now = millis();

        // Flashing warmup indicator - no countdown since sync time is unknown
        static uint32_t lastBlink = 0;
        static bool blinkState = false;
        
        if ((now - lastBlink) >= 500) {  // Toggle every 500ms
            blinkState = !blinkState;
            lastBlink = now;
            
            // Draw or erase text without affecting grid
            if (blinkState) {
                tft.setTextColor(CLR_RING_BRIGHT, CLR_BG);
                tft.setTextSize(2);  // Larger text
                tft.drawCentreString("RADAR WARMUP", 120, 108, 1);
            } else {
                // Redraw entire grid to erase text and restore all elements
                tft.fillScreen(CLR_BG);
                DrawRadarGrid();
                // Redraw bearing labels with slightly larger text
                tft.setTextColor(CLR_RING_BRIGHT);
                tft.setTextSize(1);  // Base size
                // Draw bold by drawing twice with offset
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

        // Attempt silent sync in background (no visual feedback)
        // Also allow timeout after 10 seconds to prevent getting stuck
        if ((now - initialSyncLastAttempt) >= 1500 || (now - warmupStartMs) >= 10000) {
            initialSyncLastAttempt = now;
            if (RefreshAircraft() || (now - warmupStartMs) >= 10000) {
                initialSyncComplete = true;
                lastRotation = now;
                // Final grid redraw when starting sweep
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

    // ── Called once per rotation: fetch one ADS-B frame ──
    if (lastRotation == 0 || (millis() - lastRotation) >= fetchInterval) {
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

    // ── PPI phosphor decay: dim blips at fixed cadence ──
    // 24 brightness levels @ 375ms -> ~9 seconds fade out.
    static uint32_t lastDecay = 0;
    if (millis() - lastDecay >= DECAY_INTERVAL_MS) {
        DecayAircraft();
        lastDecay = millis();
    }
}

// ── Called once per rotation: fetch data + update projected positions ──
bool AircraftManager::RefreshAircraft()
{
    // Fetch fresh data (no visual scrub during warmup)
    StorePrev(trackedAircraft, prevPositions);
    bool fetchOk = FetchLocal();
    if (!fetchOk) {
        return false;
    }
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
            // Redraw at lower brightness without hard background erase.
            // This prevents visible dark patches/squares around fading targets.
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
    const int r = 119;             // Scan/trail to near panel edge
    const int erase_r = 121;       // slight overdraw to kill edge residue

    // ── Advance scan angle by elapsed time (exact 360° per ROTATION_MS) ──
    constexpr float DEG1 = 0.0174533f; // visual beam width
    static uint32_t lastStepMs = 0;
    uint32_t nowMs = millis();
    uint32_t dtMs = (lastStepMs == 0) ? SCAN_INTERVAL : (nowMs - lastStepMs);
    lastStepMs = nowMs;
    bool stalled = false;
    if (dtMs > 250) {
        dtMs = SCAN_INTERVAL; // keep rotation stable after network stalls
        stalled = true;
    }

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
    if (eraseLag > (DEG1 * 8.0f)) eraseLag = DEG1 * 8.0f;
    if (eraseWidth > (DEG1 * 8.0f)) eraseWidth = DEG1 * 8.0f;
    if (stalled) {
        // Aggressive cleanup when a fetch stall occurred.
        eraseLag = DEG1 * 4.0f;
        eraseWidth = DEG1 * 6.0f;
    }

    float eraseC = headC, eraseS = headS;
    RotateAngle(eraseC, eraseS, eraseLag);
    float eraseNextC = eraseC, eraseNextS = eraseS;
    RotateAngle(eraseNextC, eraseNextS, eraseWidth);

    tft.fillTriangle(cx, cy,
        cx + (int)(eraseC * erase_r), cy - (int)(eraseS * erase_r),
        cx + (int)(eraseNextC * erase_r), cy - (int)(eraseNextS * erase_r),
        CLR_BG);

    // Visible phosphor trail behind beam head.
    DrawTrail(cx, cy, r, headC, headS);

    if (stalled) {
        // One extra scrub wedge to clear any sync-boundary residue.
        float scrubC = headC, scrubS = headS;
        RotateAngle(scrubC, scrubS, DEG1 * 4.0f);
        tft.fillTriangle(cx, cy,
            cx + (int)(headC * erase_r),  cy - (int)(headS * erase_r),
            cx + (int)(scrubC * erase_r), cy - (int)(scrubS * erase_r),
            CLR_BG);
    }

    // Bright scan line (1° wedge at leading edge)
    float headNextC = headC + headS * DEG1;
    float headNextS = headS - headC * DEG1;
    tft.fillTriangle(cx, cy,
        cx + (int)(headC * r), cy - (int)(headS * r),
        cx + (int)(headNextC * r), cy - (int)(headNextS * r),
        CLR_SCAN);

    // Bridge large frame steps near edge to remove outer-ring skipping.
    // Draw only in outer third (66%-100%) to keep center beam narrow.
    int bridgeSteps = (int)((delta / DEG1) * 1.5f); // Increase bridge steps for smoother outer ring
    if (bridgeSteps > 1) {
        if (bridgeSteps > 6) bridgeSteps = 6; // Increase max bridge steps
        const int bridgeInnerR = (r * 60) / 100; // Start bridge earlier (60% instead of 70%)
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

    // Subtle clutter/noise floor (feature #7)
    static uint32_t clutterSeed = 0xA53C9E21u;
    clutterSeed = (clutterSeed * 1664525u) + 1013904223u;
    for (int i = 0; i < 2; i++) {
        clutterSeed = (clutterSeed * 1664525u) + 1013904223u;
        int rr = (int)((clutterSeed >> 24) & 0x1F);      // 0..31 px
        clutterSeed = (clutterSeed * 1664525u) + 1013904223u;
        float ang = ((float)(clutterSeed & 0x3FF) / 1024.0f) * 6.2831853f;
        int px = cx + (int)(sinf(ang) * rr);
        int py = cy - (int)(cosf(ang) * rr);
        if (px > 1 && px < 238 && py > 1 && py < 238) {
            uint16_t c = ((clutterSeed & 0x800) ? CLR_CROSSHAIR : CLR_BG);
            tft.drawPixel(px, py, c);
        }
    }

    // ── Update projected positions each frame (dead-reckoning between fetches) ──
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

        if (lp.visible && (!on || abs(nx - lp.x) > 1 || abs(ny - lp.y) > 1)) {
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
            float beamGain = (dot - beamTouchCos) / (1.0f - beamTouchCos);
            if (beamGain < 0.0f) beamGain = 0.0f;
            if (beamGain > 1.0f) beamGain = 1.0f;

            float rangeNorm = sqrtf(d2) / (float)r;
            if (rangeNorm > 1.0f) rangeNorm = 1.0f;
            float rangeGain = 1.0f - (0.35f * rangeNorm);

            uint32_t sinceFetchMs2 = millis() - lastFetch;
            float ageSec = ComputeDataAgeSec(trackedAircraft.at(icao), sinceFetchMs2);
            float quality = ComputeQuality01(ageSec);

            float excite = (float)BRIGHTNESS_MAX * (0.60f + 0.40f * beamGain * rangeGain * (0.50f + 0.50f * quality));
            if (excite < (float)(BRIGHTNESS_MAX * 0.55f)) excite = (float)(BRIGHTNESS_MAX * 0.55f);
            if (excite > (float)BRIGHTNESS_MAX) excite = (float)BRIGHTNESS_MAX;
            lp.brightness = (uint8_t)(excite + 0.5f);
            DrawAircraftBlip(lp.x, lp.y, trackedAircraft.at(icao), lp.brightness);
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

// ── Draw phosphor trail: 32° behind scan line, 10 thin segments ──
void AircraftManager::DrawTrail(int cx, int cy, int r, float headC, float headS)
{
    // Trail starts 32° behind head - but we want it in front for correct fade direction
    // So we calculate the trail position in front of the head
    float trailC = headC * TRAIL_TAIL_COS - headS * TRAIL_TAIL_SIN;
    float trailS = headS * TRAIL_TAIL_COS + headC * TRAIL_TAIL_SIN;

    // ── Clear full wedge to black first ──
    tft.fillTriangle(cx, cy,
        cx + (int)(headC * r),   cy - (int)(headS * r),
        cx + (int)(trailC * r),   cy - (int)(trailS * r),
        CLR_BG);

    // Rotate each segment backward toward tail (opposite direction)
    constexpr float STEP = TRAIL_STEP_DEG * 0.0174533f;
    float segC = headC;
    float segS = headS;
    const float headStartC = headC;
    const float headStartS = headS;

    for (int i = 0; i < TRAIL_SEGMENTS; i++) {
        RotateAngle(segC, segS, -STEP); // Negative step to go backward
        uint16_t color = TRAIL_GRADIENT[TRAIL_SEGMENTS - 1 - i]; // Reverse gradient order
        tft.fillTriangle(cx, cy,
            cx + (int)(segC * r),   cy - (int)(segS * r),
            cx + (int)(headC * r),  cy - (int)(headS * r),
            color);
        headC = segC;
        headS = segS;
    }

    // Hard-black guard wedge over oldest trail region (kills tail-end green flash)
    float guardC = headStartC;
    float guardS = headStartS;
    constexpr float TAIL_GUARD_STEP = 0.0174533f * 10.0f; // 10°
    RotateAngle(guardC, guardS, -TAIL_GUARD_STEP); // Negative step
    tft.fillTriangle(cx, cy,
        cx + (int)(headStartC * (r + 2)), cy - (int)(headStartS * (r + 2)),
        cx + (int)(guardC * (r + 2)),     cy - (int)(guardS * (r + 2)),
        CLR_BG);

    // Force tail tip to black to prevent edge flash on low-res triangle joins
    tft.fillCircle(cx + (int)(headStartC * r), cy - (int)(headStartS * r), 5, CLR_BG);
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

    // Tick marks every 30° (12 ticks) EXCEPT at cardinal directions (0°, 90°, 180°, 270°)
    // Skip indices: 0, 3, 6, 9 (N, E, S, W)
    for (int i = 0; i < 12; i++) {
        // Skip cardinal directions
        if (i == 0 || i == 3 || i == 6 || i == 9) continue;
        
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

// ── Fade a base color toward black by brightness level (1-BRIGHTNESS_MAX) ──
uint16_t AircraftManager::FadeColor(uint16_t base, uint8_t level) const
{
    if (level <= 0) return CLR_BG;
    if (level >= BRIGHTNESS_MAX) return base;

    // Extract RGB565 components, scale down by level/BRIGHTNESS_MAX
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

    // Immediately restore static radar primitives under erased area
    // to avoid visible black patches around aircraft.
    const int cx = 120, cy = 120;
    tft.drawCircle(cx, cy, RING_OUTER_PX, CLR_RING);
    tft.drawCircle(cx, cy, RING_MID_PX,   CLR_RING);
    tft.drawCircle(cx, cy, RING_INNER_PX, CLR_RING);
    tft.drawFastHLine(1, cy, 238, CLR_CROSSHAIR);
    tft.drawFastVLine(cx, 1, 238, CLR_CROSSHAIR);
}

// ── Draw aircraft blip with PPI brightness scaling ──
void AircraftManager::DrawAircraftBlip(int x, int y, const SimpleAircraft& ac, uint8_t brightness) const
{
    AircraftType type = GetAircraftType(ac);
    TargetGlyph glyph = GetTargetGlyph(ac);
    uint16_t baseColor;
    uint16_t glowColor;
    switch (type) {
        case AircraftType::MILITARY:  
            baseColor = CLR_MILITARY;  
            glowColor = CLR_GLOW_MIL;
            break;
        case AircraftType::COMMERCIAL: 
            baseColor = CLR_COMMERIAL; 
            glowColor = CLR_GLOW_COMM;
            break;
        default:                      
            baseColor = CLR_UNKNOWN;   
            glowColor = CLR_UNKNOWN;
            break;
    }

    uint32_t sinceFetchMs = millis() - lastFetch;
    float ageSec = ComputeDataAgeSec(ac, sinceFetchMs);
    float quality = ComputeQuality01(ageSec);
    if (quality <= 0.03f) return;

    uint8_t effective = (uint8_t)((float)brightness * (0.25f + 0.75f * quality));
    if (effective < 1) effective = 1;
    if (effective > BRIGHTNESS_MAX) effective = BRIGHTNESS_MAX;

    uint16_t color = FadeColor(baseColor, effective);
    uint16_t glow = FadeColor(glowColor, effective);

    float hRad = ac.heading * 0.0174533f;

    // Keep geometry stable across fade steps; only color/intensity changes.
    // This prevents background patching artifacts during refresh/decay.
    const int headLen = 10;
    int tx = x + (int)(sin(hRad) * headLen);
    int ty = y - (int)(cos(hRad) * headLen);

    // Add phosphor glow effect - draw a larger, dimmer circle behind the blip
    if (effective > BRIGHTNESS_MAX * 0.3f) {
        uint16_t glowFaded = FadeColor(glowColor, (uint8_t)(effective * 0.4f));
        tft.fillCircle(x, y, 5, glowFaded);  // Outer glow
    }

    // Velocity vector: 15s look-ahead (fixed geometry, color scales with fade)
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

    // Class glyphs (feature #5) — constant footprint for clean fading
    switch (glyph) {
        case TargetGlyph::HELICOPTER: {
            const int rr = 3;
            tft.drawLine(x - rr, y, x + rr, y, color);
            tft.drawLine(x, y - rr, x, y + rr, color);
            tft.fillCircle(x, y, 1, color);
            break;
        }
        case TargetGlyph::HEAVY: {
            const int rr = 4;
            tft.fillCircle(x, y, rr, color);
            tft.drawCircle(x, y, rr + 1, FadeColor(baseColor, (uint8_t)(effective * 0.7f)));
            // Add glow for heavy aircraft
            if (effective > BRIGHTNESS_MAX * 0.5f) {
                uint16_t outerGlow = FadeColor(glowColor, (uint8_t)(effective * 0.3f));
                tft.drawCircle(x, y, rr + 2, outerGlow);
            }
            break;
        }
        case TargetGlyph::FIXED_WING:
        default: {
            if (displayTriangles) {
                tft.fillCircle(x, y, 3, color);
                tft.drawLine(x, y, tx, ty, color);
            } else {
                tft.fillCircle(x, y, 3, color);
                // Add subtle glow around regular aircraft
                if (effective > BRIGHTNESS_MAX * 0.4f) {
                    uint16_t softGlow = FadeColor(glowColor, (uint8_t)(effective * 0.2f));
                    tft.drawCircle(x, y, 4, softGlow);
                }
            }
            break;
        }
    }

    // Heading cue for all glyphs except fixed-wing triangle path where it is already drawn.
    if (!(glyph == TargetGlyph::FIXED_WING && displayTriangles)) {
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

    // Keep nearest in-range aircraft so low-priority targets (e.g. helicopters)
    // are not dropped just because they appear later in the JSON list.
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

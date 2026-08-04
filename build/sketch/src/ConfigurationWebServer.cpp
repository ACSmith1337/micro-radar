#line 1 "/home/hermes/micro-radar/src/ConfigurationWebServer.cpp"
#include "ConfigurationWebServer.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <ESPmDNS.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266mDNS.h>
#endif

// Forward declaration for HandleSync
#include "AircraftManager.h"

// Global log buffer instance
LogBuffer g_logBuffer;

// Log helper — writes to Serial AND the web-viewable buffer
void GridLog(const char* msg) {
    Serial.println(msg);
    g_logBuffer.log(msg);
}

// HTML stored in flash
static const char CONFIG_HTML[] PROGMEM = R"HTML(
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Micro Radar</title>
        <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4.3.0"></script>
    </head>
    <body class="font-mono bg-gray-900 text-green-500 min-h-screen p-4 sm:p-0 text-md sm:text-sm">
        <fieldset class="border border-green-500 p-5 w-full max-w-2xl mx-auto sm:m-10">
            <legend class="px-2">Configure Micro Radar</legend>

            <form id="cfg" action="/save" method="POST" class="flex flex-col gap-4 sm:gap-2">

                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5">
                    <label class="flex flex-col sm:flex-row gap-2 flex-1">
                        <span>Latitude:</span>
                        <input
                            name="latitude"
                            type="text"
                            inputmode="decimal"
                            pattern="-?[0-9]+\.?[0-9]*"
                            value='%LATITUDE%'
                            class="border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>

                    <label class="flex flex-col sm:flex-row gap-2 flex-1">
                        <span>Longitude:</span>
                        <input
                            name="longitude"
                            type="text"
                            inputmode="decimal"
                            pattern="-?[0-9]+\.?[0-9]*"
                            value='%LONGITUDE%'
                            class="border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                </div>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>Max range (outer ring, NM):</span>
                    <input
                        name="maxrange"
                        type="text"
                        inputmode="decimal"
                        pattern="[0-9]+\.?[0-9]*"
                        value='%MAXRANGE%'
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    <small id="ring-info" style="color:#666; margin-left:4px;">Mid ring ~66%, inner ring ~33% of max range</small>
                </label>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>Data Source:</span>
                    <select
                        name="datasource"
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                        <option value="local"%DS_LOCAL%>Local readsb/dump1090 (real-time)</option>
                        <option value="adsblol"%DS_ADSBLOL%>ADSB.lol (global API)</option>
                    </select>
                </label>

                <fieldset id="local-fields" class="border border-green-700 p-3 flex flex-col gap-2">
                    <legend class="px-2 text-xs">Local ADS-B Settings</legend>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>readsb/dump1090 Host:</span>
                        <input
                            name="readsbhost"
                            type="text"
                            placeholder="e.g. 192.168.1.100"
                            value='%READSBHOST%'
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Port:</span>
                        <input
                            name="readsbport"
                            type="text"
                            inputmode="numeric"
                            pattern="[0-9]+"
                            value='%READSBPORT%'
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Fetch Interval (sec):</span>
                        <input
                            name="fetchinterval"
                            type="text"
                            inputmode="decimal"
                            pattern="[0-9]+\.?[0-9]*"
                            value='%FETCHINTERVAL%'
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>JSON Path:</span>
                        <input
                            name="readsbpath"
                            type="text"
                            placeholder="/data/aircraft.json"
                            value='%READSBPATH%'
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                </fieldset>

                <fieldset class="border border-green-700 p-3 flex flex-col gap-2">
                    <legend class="px-2 text-xs">Display Settings</legend>
                    <div class="flex flex-col sm:flex-row gap-4 sm:gap-6">
                        <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                            <span>Radar sweep:</span>
                            <input
                                name="scanline"
                                type="checkbox"
                                %SCANLINE%
                                class="px-3 sm:px-1 accent-green-500">
                        </label>
                        <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                            <span>Aircraft Info:</span>
                            <input
                                name="infotext"
                                type="checkbox"
                                %INFOTEXT%
                                class="px-3 sm:px-1 accent-green-500">
                        </label>
                        <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                            <span>Directional Aircraft:</span>
                            <input
                                name="triangle"
                                type="checkbox"
                                %TRIANGLE%
                                class="px-3 sm:px-1 accent-green-500">
                        </label>
                    </div>
                    <div class="flex flex-col sm:flex-row gap-4 sm:gap-6">
                        <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                            <span>Aircraft Trails:</span>
                            <input
                                name="trails"
                                type="checkbox"
                                %TRAILS%
                                class="px-3 sm:px-1 accent-green-500">
                            <small style="color:#666;">Dotted path behind aircraft</small>
                        </label>
                        <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                            <span>Squawk Alerts:</span>
                            <input
                                name="squawkalert"
                                type="checkbox"
                                %SQUAWKALERT%
                                class="px-3 sm:px-1 accent-green-500">
                            <small style="color:#666;">Flash emergency codes</small>
                        </label>
                    </div>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Phosphor Colour:</span>
                        <select
                            name="phosphor"
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                            <option value="green"%PHOSPHOR_GREEN%>Green (P1)</option>
                            <option value="amber"%PHOSPHOR_AMBER%>Amber (P4)</option>
                        </select>
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Scan Mode:</span>
                        <select
                            name="scanmode"
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                            <option value="angular"%SCANMODE_ANGULAR%>Angular Sweep</option>
                            <option value="radial"%SCANMODE_RADIAL%>Radial Ping</option>
                        </select>
                    </label>
                </fieldset>

                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5">
                    <input
                        type="submit"
                        value="Save"
                        class="bg-green-500 text-black mt-4 px-4 py-3 text-lg sm:text-base sm:px-2 sm:py-0 self-start cursor-pointer">

                    <button
                        type="button"
                        id="sync-btn"
                        onclick="fetch('/sync').then(r=>r.text()).then(t=>{document.getElementById('sync-status').textContent=t;setTimeout(()=>document.getElementById('sync-status').textContent='',3000);})"
                        class="bg-gray-700 text-green-400 border border-green-500 mt-4 px-4 py-3 text-lg sm:text-base sm:px-2 sm:py-0 self-start cursor-pointer">
                        Sync Now
                    </button>

                        <div id="result" class="mt-4 px-1 sm:px-10"></div>
                        <div id="sync-status" class="mt-4 px-1 sm:px-10 text-sm" style="color:#aaa;"></div>
                    </div>

                    <a href="/logs" class="block text-center mt-4 px-4 py-2 border border-green-500 hover:bg-green-500 hover:bg-opacity-10 transition-colors duration-200 text-sm">
                        View Logs
                    </a>
                </fieldset>

        <fieldset class="border border-green-700 p-5 w-full max-w-2xl mx-auto sm:m-10 mt-4">
            <legend class="px-2 text-green-400">Display Legend</legend>
            <div class="flex flex-col gap-3 text-sm">
                <div class="flex items-center gap-3">
                    <span style="color:#00FF00;">&gt;</span>
                    <span>Fixed-wing aircraft (green/amber)</span>
                </div>
                <div class="flex items-center gap-3">
                    <span style="color:#00FF00;">&gt;</span>
                    <span>Heavy aircraft - larger triangle</span>
                </div>
                <div class="flex items-center gap-3">
                    <span style="color:#00FF00;">X</span>
                    <span>Helicopter - circle with X</span>
                </div>
                <div class="flex items-center gap-3">
                    <span style="color:#FF8800;">&gt;</span>
                    <span>Military - orange (squawk 4000-4999, 7000+)</span>
                </div>
                <hr style="border-color:#333;">
                <div class="text-xs" style="color:#888;">
                    <p class="mb-2"><strong style="color:#aaa;">How aircraft fade:</strong></p>
                    <p class="mb-1">- Aircraft appear at full brightness when the radar sweep passes over them</p>
                    <p class="mb-1">- They then gradually dim over ~6 seconds, mimicking CRT phosphor decay</p>
                    <p class="mb-1">- Fresh data (recent position updates) produces brighter blips</p>
                    <p class="mb-1">- Stale data fades faster - aircraft with old positions disappear quicker</p>
                    <p class="mb-1">- When the sweep line touches a target again, it re-illuminates to full brightness</p>
                </div>
                <hr style="border-color:#333;">
                <div class="text-xs" style="color:#888;">
                    <p class="mb-2"><strong style="color:#aaa;">Squawk alerts:</strong></p>
                    <p class="mb-1"><span style="color:#FF4444;">7500</span> - Hijack / unlawful interference</p>
                    <p class="mb-1"><span style="color:#FF4444;">7600</span> - Radio failure</p>
                    <p class="mb-1"><span style="color:#FF4444;">7700</span> - General emergency</p>
                    <p class="mb-1"><span style="color:#FF4444;">1200</span> - VFR / general aviation (US)</p>
                </div>
            </div>
        </fieldset>

        <script>
            document.getElementById('cfg').addEventListener('submit', function(e) {
                e.preventDefault();
                var params = new URLSearchParams(new FormData(this));
                fetch(this.action, {
                    method: 'POST',
                    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                    body: params.toString()
                })
                    .then(r => r.text())
                    .then(html => document.getElementById('result').innerHTML = html);
            });

            const ds = document.querySelector('select[name="datasource"]');
            const maxRangeInput = document.querySelector('input[name="maxrange"]');
            const ringInfo = document.getElementById('ring-info');
            const localFields = document.getElementById('local-fields');

            function fmtNm(v) {
                if (!isFinite(v) || v <= 0) return '--';
                if (v < 10) return v.toFixed(1);
                return Math.round(v).toString();
            }

            function updateRingInfo() {
                const nmOuter = parseFloat(maxRangeInput.value);
                if (!isFinite(nmOuter) || nmOuter <= 0) {
                    ringInfo.innerHTML = 'Outer = max range. Mid ~66%. Inner ~33%';
                    return;
                }

                const nmMid = nmOuter * (2.0 / 3.0);
                const nmInner = nmOuter * (1.0 / 3.0);

                ringInfo.innerHTML =
                    'Outer ' + fmtNm(nmOuter) + ' NM' +
                    ' | Mid ' + fmtNm(nmMid) + ' NM' +
                    ' | Inner ' + fmtNm(nmInner) + ' NM';
            }

            function toggleSections() {
                if (ds.value === 'local') {
                    localFields.style.display = 'flex';
                } else {
                    localFields.style.display = 'none';
                }
            }

            ds.addEventListener('change', toggleSections);
            toggleSections();
            maxRangeInput.addEventListener('input', updateRingInfo);
            updateRingInfo();
        </script>
    </body>
</html>
)HTML";

#if defined(ARDUINO_ARCH_ESP32)

void ConfigurationWebServer::Initialise() {
    if (!MDNS.begin("microradar")) {
        Serial.println("[WARN] Failed to start mDNS. Continuing without mDNS...");
    }

    server->on("/", HTTP_GET, [&](AsyncWebServerRequest* request) {
        Serial.println("[GET] Handling request to config web server...");

        prefs.begin("config", true);
        const String latitude = prefs.getString("latitude", "");
        const String longitude = prefs.getString("longitude", "");
        const String radiusDeg = prefs.getString("radius", "1.0");
        String maxRangeNm = prefs.getString("maxrange", "");
        if (maxRangeNm.isEmpty()) {
            maxRangeNm = String(radiusDeg.toFloat() * 60.0f, 1);
        }
        const String scanlineEnabled = prefs.getString("scanline", "true");
        const String infoTextEnabled = prefs.getString("infotext", "true");
        const String triangleEnabled = prefs.getString("triangle", "true");
        const String trailsEnabled = prefs.getString("trails", "false");
        const String squawkAlertEnabled = prefs.getString("squawkalert", "false");
        const String phosphor = prefs.getString("phosphor", "green");
        const String dataSource = prefs.getString("datasource", "local");
        const String readsbHost = prefs.getString("readsbhost", "");
        const String readsbPort = prefs.getString("readsbport", "8080");
        const String fetchInterval = prefs.getString("fetchinterval", "3");
        const String scanMode = prefs.getString("scanmode", "angular");
        prefs.end();

        const String dsLocal = dataSource == "local" ? "selected" : "";
        const String dsAdsblol = dataSource == "adsblol" ? "selected" : "";
        const String phosphorGreen = phosphor == "green" ? "selected" : "";
        const String phosphorAmber = phosphor == "amber" ? "selected" : "";
        const String scanModeAngular = scanMode == "angular" ? "selected" : "";
        const String scanModeRadial = scanMode == "radial" ? "selected" : "";

        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [latitude, longitude, maxRangeNm, scanlineEnabled, infoTextEnabled, triangleEnabled, trailsEnabled, squawkAlertEnabled, phosphorGreen, phosphorAmber, dsLocal, dsAdsblol, readsbHost, readsbPort, fetchInterval, scanModeAngular, scanModeRadial]
            (const String& var) -> String {
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "MAXRANGE")       return maxRangeNm;
                if (var == "SCANLINE")       return scanlineEnabled == "true" ? "checked" : "";
                if (var == "INFOTEXT")       return infoTextEnabled == "true" ? "checked" : "";
                if (var == "TRIANGLE")       return triangleEnabled == "true" ? "checked" : "";
                if (var == "TRAILS")         return trailsEnabled == "true" ? "checked" : "";
                if (var == "SQUAWKALERT")    return squawkAlertEnabled == "true" ? "checked" : "";
                if (var == "PHOSPHOR_GREEN") return phosphorGreen;
                if (var == "PHOSPHOR_AMBER") return phosphorAmber;
                if (var == "DS_LOCAL")       return dsLocal;
                if (var == "DS_ADSBLOL")     return dsAdsblol;
                if (var == "READSBHOST")     return readsbHost;
                if (var == "READSBPORT")     return readsbPort;
                if (var == "FETCHINTERVAL")  return fetchInterval;
                if (var == "SCANMODE_ANGULAR") return scanModeAngular;
                if (var == "SCANMODE_RADIAL")  return scanModeRadial;
                return "";
            }
        );
        request->send(response);
    });

    server->on("/save", HTTP_POST, [&](AsyncWebServerRequest* request) {
        Serial.println("[POST] Handling form submission to config web server...");

        auto TrySaveParam = [request, this](const char* paramName) {
            const auto* param = request->getParam(paramName, true);
            if (param == nullptr)
                return false;

            prefs.putString(paramName, param->value());
            return true;
        };

        prefs.begin("config", false);

        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("maxrange");
        TrySaveParam("datasource");
        TrySaveParam("readsbhost");
        TrySaveParam("readsbport");
        TrySaveParam("readsbpath");
        TrySaveParam("fetchinterval");
        TrySaveParam("phosphor");
        TrySaveParam("scanmode");

        const auto* maxRangeParam = request->getParam("maxrange", true);
        if (maxRangeParam != nullptr) {
            float maxRangeNm = maxRangeParam->value().toFloat();
            if (maxRangeNm > 0.0f) {
                prefs.putString("radius", String(maxRangeNm / 60.0f, 4));
            }
        }

        prefs.putString("scanline", request->hasParam("scanline", true) ? "true" : "false");
        prefs.putString("triangle", request->hasParam("triangle", true) ? "true" : "false");
        prefs.putString("infotext", request->hasParam("infotext", true) ? "true" : "false");
        prefs.putString("trails", request->hasParam("trails", true) ? "true" : "false");
        prefs.putString("squawkalert", request->hasParam("squawkalert", true) ? "true" : "false");
        prefs.end();

        request->send(200, "text/html", "Saved - restarting device...");
        ESP.restart();
    });

    server->on("/sync", HTTP_GET, [&](AsyncWebServerRequest* request) {
        Serial.println("[GET] Sync now requested");
        AircraftManager::RequestForceSync();
        request->send(200, "text/plain", "Sync triggered");
    });

    server->on("/logs", HTTP_GET, [&](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", g_logBuffer.dump());
    });

    server->begin();
}

#elif defined(ARDUINO_ARCH_ESP8266)

static inline void substitutePlaceholders(String& templateStr, const String& key, const String& value)
{
    templateStr.replace(key, value);
}

void ConfigurationWebServer::HandleRoot() {
    Serial.println("[GET] Handling request to config web server...");

    // Read config values
    prefs.begin("config", true);
    String latitude = prefs.getString("latitude", "");
    String longitude = prefs.getString("longitude", "");
    String radiusDeg = prefs.getString("radius", "1.0");
    String maxRangeNm = prefs.getString("maxrange", "");
    if (maxRangeNm.isEmpty()) {
        maxRangeNm = String(radiusDeg.toFloat() * 60.0f, 1);
    }
    String scanlineEnabled = prefs.getString("scanline", "true");
    String infoTextEnabled = prefs.getString("infotext", "true");
    String triangleEnabled = prefs.getString("triangle", "true");
    String trailsEnabled = prefs.getString("trails", "false");
    String squawkAlertEnabled = prefs.getString("squawkalert", "false");
    String phosphor = prefs.getString("phosphor", "green");
    String dataSource = prefs.getString("datasource", "local");
    String readsbHost = prefs.getString("readsbhost", "");
    String readsbPort = prefs.getString("readsbport", "8080");
    String readsbPath = prefs.getString("readsbpath", "/data/aircraft.json");
    String fetchInterval = prefs.getString("fetchinterval", "3");
    String scanMode = prefs.getString("scanmode", "angular");
    prefs.end();

    String html;
    html.reserve(sizeof(CONFIG_HTML));
    for (size_t i = 0; i < sizeof(CONFIG_HTML) - 1; i++) {
        html += (char)pgm_read_byte(CONFIG_HTML + i);
    }

    substitutePlaceholders(html, "%LATITUDE%", latitude);
    substitutePlaceholders(html, "%LONGITUDE%", longitude);
    substitutePlaceholders(html, "%MAXRANGE%", maxRangeNm);
    substitutePlaceholders(html, "%SCANLINE%", scanlineEnabled == "true" ? "checked" : "");
    substitutePlaceholders(html, "%INFOTEXT%", infoTextEnabled == "true" ? "checked" : "");
    substitutePlaceholders(html, "%TRIANGLE%", triangleEnabled == "true" ? "checked" : "");
    substitutePlaceholders(html, "%TRAILS%", trailsEnabled == "true" ? "checked" : "");
    substitutePlaceholders(html, "%SQUAWKALERT%", squawkAlertEnabled == "true" ? "checked" : "");
    substitutePlaceholders(html, "%PHOSPHOR_GREEN%", phosphor == "green" ? "selected" : "");
    substitutePlaceholders(html, "%PHOSPHOR_AMBER%", phosphor == "amber" ? "selected" : "");
    substitutePlaceholders(html, "%DS_LOCAL%", dataSource == "local" ? "selected" : "");
    substitutePlaceholders(html, "%DS_ADSBLOL%", dataSource == "adsblol" ? "selected" : "");
    substitutePlaceholders(html, "%READSBHOST%", readsbHost);
    substitutePlaceholders(html, "%READSBPORT%", readsbPort);
    substitutePlaceholders(html, "%READSBPATH%", readsbPath);
    substitutePlaceholders(html, "%FETCHINTERVAL%", fetchInterval);
    substitutePlaceholders(html, "%SCANMODE_ANGULAR%", scanMode == "angular" ? "selected" : "");
    substitutePlaceholders(html, "%SCANMODE_RADIAL%", scanMode == "radial" ? "selected" : "");

    server.send(200, "text/html", html);
    html = String();  // Free heap immediately
}

void ConfigurationWebServer::HandleSave() {
    Serial.println("[POST] Handling form submission to config web server...");

    Serial.printf("[POST] Total args: %d\n", server.args());
    for (uint8_t i = 0; i < server.args(); i++) {
        Serial.printf("[POST]   %s = %s\n", server.argName(i).c_str(), server.arg(i).c_str());
    }

    prefs.begin("config", false);

    auto TrySaveParam = [&](const char* paramName) {
        if (server.hasArg(paramName)) {
            String val = server.arg(paramName);
            Serial.printf("[POST] Saving %s = '%s'\n", paramName, val.c_str());
            prefs.putString(paramName, val);
        } else {
            Serial.printf("[POST] MISSING param: %s\n", paramName);
        }
    };

    TrySaveParam("latitude");
    TrySaveParam("longitude");
    TrySaveParam("maxrange");
    TrySaveParam("datasource");
    TrySaveParam("readsbhost");
    TrySaveParam("readsbport");
    TrySaveParam("readsbpath");
    TrySaveParam("fetchinterval");
    TrySaveParam("phosphor");
    TrySaveParam("scanmode");

    if (server.hasArg("maxrange")) {
        float maxRangeNm = server.arg("maxrange").toFloat();
        if (maxRangeNm > 0.0f) {
            prefs.putString("radius", String(maxRangeNm / 60.0f, 4));
        }
    }

    prefs.putString("scanline", server.hasArg("scanline") ? "true" : "false");
    prefs.putString("triangle", server.hasArg("triangle") ? "true" : "false");
    prefs.putString("infotext", server.hasArg("infotext") ? "true" : "false");
    prefs.putString("trails", server.hasArg("trails") ? "true" : "false");
    prefs.putString("squawkalert", server.hasArg("squawkalert") ? "true" : "false");
    prefs.end();

    Serial.println("[POST] EEPROM committed. Verifying write...");
    prefs.begin("config", true);
    String verifyLat = prefs.getString("latitude", "EMPTY");
    String verifyLon = prefs.getString("longitude", "EMPTY");
    prefs.end();
    Serial.printf("[POST] Verify: lat='%s', lon='%s'\n", verifyLat.c_str(), verifyLon.c_str());

    server.send(200, "text/html", "Saved — changes applied live.");
    reloadRequested = true;
}

void ConfigurationWebServer::RequestReload() {
    reloadRequested = true;
}

void ConfigurationWebServer::Initialise() {
    if (!MDNS.begin("microradar")) {
        Serial.println("[WARN] Failed to start mDNS. Continuing without mDNS...");
    }

    server.on("/", std::bind(&ConfigurationWebServer::HandleRoot, this));
    server.on("/save", std::bind(&ConfigurationWebServer::HandleSave, this));
    server.on("/sync", std::bind(&ConfigurationWebServer::HandleSync, this));
    server.on("/logs", std::bind(&ConfigurationWebServer::HandleLogs, this));
    server.on("/status", std::bind(&ConfigurationWebServer::HandleStatus, this));

    server.begin();
    Serial.println("[INFO] Config server listening on port 80");
}

void ConfigurationWebServer::HandleClient() {
    server.handleClient();
}

void ConfigurationWebServer::HandleSync() {
    Serial.println("[GET] Sync now requested");
    AircraftManager::RequestForceSync();
    server.send(200, "text/plain", "Sync triggered");
}

void ConfigurationWebServer::HandleLogs() {
    server.send(200, "text/plain", g_logBuffer.dump());
}

void ConfigurationWebServer::HandleStatus() {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "Free Heap: %d bytes\n"
        "Max Free Block: %d bytes\n"
        "Sketch Size: %d / %d bytes\n"
        "Uptime: %lu sec\n",
        ESP.getFreeHeap(),
        ESP.getMaxFreeBlockSize(),
        ESP.getSketchSize(),
        ESP.getFreeSketchSpace(),
        millis() / 1000);
    server.send(200, "text/plain", buf);
}

#endif

const String ConfigurationWebServer::GetStoredString(const char* key) {
    prefs.begin("config", true);
    const String value = prefs.getString(key, "");
    prefs.end();
    return value;
}

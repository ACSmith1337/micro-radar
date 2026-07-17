#include "ConfigurationWebServer.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <ESPmDNS.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266mDNS.h>
#endif

// HTML stored in flash
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
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
                    <span>Radius (in °):</span>
                    <input
                        name="radius"
                        type="text"
                        inputmode="decimal"
                        pattern="[0-9]+\.?[0-9]*"
                        value='%RADIUS%'
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                </label>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>Data Source:</span>
                    <select
                        name="datasource"
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                        <option value="opensky"%DS_OPENSKY%>OpenSky Network (rate-limited)</option>
                        <option value="local"%DS_LOCAL%>Local readsb/dump1090 (real-time)</option>
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

                <fieldset id="opensky-fields" class="border border-green-700 p-3 flex flex-col gap-2">
                    <legend class="px-2 text-xs">OpenSky Network Settings</legend>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>OpenSkyAPI Client ID:</span>
                        <input
                            name="opensky-id"
                            value='%OPENSKY_ID%'
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>OpenSkyAPI Client Secret:</span>
                        <input
                            name="opensky-secret"
                            value='%OPENSKY_SECRET%'
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                </fieldset>

                <div class="flex flex-col sm:flex-row gap-4 sm:justify-between">
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

                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5">
                    <input
                        type="submit"
                        value="Save"
                        class="bg-green-500 text-black mt-4 px-4 py-3 text-lg sm:text-base sm:px-2 sm:py-0 self-start cursor-pointer">

                        <div id="result" class="mt-4 px-1 sm:px-10"></div>
                </div>
            </form>
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
            const localFields = document.getElementById('local-fields');
            const openskyFields = document.getElementById('opensky-fields');

            function toggleSections() {
                if (ds.value === 'local') {
                    localFields.style.display = 'flex';
                    openskyFields.style.display = 'none';
                } else {
                    localFields.style.display = 'none';
                    openskyFields.style.display = 'flex';
                }
            }

            ds.addEventListener('change', toggleSections);
            toggleSections();
        </script>
    </body>
</html>
)";

#if defined(ARDUINO_ARCH_ESP32)

// ──────────────────────────────────────────────
// ESP32: AsyncWebServer (template processor)
// ──────────────────────────────────────────────

void ConfigurationWebServer::Initialise() {
    if (!MDNS.begin("microradar")) {
        Serial.println("[WARN] Failed to start mDNS. Continuing without mDNS...");
    }

    server->on("/", HTTP_GET, [&](AsyncWebServerRequest* request) {
        Serial.println("[GET] Handling request to config web server...");

        prefs.begin("config", true);
        const String latitude = prefs.getString("latitude", "");
        const String longitude = prefs.getString("longitude", "");
        const String radius = prefs.getString("radius", "1.0");
        const String openskyClientId = prefs.getString("opensky-id", "");
        String openskySecret = prefs.getString("opensky-secret", "");
        const String scanlineEnabled = prefs.getString("scanline", "true");
        const String infoTextEnabled = prefs.getString("infotext", "true");
        const String triangleEnabled = prefs.getString("triangle", "true");
        const String dataSource = prefs.getString("datasource", "opensky");
        const String readsbHost = prefs.getString("readsbhost", "");
        const String readsbPort = prefs.getString("readsbport", "8080");
        const String fetchInterval = prefs.getString("fetchinterval", "3");
        prefs.end();

        std::fill(openskySecret.begin(), openskySecret.end(), '*');

        const String dsOpenSky = dataSource == "opensky" ? "selected" : "";
        const String dsLocal = dataSource == "local" ? "selected" : "";

        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [latitude, longitude, radius, openskyClientId, openskySecret, scanlineEnabled, infoTextEnabled, triangleEnabled, dsOpenSky, dsLocal, readsbHost, readsbPort, fetchInterval]
            (const String& var) -> String {
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "RADIUS")         return radius;
                if (var == "OPENSKY_ID")     return openskyClientId;
                if (var == "OPENSKY_SECRET") return openskySecret;
                if (var == "SCANLINE")       return scanlineEnabled == "true" ? "checked" : "";
                if (var == "INFOTEXT")       return infoTextEnabled == "true" ? "checked" : "";
                if (var == "TRIANGLE")       return triangleEnabled == "true" ? "checked" : "";
                if (var == "DS_OPENSKY")     return dsOpenSky;
                if (var == "DS_LOCAL")       return dsLocal;
                if (var == "READSBHOST")     return readsbHost;
                if (var == "READSBPORT")     return readsbPort;
                if (var == "FETCHINTERVAL")  return fetchInterval;
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
        TrySaveParam("radius");
        TrySaveParam("datasource");
        TrySaveParam("opensky-id");
        TrySaveParam("readsbhost");
        TrySaveParam("readsbport");
        TrySaveParam("readsbpath");
        TrySaveParam("fetchinterval");

        const auto* param = request->getParam("opensky-secret", true);
        if (param != nullptr) {
            const String& secret = param->value();
            if (secret.indexOf('*') == -1) {
                prefs.putString("opensky-secret", secret);
            }
        }

        prefs.putString("scanline", request->hasParam("scanline", true) ? "true" : "false");
        prefs.putString("triangle", request->hasParam("triangle", true) ? "true" : "false");
        prefs.putString("infotext", request->hasParam("infotext", true) ? "true" : "false");
        prefs.end();

        request->send(200, "text/html", "Saved - restarting device...");
        ESP.restart();
    });

    server->begin();
}

#elif defined(ARDUINO_ARCH_ESP8266)

// ──────────────────────────────────────────────
// ESP8266: ESP8266WebServer (synchronous, stream-based)
// ──────────────────────────────────────────────

static String substitutePlaceholders(String templateStr, const String& key, const String& value)
{
    templateStr.replace(key, value);
    return templateStr;
}

void ConfigurationWebServer::HandleRoot() {
    Serial.println("[GET] Handling request to config web server...");

    prefs.begin("config", true);
    String latitude = prefs.getString("latitude", "");
    String longitude = prefs.getString("longitude", "");
    String radius = prefs.getString("radius", "1.0");
    String openskyClientId = prefs.getString("opensky-id", "");
    String openskySecret = prefs.getString("opensky-secret", "");
    String scanlineEnabled = prefs.getString("scanline", "true");
    String infoTextEnabled = prefs.getString("infotext", "true");
    String triangleEnabled = prefs.getString("triangle", "true");
    String dataSource = prefs.getString("datasource", "opensky");
    String readsbHost = prefs.getString("readsbhost", "");
    String readsbPort = prefs.getString("readsbport", "8080");
    String readsbPath = prefs.getString("readsbpath", "/data/aircraft.json");
    String fetchInterval = prefs.getString("fetchinterval", "3");
    prefs.end();

    std::fill(openskySecret.begin(), openskySecret.end(), '*');

    String html;

#if defined(__PROGMEM_TYPES_DEFINED)
    html.reserve(sizeof(CONFIG_HTML));
    for (size_t i = 0; i < sizeof(CONFIG_HTML) - 1; i++) {
        html += (char)pgm_read_byte(CONFIG_HTML + i);
    }
#else
    html = String(CONFIG_HTML);
#endif

    html = substitutePlaceholders(html, "%LATITUDE%", latitude);
    html = substitutePlaceholders(html, "%LONGITUDE%", longitude);
    html = substitutePlaceholders(html, "%RADIUS%", radius);
    html = substitutePlaceholders(html, "%OPENSKY_ID%", openskyClientId);
    html = substitutePlaceholders(html, "%OPENSKY_SECRET%", openskySecret);
    html = substitutePlaceholders(html, "%SCANLINE%", scanlineEnabled == "true" ? "checked" : "");
    html = substitutePlaceholders(html, "%INFOTEXT%", infoTextEnabled == "true" ? "checked" : "");
    html = substitutePlaceholders(html, "%TRIANGLE%", triangleEnabled == "true" ? "checked" : "");
    html = substitutePlaceholders(html, "%DS_OPENSKY%", dataSource == "opensky" ? "selected" : "");
    html = substitutePlaceholders(html, "%DS_LOCAL%", dataSource == "local" ? "selected" : "");
    html = substitutePlaceholders(html, "%READSBHOST%", readsbHost);
    html = substitutePlaceholders(html, "%READSBPORT%", readsbPort);
    html = substitutePlaceholders(html, "%READSBPATH%", readsbPath);
    html = substitutePlaceholders(html, "%FETCHINTERVAL%", fetchInterval);

    server.send(200, "text/html", html);
}

void ConfigurationWebServer::HandleSave() {
    Serial.println("[POST] Handling form submission to config web server...");

    // Debug: dump all received args
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
    TrySaveParam("radius");
    TrySaveParam("datasource");
    TrySaveParam("opensky-id");
    TrySaveParam("readsbhost");
    TrySaveParam("readsbport");
    TrySaveParam("readsbpath");
    TrySaveParam("fetchinterval");

    if (server.hasArg("opensky-secret")) {
        const String& secret = server.arg("opensky-secret");
        if (secret.indexOf('*') == -1) {
            prefs.putString("opensky-secret", secret);
        }
    }

    prefs.putString("scanline", server.hasArg("scanline") ? "true" : "false");
    prefs.putString("triangle", server.hasArg("triangle") ? "true" : "false");
    prefs.putString("infotext", server.hasArg("infotext") ? "true" : "false");
    prefs.end();

    Serial.println("[POST] EEPROM committed. Verifying write...");
    // Quick verification read
    prefs.begin("config", true);
    String verifyLat = prefs.getString("latitude", "EMPTY");
    String verifyLon = prefs.getString("longitude", "EMPTY");
    prefs.end();
    Serial.printf("[POST] Verify: lat='%s', lon='%s'\n", verifyLat.c_str(), verifyLon.c_str());

    server.send(200, "text/html", "Saved - restarting device...");
    delay(1000); // Increase delay to ensure EEPROM flash write completes
    ESP.restart();
}

void ConfigurationWebServer::Initialise() {
    if (!MDNS.begin("microradar")) {
        Serial.println("[WARN] Failed to start mDNS. Continuing without mDNS...");
    }

    server.on("/", std::bind(&ConfigurationWebServer::HandleRoot, this));
    server.on("/save", std::bind(&ConfigurationWebServer::HandleSave, this));

    server.begin();
    Serial.println("[INFO] Config server listening on port 80");
}

void ConfigurationWebServer::HandleClient() {
    server.handleClient();
}

#endif

const String ConfigurationWebServer::GetStoredString(const char* key) {
    prefs.begin("config", true);
    const String value = prefs.getString(key, "");
    prefs.end();
    return value;
}

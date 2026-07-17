#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

#include "LGFX.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "AircraftManager.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"

// Optional hard-coded Wi-Fi credentials. Leave both blank to skip pre-baking them and use the setup hotspot instead.
const char* preconfiguredWifiSsid = "";
const char* preconfiguredWifiPassword = "";

constexpr int SCREEN_SIZE = 240;

LGFX tft;
WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;

AircraftManager aircraftManager(configServer, http, tft);

// Round panel corner masking — draws black circles to cover corners
// that extend beyond the physical round bezel.
void DrawRoundMask(LGFX& panel)
{
    constexpr int R = SCREEN_SIZE / 2;
    panel.fillCircle(0, 0, R, lgfx::color888(0, 0, 0));
    panel.fillCircle(SCREEN_SIZE - 1, 0, R, lgfx::color888(0, 0, 0));
    panel.fillCircle(0, SCREEN_SIZE - 1, R, lgfx::color888(0, 0, 0));
    panel.fillCircle(SCREEN_SIZE - 1, SCREEN_SIZE - 1, R, lgfx::color888(0, 0, 0));
}

void setup()
{
    Serial.begin(115200);

    // initialise display
    tft.init();
    tft.invertDisplay(true);

#if defined(ARDUINO_ARCH_ESP32)
    pinMode(3, OUTPUT);
    digitalWrite(3, HIGH);
#endif

#if defined(ARDUINO_ARCH_ESP8266)
    // ESP8266 D1 Mini: pin D1 (GPIO5) = backlight
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
#endif

    // establish WiFi connection
    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.setTextColor(lgfx::color888(0, 255, 0));
    tft.drawCentreString("Connecting to WiFi...", SCREEN_SIZE / 2, SCREEN_SIZE / 2);

    WiFiManagerHelpers::ConfigureWiFiManager(wm, tft);

    if (strlen(preconfiguredWifiSsid) > 0) {
        WiFi.begin(preconfiguredWifiSsid, preconfiguredWifiPassword);
        WiFi.waitForConnectResult();
    }

    wm.autoConnect(WiFiManagerHelpers::WiFiManagerName);

    // begin background server for configuration
    configServer.Initialise();

    // initialise aircraft manager (draws radar grid once)
    aircraftManager.Initialise();

    // Mask round panel corners once
    DrawRoundMask(tft);
}

void loop()
{
    // Update aircraft data + draw incremental updates (scanline + aircraft)
    aircraftManager.Update();

    // Poll synchronous web server (ESP8266)
    configServer.HandleClient();
}

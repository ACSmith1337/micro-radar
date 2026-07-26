#line 1 "/home/hermes/projects/adsb-radar/adsb-radar.ino"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

#include "src/LGFX.h"
#include "src/WiFiManagerHelpers.h"
#include "src/ConfigurationWebServer.h"
#include "src/HttpRequestManager.h"
#include "src/AircraftManager.h"
#include "src/models/Aircraft.h"

// Optional hard-coded Wi-Fi credentials. Leave both blank to skip pre-baking them and use the setup hotspot instead.
const char* preconfiguredWifiSsid = "";
const char* preconfiguredWifiPassword = "";

constexpr int SCREEN_SIZE = 240;

LGFX tft;
WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;

AircraftManager aircraftManager(configServer, http, tft);


#line 26 "/home/hermes/projects/adsb-radar/adsb-radar.ino"
void setup();
#line 67 "/home/hermes/projects/adsb-radar/adsb-radar.ino"
void loop();
#line 26 "/home/hermes/projects/adsb-radar/adsb-radar.ino"
void setup()
{
    Serial.begin(115200);

    // initialise display
    tft.init();

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

 
}

void loop()
{
    // Update aircraft data + draw incremental updates (scanline + aircraft)
    aircraftManager.Update();

    // Poll synchronous web server (ESP8266)
    configServer.HandleClient();
}

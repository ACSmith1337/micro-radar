#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "PreferencesCompat.h"

class ConfigurationWebServer {
private:
    AsyncWebServer* server;
    Preferences prefs;

public:
    ConfigurationWebServer() : server(new AsyncWebServer(80)), prefs() {}
    ~ConfigurationWebServer() { delete server; }

    void Initialise();
    void HandleClient() {} // No-op for AsyncWebServer
    [[nodiscard]] const String GetStoredString(const char* key);
};

#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "PreferencesCompat.h"

class ConfigurationWebServer {
private:
    ESP8266WebServer server;
    Preferences prefs;

    void HandleRoot();
    void HandleSave();


public:
    ConfigurationWebServer() : server(80), prefs() {}

    void Initialise();
    void HandleClient();
    [[nodiscard]] const String GetStoredString(const char* key);
};
#endif

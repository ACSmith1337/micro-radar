#line 1 "/home/hermes/micro-radar/src/ConfigurationWebServer.h"
#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "PreferencesCompat.h"

// ── Shared log buffer ──
#define LOG_BUFFER_SIZE 32
#define LOG_ENTRY_LEN 128
struct LogBuffer {
    char entries[LOG_BUFFER_SIZE][LOG_ENTRY_LEN];
    uint8_t head = 0;
    uint8_t count = 0;
    void log(const char* msg) {
        strncpy(entries[head], msg, LOG_ENTRY_LEN - 1);
        entries[head][LOG_ENTRY_LEN - 1] = '\0';
        head = (head + 1) % LOG_BUFFER_SIZE;
        if (count < LOG_BUFFER_SIZE) count++;
    }
    String dump() {
        String out;
        uint8_t start = (head + LOG_BUFFER_SIZE - count) % LOG_BUFFER_SIZE;
        for (uint8_t i = 0; i < count; i++) {
            uint8_t idx = (start + i) % LOG_BUFFER_SIZE;
            out += entries[idx];
            out += '\n';
        }
        return out;
    }
};
extern LogBuffer g_logBuffer;

// Log helper — writes to Serial AND the web-viewable buffer
void GridLog(const char* msg);

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
    void RequestReload();
};

#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "PreferencesCompat.h"

// ── Shared log buffer ──
#define LOG_BUFFER_SIZE 32
#define LOG_ENTRY_LEN 128
struct LogBuffer {
    char entries[LOG_BUFFER_SIZE][LOG_ENTRY_LEN];
    uint8_t head = 0;
    uint8_t count = 0;
    void log(const char* msg) {
        strncpy(entries[head], msg, LOG_ENTRY_LEN - 1);
        entries[head][LOG_ENTRY_LEN - 1] = '\0';
        head = (head + 1) % LOG_BUFFER_SIZE;
        if (count < LOG_BUFFER_SIZE) count++;
    }
    String dump() {
        String out;
        uint8_t start = (head + LOG_BUFFER_SIZE - count) % LOG_BUFFER_SIZE;
        for (uint8_t i = 0; i < count; i++) {
            uint8_t idx = (start + i) % LOG_BUFFER_SIZE;
            out += entries[idx];
            out += '\n';
        }
        return out;
    }
};
extern LogBuffer g_logBuffer;

// Log helper — writes to Serial AND the web-viewable buffer
void GridLog(const char* msg);

class ConfigurationWebServer {
private:
    ESP8266WebServer server;
    Preferences prefs;
    bool reloadRequested = false;

    void HandleRoot();
    void HandleSave();
    void HandleSync();
    void HandleLogs();
    void HandleStatus();

public:
    ConfigurationWebServer() : server(80), prefs() {}

    void Initialise();
    void HandleClient();
    [[nodiscard]] const String GetStoredString(const char* key);
    void RequestReload();
    bool HasReloadRequested() { bool r = reloadRequested; reloadRequested = false; return r; }
};
#endif

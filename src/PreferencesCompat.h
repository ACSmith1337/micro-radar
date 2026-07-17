#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>

#elif defined(ARDUINO_ARCH_ESP8266)
// ESP8266: LittleFS + JSON config — reliable, no corruption
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>

#define CONFIG_JSON_PATH "/config.json"

class Preferences {
private:
    const char* _namespace = nullptr;
    bool _opened = false;

public:
    bool begin(const char* ns, bool readOnly = false) {
        (void)readOnly;
        _namespace = ns;
        if (!LittleFS.begin()) {
            Serial.println("[FS] Mounting LittleFS...");
            LittleFS.format();
            LittleFS.begin();
        }
        _opened = true;
        return true;
    }

    void end() {
        if (_opened) {
            _opened = false;
        }
    }

    String getString(const char* key, const String& def = "") {
        if (!_opened) return def;
        if (!LittleFS.exists(CONFIG_JSON_PATH)) return def;

        File f = LittleFS.open(CONFIG_JSON_PATH, "r");
        if (!f) return def;

        size_t len = f.size();
        std::unique_ptr<char[]> buf(new char[len + 1]);
        f.readBytes(buf.get(), len);
        buf[len] = '\0';
        f.close();

        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, buf.get());
        if (err) return def;

        const char* val = doc[key];
        if (val) return String(val);
        return def;
    }

    void putString(const char* key, const String& value) {
        if (!_opened) return;

        StaticJsonDocument<512> doc;

        // Load existing config
        bool loadExisting = false;
        if (LittleFS.exists(CONFIG_JSON_PATH)) {
            File f = LittleFS.open(CONFIG_JSON_PATH, "r");
            if (f) {
                size_t len = f.size();
                std::unique_ptr<char[]> buf(new char[len + 1]);
                f.readBytes(buf.get(), len);
                buf[len] = '\0';
                f.close();

                DeserializationError err = deserializeJson(doc, buf.get());
                if (!err) loadExisting = true;
            }
        }

        // Set value
        doc[key] = value.c_str();

        // Save
        File f = LittleFS.open(CONFIG_JSON_PATH, "w");
        if (!f) {
            Serial.printf("[FS] Failed to open %s for write\n", CONFIG_JSON_PATH);
            return;
        }

        if (serializeJson(doc, f) == 0) {
            Serial.println("[FS] Failed to serialize config");
        }
        f.close();

        Serial.printf("[FS] Saved %s = '%s'\n", key, value.c_str());
    }
};

#endif

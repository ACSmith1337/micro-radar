#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>

#elif defined(ARDUINO_ARCH_ESP8266)
// ESP8266 has no Preferences NVS — shim it with EEPROM.
// Supports only the subset we use: begin(), getString(), putString(), end()
#include <EEPROM.h>

class Preferences {
private:
    static const uint32_t EEPROM_SIZE = 2048;
    static const uint16_t SLOT_SIZE   = 64;
    bool _opened = false;

    uint8_t crc8(const uint8_t* data, size_t len) {
        uint8_t crc = 0;
        for (size_t i = 0; i < len; i++) {
            uint8_t in = data[i];
            for (int j = 0; j < 8; j++) {
                crc ^= (in >> j) & 1;
                crc = crc & 1 ? (crc >> 1) ^ 0x8C : crc >> 1;
            }
        }
        return crc;
    }

public:
    bool begin(const char* partition, bool readOnly = false) {
        (void)readOnly;
        EEPROM.begin(EEPROM_SIZE);
        _opened = true;
        return true;
    }

    void end() {
        if (_opened) {
            EEPROM.commit();
            _opened = false;
        }
    }

    String getString(const char* key, const String& def = "") {
        if (!_opened) return def;
        size_t klen = strlen(key);
        Serial.printf("[EEPROM] getString('%s', len=%d): scanning %d slots\n", key, klen, EEPROM_SIZE / SLOT_SIZE);
        for (uint16_t slot = 0; slot < (EEPROM_SIZE / SLOT_SIZE); slot++) {
            uint16_t off = slot * SLOT_SIZE;
            uint8_t kl = EEPROM.read(off);
            if (kl == 0xFF || kl != klen || kl > 56) continue;
            bool match = true;
            for (size_t i = 0; i < klen; i++) {
                if (EEPROM.read(off + 1 + i) != key[i]) { match = false; break; }
            }
            if (!match || EEPROM.read(off + 1 + kl) != 0) continue;
            uint16_t vl = (EEPROM.read(off + 1 + kl + 1) << 8) | EEPROM.read(off + 1 + kl + 2);
            if (vl > 52) continue;
            String val;
            for (uint16_t i = 0; i < vl; i++) {
                val += (char)EEPROM.read(off + 1 + kl + 3 + i);
            }
            Serial.printf("[EEPROM] getString('%s') FOUND at slot %d, vl=%d → '%s'\n", key, slot, vl, val.c_str());
            return val;
        }
        Serial.printf("[EEPROM] getString('%s') NOT FOUND → returning default '%s'\n", key, def.c_str());
        return def;
    }

    void putString(const char* key, const String& value) {
        if (!_opened) return;
        size_t klen = strlen(key);
        size_t vlen = value.length();
        if (klen + vlen + 4 > 56) return;

        uint16_t targetOff = 0;
        bool found = false;
        for (uint16_t slot = 0; slot < (EEPROM_SIZE / SLOT_SIZE); slot++) {
            uint16_t off = slot * SLOT_SIZE;
            uint8_t kl = EEPROM.read(off);
            if (kl == 0xFF || kl > 56) { targetOff = off; found = true; break; } // uninitialized
            if (kl != klen) continue;
            bool match = true;
            for (size_t i = 0; i < klen; i++) {
                if (EEPROM.read(off + 1 + i) != key[i]) { match = false; break; }
            }
            if (match) { targetOff = off; found = true; break; }
        }
        if (!found) {
            Serial.printf("[EEPROM] putString('%s') FAILED - no free slot\n", key);
            return;
        }

        EEPROM.write(targetOff, (uint8_t)klen);
        for (size_t i = 0; i < klen; i++) EEPROM.write(targetOff + 1 + i, key[i]);
        uint8_t dataOff = targetOff + 1 + klen;
        EEPROM.write(dataOff, (uint8_t)0);
        EEPROM.write(dataOff + 1, (uint8_t)(vlen >> 8));
        EEPROM.write(dataOff + 2, (uint8_t)(vlen & 0xFF));
        for (size_t i = 0; i < vlen; i++) EEPROM.write(dataOff + 3 + i, value[i]);
        Serial.printf("[EEPROM] putString('%s'='%s') → slot %d (offset=%d)\n", key, value.c_str(), targetOff / SLOT_SIZE, targetOff);
    }
};
#endif

#line 1 "/home/hermes/micro-radar/src/HttpRequestManager.h"
#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include <HTTPClient.h>
#elif defined(ARDUINO_ARCH_ESP8266)
// On ESP8266, use raw WiFiClient — ESP8266HTTPClient's HTTP_GET/HTTP_POST enums
// conflict with ESPAsyncWebServer, and the old begin(url) API is deprecated.
#include <WiFiClient.h>
#endif

#include <vector>

struct HttpResult {
    bool success;           // Whether the request succeeded
    int statusCode;         // HTTP status code (0 if network error)
    String response;        // Response body (empty on error)
    String errorMessage;    // Error description if success == false
};

// Streaming result: caller receives a WiFiClient* positioned at body start
// Caller MUST delete the client when done (even on error if client != nullptr)
struct HttpStreamResult {
    bool success;
    int statusCode;
    int contentLength;
    WiFiClient* client;     // nullptr on failure; caller owns deletion
    String errorMessage;
};

class HttpRequestManager
{
#if defined(ARDUINO_ARCH_ESP32)
private:
    HTTPClient http;
#endif

    String BuildQueryString(const std::vector<std::pair<String, String>>& params) const;

public:
    HttpRequestManager() = default;
    ~HttpRequestManager() = default;

    [[nodiscard]] HttpResult Get(const String& url, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {}, int timeout_ms = 0);
    [[nodiscard]] HttpResult Post(const String& url, const String& body = "", const std::vector<std::pair<String, String>>& headers = {});

    // Streaming GET: returns client positioned at body start for direct deserialization
    // Caller MUST delete result.client when done
#if defined(ARDUINO_ARCH_ESP8266)
    [[nodiscard]] HttpStreamResult StreamGet(const String& url, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {});
#endif
};

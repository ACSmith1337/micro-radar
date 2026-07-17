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

    [[nodiscard]] HttpResult Get(const String& url, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {});
    [[nodiscard]] HttpResult Post(const String& url, const String& body = "", const std::vector<std::pair<String, String>>& headers = {});
};

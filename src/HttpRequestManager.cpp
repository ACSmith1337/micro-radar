#include "HttpRequestManager.h"

#include <algorithm>

constexpr int HTTP_TIMEOUT_MS = 5000; // 5 second request timeout
constexpr int MAX_HTTP_BODY   = 8192; // Cap response size

static bool TimedWaitAvailable(WiFiClient& client, int timeout_ms)
{
    uint32_t start = millis();
    while (client.connected() && !client.available()) {
        if (millis() - start > timeout_ms) return false;
        delay(5);
    }
    return client.connected();
}

static bool ReadHeaderTimeout(WiFiClient& client, int timeout_ms)
{
    uint32_t start = millis();
    while (client.connected() && (millis() - start < timeout_ms)) {
        if (!client.available()) {
            delay(5);
            continue;
        }
        String line = client.readStringUntil('\n');
        // Empty line = end of headers
        if (line.length() <= 2) return true;
    }
    return client.connected();
}

String HttpRequestManager::BuildQueryString(const std::vector<std::pair<String, String>>& params) const
{
    if (params.empty())
        return "";

    String queryStream = "?";
    bool first = true;
    for (const auto& [key, value] : params)
    {
        if (!first)
            queryStream += "&";
        queryStream += key + "=" + value;
        first = false;
    }
    return queryStream;
}

#if defined(ARDUINO_ARCH_ESP32)

HttpResult HttpRequestManager::Get(const String& url, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    HttpResult result{ false, 0, "", "" };

    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    http.begin(fullUrl);

    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    int responseCode = http.GET();
    result.statusCode = responseCode;

    // Require 2xx status
    if (responseCode >= 200 && responseCode < 300) {
        result.success = true;
        result.response = http.getString();
    }
    else {
        result.success = false;
        result.errorMessage = http.errorToString(responseCode);
        Serial.print("[GET] HTTP Error (");
        Serial.print(responseCode);
        Serial.print("): ");
        Serial.println(result.errorMessage);
    }

    http.end();
    return result;
}

HttpResult HttpRequestManager::Post(const String& url, const String& body, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    http.begin(url);

    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    int responseCode = http.POST(body);
    result.statusCode = responseCode;

    if (responseCode >= 200 && responseCode < 300) {
        result.success = true;
        result.response = http.getString();
    }
    else {
        result.success = false;
        result.errorMessage = http.errorToString(responseCode);
        Serial.print("[POST] HTTP Error (");
        Serial.print(responseCode);
        Serial.print("): ");
        Serial.println(result.errorMessage);
    }

    http.end();
    return result;
}

#elif defined(ARDUINO_ARCH_ESP8266)

HttpResult HttpRequestManager::Get(const String& url, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    HttpResult result{ false, 0, "", "" };

    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    const char* url_cstr = fullUrl.c_str();
    int schemeEnd = 7;
    int pathStart = fullUrl.indexOf('/', schemeEnd);
    if (pathStart == -1) pathStart = fullUrl.length();

    String host = fullUrl.substring(schemeEnd, pathStart);
    String path = fullUrl.substring(pathStart);
    int port = 80;

    int colonPos = host.indexOf(':');
    if (colonPos != -1) {
        port = host.substring(colonPos + 1).toInt();
        host = host.substring(0, colonPos);
    }

    WiFiClient client;
    if (!client.connect(host.c_str(), port)) {
        result.errorMessage = "Connection failed";
        Serial.println("[GET] Connection failed: " + host);
        return result;
    }

    client.print("GET ");
    client.print(path);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);

    for (const auto& header : headers) {
        client.print(header.first);
        client.print(": ");
        client.println(header.second);
    }

    client.println("Connection: close");
    client.println();

    // Wait for response with timeout
    if (!TimedWaitAvailable(client, HTTP_TIMEOUT_MS)) {
        result.errorMessage = "Timeout waiting for response";
        client.stop();
        Serial.println("[GET] Timeout: " + host);
        return result;
    }

    // Read HTTP status line (e.g. "HTTP/1.1 200 OK\r")
    String statusCodeLine = client.readStringUntil('\r');
    // Consume trailing \n from CRLF — prevents ReadHeaderTimeout
    // from mistaking bare \n as end-of-headers
    if (client.available()) client.read();

    int statusCode = 0;
    int space1 = statusCodeLine.indexOf(' ');
    int space2 = statusCodeLine.indexOf(' ', space1 + 1);
    if (space1 > 0 && space2 > space1) {
        statusCode = statusCodeLine.substring(space1 + 1, space2).toInt();
    }

    result.statusCode = statusCode;

    // Skip headers with timeout
    ReadHeaderTimeout(client, HTTP_TIMEOUT_MS);

    // Require 2xx status — read body in chunks with yield() between reads
    // so the WiFi stack + rendering task get time slices during network I/O
    if (statusCode >= 200 && statusCode < 300) {
        result.success = true;
        // Chunked read: 256 bytes per chunk, yield between each
        constexpr int CHUNK = 256;
        uint8_t buf[CHUNK];
        while (client.connected()) {
            int avail = client.available();
            if (avail > 0) {
                int toRead = std::min(avail, CHUNK);
                if (toRead < 1) {
                    yield();
                    delay(1);
                    continue;
                }
                if ((int)result.response.length() + toRead > MAX_HTTP_BODY) {
                    toRead = MAX_HTTP_BODY - (int)result.response.length();
                    if (toRead < 1) break;
                }
                buf[toRead] = '\0';
                int n = client.read(buf, toRead);
                if (n > 0) {
                    buf[n] = '\0';
                    result.response += (const char*)buf;
                }
            } else {
                yield(); // Let rendering run while waiting for data
                delay(1);
            }
        }
    } else {
        result.success = false;
        result.errorMessage = "HTTP " + String(statusCode);
        // Drain remaining body to close connection cleanly
        client.stop();
    }

    client.stop();
    return result;
}

HttpResult HttpRequestManager::Post(const String& url, const String& body, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    int schemeEnd = url.indexOf('://');
    schemeEnd = (schemeEnd >= 0) ? schemeEnd + 3 : 0;
    int pathStart = url.indexOf('/', schemeEnd);
    if (pathStart == -1) pathStart = url.length();

    String host = url.substring(schemeEnd, pathStart);
    String path = url.substring(pathStart);
    int port = 80;

    int colonPos = host.indexOf(':');
    if (colonPos != -1) {
        port = host.substring(colonPos + 1).toInt();
        host = host.substring(0, colonPos);
    }

    WiFiClient client;
    if (!client.connect(host.c_str(), port)) {
        result.errorMessage = "Connection failed";
        Serial.println("[POST] Connection failed: " + host);
        return result;
    }

    client.print("POST ");
    client.print(path);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Content-Type: application/x-www-form-urlencoded");

    for (const auto& header : headers) {
        client.print(header.first);
        client.print(": ");
        client.println(header.second);
    }

    client.println("Connection: close");
    client.println();

    if (body.length() > 0) {
        client.print(body);
    }

    if (!TimedWaitAvailable(client, HTTP_TIMEOUT_MS)) {
        result.errorMessage = "Timeout waiting for response";
        client.stop();
        Serial.println("[POST] Timeout: " + host);
        return result;
    }

    // Read HTTP status line + consume trailing \n from CRLF
    String statusCodeLine = client.readStringUntil('\r');
    if (client.available()) client.read();

    int statusCode = 0;
    int space1 = statusCodeLine.indexOf(' ');
    int space2 = statusCodeLine.indexOf(' ', space1 + 1);
    if (space1 > 0 && space2 > space1) {
        statusCode = statusCodeLine.substring(space1 + 1, space2).toInt();
    }

    result.statusCode = statusCode;

    ReadHeaderTimeout(client, HTTP_TIMEOUT_MS);

    if (statusCode >= 200 && statusCode < 300) {
        result.success = true;
        result.response = client.readString();
    } else {
        result.success = false;
        result.errorMessage = "HTTP " + String(statusCode);
        client.stop();
    }

    client.stop();
    return result;
}

#endif

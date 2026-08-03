#include "HttpRequestManager.h"

#include <algorithm>

constexpr int HTTP_TIMEOUT_MS = 8000; // 8 second request timeout (tar1090 can be slow)
constexpr int MAX_HTTP_BODY   = 8192; // Cap response size
constexpr int MAX_FETCH_RETRIES = 2;  // Retry failed fetches up to 2 times

static bool TimedWaitAvailable(WiFiClient& client, int timeout_ms)
{
    uint32_t start = millis();
    uint32_t timeout = (timeout_ms > 0) ? (uint32_t)timeout_ms : 0U;
    while (client.connected() && !client.available()) {
        if (millis() - start > timeout) return false;
        yield();  // Keep WiFi watchdog alive, let web server respond
        delay(1);
    }
    return client.connected();
}

// ── Read HTTP status line, return status code ──
// Consumes the full "HTTP/1.1 200 OK\r\n" line
static int ReadStatusLine(WiFiClient& client, int timeout_ms)
{
    if (!TimedWaitAvailable(client, timeout_ms)) return 0;
    String line = client.readStringUntil('\n');
    int space1 = line.indexOf(' ');
    if (space1 < 0) return 0;
    return line.substring(space1 + 1).toInt();
}

// ── Parse headers, return Content-Length (0 if not found) ──
// Consumes all headers up to and including the blank line
static int ReadHeaders(WiFiClient& client, int timeout_ms)
{
    int contentLength = 0;
    uint32_t start = millis();
    uint32_t timeout = (timeout_ms > 0) ? (uint32_t)timeout_ms : 0U;
    while (client.connected() && (millis() - start < timeout)) {
        if (!client.available()) {
            yield();
            delay(1);
            continue;
        }
        String line = client.readStringUntil('\n');
        // Blank line (just \r or empty) = end of headers
        if (line.length() <= 2) break;
        // Check for Content-Length header
        if (line.startsWith("Content-Length:")) {
            String val = line.substring(15);
            val.trim();
            contentLength = val.toInt();
        }
    }
    return contentLength;
}

// ── Read exactly N bytes from client, with yield() between chunks ──
static String ReadBody(WiFiClient& client, int numBytes, int maxBytes)
{
    String result;
    if (numBytes > maxBytes) numBytes = maxBytes;
    if (numBytes <= 0) return result;
    result.reserve(numBytes);

    constexpr int CHUNK = 256;
    uint8_t buf[CHUNK + 1];
    int remaining = numBytes;

    uint32_t start = millis();
    uint32_t lastProgress = start;
    while (remaining > 0) {
        int avail = client.available();
        if (avail > 0) {
            lastProgress = millis();
            int toRead = std::min(avail, std::min(remaining, CHUNK));
            int n = client.read(buf, toRead);
            if (n > 0) {
                buf[n] = '\0';
                result += (const char*)buf;
                remaining -= n;
            }
        } else {
            yield();
            delay(1);
            // Give the TCP stack time to surface buffered bytes after FIN.
            // Abort only after total timeout OR prolonged no-progress stall.
            uint32_t now = millis();
            if ((now - start) > HTTP_TIMEOUT_MS) break;
            if (!client.connected() && (now - lastProgress) > 250) break;
        }
    }
    return result;
}

// ── Drain remaining data from connection (for non-Content-Length responses) ──
static String ReadBodyStream(WiFiClient& client, int maxBytes)
{
    String result;
    constexpr int CHUNK = 256;
    uint8_t buf[CHUNK + 1];
    int idleTimeout = 0;
    while ((int)result.length() < maxBytes) {
        int avail = client.available();
        if (avail > 0) {
            idleTimeout = 0;
            int toRead = std::min(avail, CHUNK);
            if ((int)result.length() + toRead > maxBytes) {
                toRead = maxBytes - (int)result.length();
                if (toRead < 1) break;
            }
            int n = client.read(buf, toRead);
            if (n > 0) {
                buf[n] = '\0';
                result += (const char*)buf;
            }
        } else {
            yield();
            delay(1);
            idleTimeout++;
            if (!client.connected() && idleTimeout > 50) break;
        }
    }
    return result;
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

    int schemeEnd = fullUrl.indexOf("://");
    schemeEnd = (schemeEnd >= 0) ? schemeEnd + 3 : 0;
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

    int statusCode = ReadStatusLine(client, HTTP_TIMEOUT_MS);
    if (statusCode == 0) {
        result.errorMessage = "Timeout or invalid response";
        client.stop();
        return result;
    }
    result.statusCode = statusCode;

    // Parse headers → get Content-Length
    int contentLength = ReadHeaders(client, HTTP_TIMEOUT_MS);
    Serial.printf("[GET] Status=%d CL=%d\r\n", statusCode, contentLength);

    if (statusCode >= 200 && statusCode < 300) {
        result.success = true;
        if (contentLength > 0 && contentLength <= MAX_HTTP_BODY) {
            // Small enough to buffer
            result.response = ReadBody(client, contentLength, MAX_HTTP_BODY);
            if ((int)result.response.length() < contentLength) {
                result.success = false;
                result.errorMessage = "Truncated body: got " + String(result.response.length()) +
                                      " of " + String(contentLength);
            }
        } else if (contentLength > MAX_HTTP_BODY) {
            // Large response — caller must use StreamGet instead
            result.success = false;
            result.errorMessage = "Response too large (" + String(contentLength) + " bytes) — use StreamGet";
        } else {
            // Fallback: drain until connection closes
            result.response = ReadBodyStream(client, MAX_HTTP_BODY);
        }
    } else {
        result.success = false;
        result.errorMessage = "HTTP " + String(statusCode);
        client.stop();
        return result;
    }

    // Drain any trailing data
    while (client.connected() || client.available()) {
        yield();
        delay(1);
        while (client.available()) client.read();
        if (!client.connected()) break;
    }

    client.stop();
    return result;
}

// ── Streaming GET: returns client after headers for direct ArduinoJson deserialization ──
HttpStreamResult HttpRequestManager::StreamGet(const String& url, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    HttpStreamResult result{ false, 0, 0, nullptr };

    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    int schemeEnd = fullUrl.indexOf("://");
    schemeEnd = (schemeEnd >= 0) ? schemeEnd + 3 : 0;
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

    WiFiClient* client = new WiFiClient();
    if (!client->connect(host.c_str(), port)) {
        result.errorMessage = "Connection failed";
        Serial.println("[STREAM] Connection failed: " + host);
        delete client;
        return result;
    }

    client->print("GET ");
    client->print(path);
    client->println(" HTTP/1.1");
    client->print("Host: ");
    client->println(host);

    for (const auto& header : headers) {
        client->print(header.first);
        client->print(": ");
        client->println(header.second);
    }

    client->println("Connection: close");
    client->println();

    int statusCode = ReadStatusLine(*client, HTTP_TIMEOUT_MS);
    if (statusCode == 0) {
        result.errorMessage = "Timeout or invalid response";
        client->stop();
        delete client;
        return result;
    }
    result.statusCode = statusCode;

    int contentLength = ReadHeaders(*client, HTTP_TIMEOUT_MS);
    Serial.printf("[STREAM] Status=%d CL=%d\r\n", statusCode, contentLength);

    if (statusCode >= 200 && statusCode < 300) {
        result.success = true;
        result.contentLength = contentLength;
        result.client = client;
    } else {
        result.success = false;
        result.errorMessage = "HTTP " + String(statusCode);
        client->stop();
        delete client;
    }

    return result;
}

HttpResult HttpRequestManager::Post(const String& url, const String& body, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    int schemeEnd = url.indexOf("://");
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

    int statusCode = ReadStatusLine(client, HTTP_TIMEOUT_MS);
    if (statusCode == 0) {
        result.errorMessage = "Timeout or invalid response";
        client.stop();
        return result;
    }
    result.statusCode = statusCode;

    int contentLength = ReadHeaders(client, HTTP_TIMEOUT_MS);

    if (statusCode >= 200 && statusCode < 300) {
        result.success = true;
        if (contentLength > 0) {
            result.response = ReadBody(client, contentLength, MAX_HTTP_BODY);
            if ((int)result.response.length() < contentLength && contentLength <= MAX_HTTP_BODY) {
                result.success = false;
                result.errorMessage = "Truncated body: got " + String(result.response.length()) +
                                      " of " + String(contentLength);
            }
        } else {
            result.response = ReadBodyStream(client, MAX_HTTP_BODY);
        }
    } else {
        result.success = false;
        result.errorMessage = "HTTP " + String(statusCode);
        client.stop();
        return result;
    }

    client.stop();
    return result;
}

#endif

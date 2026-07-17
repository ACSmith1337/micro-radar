#include "HttpRequestManager.h"

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

    // add headers to request
    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    // send request and handle response
    int responseCode = http.GET();
    result.statusCode = responseCode;

    if (responseCode > 0) {
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

    // add headers to request
    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    // send request and handle response
    int responseCode = http.POST(body);
    result.statusCode = responseCode;

    if (responseCode > 0) {
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

// Raw WiFiClient implementation for ESP8266 — avoids ESP8266HTTPClient enum conflicts
HttpResult HttpRequestManager::Get(const String& url, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    HttpResult result{ false, 0, "", "" };

    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    // Parse host and path from URL (assuming http://)
    const char* url_cstr = fullUrl.c_str();
    int schemeEnd = 7; // skip "http://"
    int pathStart = fullUrl.indexOf('/', schemeEnd);
    if (pathStart == -1) pathStart = fullUrl.length();

    String host = fullUrl.substring(schemeEnd, pathStart);
    String path = fullUrl.substring(pathStart);
    int port = 80;

    // Check for custom port
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

    // Build HTTP request
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

    // Read response
    int statusCode = 0;
    String statusCodeLine;
    while (client.connected() && !client.available()) { delay(10); }
    if (client.connected()) {
        statusCodeLine = client.readStringUntil('\r');
        // "HTTP/1.1 200 OK"
        int space1 = statusCodeLine.indexOf(' ');
        int space2 = statusCodeLine.indexOf(' ', space1 + 1);
        if (space1 > 0 && space2 > space1) {
            statusCode = statusCodeLine.substring(space1 + 1, space2).toInt();
        }
    }

    result.statusCode = statusCode;

    // Skip headers until empty line
    String headerLine;
    while (client.connected()) {
        headerLine = client.readStringUntil('\n');
        if (headerLine.length() <= 2) break; // empty line
    }

    // Read body
    if (statusCode > 0) {
        result.success = true;
        result.response = client.readString();
    } else {
        result.errorMessage = "Invalid response";
        Serial.print("[GET] Invalid response from ");
        Serial.println(host);
    }

    client.stop();
    return result;
}

HttpResult HttpRequestManager::Post(const String& url, const String& body, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    // Parse host and path
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

    // Send body
    if (body.length() > 0) {
        client.print(body);
    }

    // Read response
    int statusCode = 0;
    String statusCodeLine;
    while (client.connected() && !client.available()) { delay(10); }
    if (client.connected()) {
        statusCodeLine = client.readStringUntil('\r');
        int space1 = statusCodeLine.indexOf(' ');
        int space2 = statusCodeLine.indexOf(' ', space1 + 1);
        if (space1 > 0 && space2 > space1) {
            statusCode = statusCodeLine.substring(space1 + 1, space2).toInt();
        }
    }

    result.statusCode = statusCode;

    // Skip headers
    String headerLine;
    while (client.connected()) {
        headerLine = client.readStringUntil('\n');
        if (headerLine.length() <= 2) break;
    }

    if (statusCode > 0) {
        result.success = true;
        result.response = client.readString();
    } else {
        result.errorMessage = "Invalid response";
        Serial.print("[POST] Invalid response from ");
        Serial.println(host);
    }

    client.stop();
    return result;
}

#endif

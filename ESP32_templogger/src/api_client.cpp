// =============================================================================
//  api_client.cpp  –  Implementierung von ApiClient
// =============================================================================
#include "api_client.h"
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>

int ApiClient::get(const std::string &url, const HttpHeader *headers, size_t headerCount, std::string &response) {
  return request(url, headers, headerCount, nullptr, response);
}

int ApiClient::post(const std::string &url, const HttpHeader *headers, size_t headerCount, const std::string &body,
                    std::string &response) {
  return request(url, headers, headerCount, &body, response);
}

int ApiClient::request(const std::string &url, const HttpHeader *headers, size_t headerCount, const std::string *body,
                       std::string &response) {
  response.clear();
  const bool https = url.rfind("https://", 0) == 0;
  NetworkClient       plain;
  NetworkClientSecure secure;

  if (https) {
    secure.useBuiltinCACertBundle();
  } else if (!warnedPlain_) {
    Serial.println("[API] WARNUNG: unverschlüsselte Verbindung (http), nur für lokale Tests");
    warnedPlain_ = true;
  }

  HTTPClient http;
  http.setTimeout(cfg_.timeoutMs);
  http.setConnectTimeout(cfg_.timeoutMs);
  http.setReuse(false);
  http.setUserAgent(cfg_.userAgent);
  const bool started = https ? http.begin(secure, url.c_str()) : http.begin(plain, url.c_str());
  if (!started) {
    Serial.println("[API] HTTP-Start fehlgeschlagen (Server-URL prüfen)");
    return -100;
  }

  http.addHeader("Accept", "application/json");
  if (body != nullptr) http.addHeader("Content-Type", "application/json");
  for (size_t i = 0; i < headerCount; i++) http.addHeader(headers[i].name, headers[i].value);

  const int code = body != nullptr
                       ? http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(body->data())), body->size())
                       : http.GET();
  if (code > 0) {
    response = http.getString().c_str();
  } else {
    Serial.printf("[API] Verbindung fehlgeschlagen: %d (%s)\n", code, HTTPClient::errorToString(code).c_str());
  }
  http.end();
  return code;
}

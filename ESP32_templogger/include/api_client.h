// =============================================================================
//  api_client.h  –  HTTP(S)-Anfragen an den Energielogger
// -----------------------------------------------------------------------------
//  Nur der Transport: eine Anfrage, keine Wiederholung. Was die Antwort bedeutet,
//  entscheidet EnergieloggerClient. https prüft das Serverzertifikat gegen das im
//  Core enthaltene CA-Paket (Anhang A.2); http ist nur für lokale Tests gedacht.
//  Der Body wird als rohe Bytes gesendet, denn genau diese Bytes sind signiert.
// =============================================================================
#pragma once
#include <Arduino.h>
#include <string>

struct HttpHeader {
  const char *name;
  const char *value;
};

class ApiClient {
public:
  struct Config {
    const char *userAgent;   // eigener User-Agent, siehe README Kapitel 4
    uint32_t    timeoutMs;   // je für Verbindungsaufbau und Antwort
  };

  explicit ApiClient(const Config &cfg) : cfg_(cfg) {}

  // Rückgabe: HTTP-Statuscode oder negativer Fehlercode (Netz, DNS, TLS);
  // -100, wenn die URL nicht verwendbar ist.
  int get(const std::string &url, const HttpHeader *headers, size_t headerCount, std::string &response);
  int post(const std::string &url, const HttpHeader *headers, size_t headerCount, const std::string &body,
           std::string &response);

private:
  int request(const std::string &url, const HttpHeader *headers, size_t headerCount, const std::string *body,
              std::string &response);

  Config cfg_;
  bool   warnedPlain_ = false;
};

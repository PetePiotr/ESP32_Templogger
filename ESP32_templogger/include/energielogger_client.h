// =============================================================================
//  energielogger_client.h  –  Anmeldung und signierter Upload (Schnittstelle Version 1)
// -----------------------------------------------------------------------------
//  Setzt Anhang A von martins/energielogger-pastler/README.md um:
//
//    1. Anmelden (ohne Secret): POST /api/discovery -> 202 pending, dann im Abstand
//       von retry_after GET /api/discovery/{name}, bis das Gerät im Portal unter
//       "Neue Geräte" eingerichtet ist -> 200 approved mit Secret.
//    2. Senden: POST an ingest_url alle interval_s Sekunden, signiert mit
//       HMAC-SHA256(secret, X-Timestamp + "\n" + X-Nonce + "\n" + Body).
//    3. Neue Sensoren laufen im Upload unter "sensors" mit, bis der Server 200 meldet.
//
//  claim_token, Secret, gateway_id und ingest_url liegen dauerhaft im Flash (NVS).
//  Secret und claim_token erscheinen nie im Log. Werte werden erst nach 200 oder
//  einer endgültigen Ablehnung (400, 409, 413, 422) aus der Warteschlange gelöscht.
// =============================================================================
#pragma once
#include <Arduino.h>
#include <string>
#include <vector>
#include "api_client.h"
#include "measurement_source.h"
#include "reading_queue.h"

class EnergieloggerClient {
public:
  struct Config {
    const char *serverUrl;          // z. B. "https://portal.energielogger.de"
    const char *deviceBaseName;     // Gerätename = <Basisname>-<letzte 3 MAC-Bytes>
    const char *firmware;
    size_t      maxReadingsPerUpload;
    uint32_t    catchupGapMs;       // Abstand der Uploads, solange ein Rückstau abzubauen ist
    bool        factoryReset;       // Anmeldung und claim_token löschen, einmal je Build
    const char *buildId;            // Kennung des Firmware-Builds (Datum und Uhrzeit)
  };

  // Zustand des Netzes für die Diagnose; WLAN-Signal gibt es über Ethernet nicht.
  struct NetStatus {
    bool online;
    bool timeValid;
    bool hasRssi;
    int  rssi;
  };

  EnergieloggerClient(const Config &cfg, ApiClient &http, ReadingQueue &queue)
      : cfg_(cfg), http_(http), queue_(queue) {}

  // Zustand aus dem Flash laden; beim ersten Start claim_token erzeugen.
  void begin(const std::vector<SensorInfo> &sensors);
  // Regelmäßig aufrufen. Blockiert, solange eine Anfrage läuft.
  void loop(const NetStatus &net);

  bool               registered() const { return phase_ == Phase::Ready; }
  const std::string &deviceName() const { return deviceName_; }

private:
  enum class Phase { Register, Pending, Ready };

  static constexpr int      UNAUTHORIZED_LIMIT = 3;            // 401 "unauthorized" in Folge: neu anmelden
  static constexpr uint32_t DEFAULT_INTERVAL_S = 900;
  static constexpr uint32_t REJECTED_WAIT_MS   = 25UL * 3600 * 1000;
  static constexpr size_t   MAX_DISCOVERY_SENSORS = 64;

  void loadState();
  void saveState();
  void forgetRegistration(bool forgetToken);

  void postDiscovery();
  void pollDiscovery();
  void handleDiscoveryResponse(int code, const std::string &response);
  void upload(const NetStatus &net);
  void handleUploadResponse(int code, const std::string &response, size_t count);

  std::string buildUploadBody(size_t count, uint32_t now, const NetStatus &net);
  void        waitForRetry(int failures, const uint32_t *steps, size_t stepCount);

  Config        cfg_;
  ApiClient    &http_;
  ReadingQueue &queue_;
  std::vector<SensorInfo> sensors_;
  std::vector<bool>       reported_;   // Server kennt die Beschreibung
  std::vector<bool>       inUpload_;   // im laufenden Upload beschrieben

  std::string deviceName_, mac_;
  std::string claimToken_, gatewayId_, secret_, ingestUrl_;
  uint32_t    intervalS_ = DEFAULT_INTERVAL_S;

  Phase    phase_        = Phase::Register;
  uint32_t nextMs_       = 0;
  int      failures_     = 0;
  int      unauthorized_ = 0;

  uint32_t uploadsOk_ = 0, uploadsFailed_ = 0, uploadsRejected_ = 0, lastPostMs_ = 0;
};

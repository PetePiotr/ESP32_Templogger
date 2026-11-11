// =============================================================================
//  energielogger_client.cpp  –  Implementierung von EnergieloggerClient
// =============================================================================
#include "energielogger_client.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/md.h>
#include <time.h>

static const char *const NVS_NAMESPACE = "energielogger";

// Abstände nach Netz- oder Serverfehlern (Tabellen in A.4 und A.5)
static const uint32_t DISCOVERY_RETRY_S[] = {30, 60, 120, 300};
static const uint32_t UPLOAD_RETRY_S[]    = {60, 120, 240, 480, 900};

// a liegt nicht vor b (Überlauf von millis() nach 49 Tagen eingerechnet)
static bool reached(uint32_t nowMs, uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }

static std::string base64url(const uint8_t *data, size_t len) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < len) n |= data[i + 2];
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    if (i + 1 < len) out += alphabet[(n >> 6) & 63];
    if (i + 2 < len) out += alphabet[n & 63];
  }
  return out;
}

static std::string randomNonce() {
  char nonce[33];
  snprintf(nonce, sizeof(nonce), "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  return nonce;
}

// Hex von HMAC-SHA256; leer bei einem Fehler der Bibliothek.
static std::string hmacSha256Hex(const std::string &secret, const std::string &message) {
  unsigned char out[32];
  const int err = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                                  reinterpret_cast<const unsigned char *>(secret.data()), secret.size(),
                                  reinterpret_cast<const unsigned char *>(message.data()), message.size(), out);
  if (err != 0) return "";
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", out[i]);
  return hex;
}

static const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power_on";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    default:                return "other";
  }
}

static void addSensorDescription(JsonArray out, const SensorInfo &s) {
  JsonObject o = out.add<JsonObject>();
  o["key"]  = s.key;
  o["type"] = s.type;
  o["unit"] = s.unit;
  if (s.bus && *s.bus)                   o["bus"]          = s.bus;
  if (s.address && *s.address)           o["address"]      = s.address;
  if (s.manufacturer && *s.manufacturer) o["manufacturer"] = s.manufacturer;
  if (s.model && *s.model)               o["model"]        = s.model;
}

// --- Zustand ------------------------------------------------------------------------
void EnergieloggerClient::loadState() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  claimToken_ = prefs.getString("token", "").c_str();
  gatewayId_  = prefs.getString("gateway", "").c_str();
  secret_     = prefs.getString("secret", "").c_str();
  ingestUrl_  = prefs.getString("url", "").c_str();
  intervalS_  = prefs.getUInt("interval", DEFAULT_INTERVAL_S);
  prefs.end();
}

void EnergieloggerClient::saveState() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("token", claimToken_.c_str());
  prefs.putString("gateway", gatewayId_.c_str());
  prefs.putString("secret", secret_.c_str());
  prefs.putString("url", ingestUrl_.c_str());
  prefs.putUInt("interval", intervalS_);
  prefs.end();
}

void EnergieloggerClient::forgetRegistration(bool forgetToken) {
  gatewayId_.clear();
  secret_.clear();
  ingestUrl_.clear();
  intervalS_ = DEFAULT_INTERVAL_S;
  if (forgetToken) claimToken_.clear();
  saveState();
  reported_.assign(sensors_.size(), false);
  phase_        = Phase::Register;
  failures_     = 0;
  unauthorized_ = 0;
  nextMs_       = millis();
}

void EnergieloggerClient::begin(const std::vector<SensorInfo> &sensors) {
  sensors_ = sensors;
  reported_.assign(sensors_.size(), false);
  inUpload_.assign(sensors_.size(), false);

  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  mac_ = buf;
  snprintf(buf, sizeof(buf), "%.24s-%02x%02x%02x", cfg_.deviceBaseName, mac[3], mac[4], mac[5]);
  deviceName_ = buf;
  // Erlaubt sind nur Kleinbuchstaben, Ziffern und "-" (sonst 422 invalid_device_id).
  bool renamed = false;
  for (char &c : deviceName_) {
    const char fixed = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a')
                     : ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') ? c : '-';
    renamed = renamed || fixed != c;
    c = fixed;
  }
  if (renamed) {
    Serial.printf("[EL] WARNUNG: DEVICE_ID enthält unerlaubte Zeichen – Gerätename wird %s "
                  "(erlaubt: a-z, 0-9, -)\n", deviceName_.c_str());
  }

  loadState();
  if (cfg_.factoryReset) {
    // Nur einmal je Build: Die Kennung des Builds, der zurückgesetzt hat, steht im NVS.
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    const bool done = strcmp(prefs.getString("resetbuild", "").c_str(), cfg_.buildId) == 0;
    if (!done) prefs.putString("resetbuild", cfg_.buildId);
    prefs.end();
    if (done) {
      Serial.println("[EL] FACTORY_RESET: für diese Firmware schon ausgeführt – Anmeldung bleibt "
                     "(in config.h wieder auf false stellen)");
    } else {
      Serial.println("[EL] FACTORY_RESET: Portal-Anmeldung gelöscht, neues claim_token – "
                     "Gerät im Portal unter \"Neue Geräte\" neu einrichten");
      forgetRegistration(true);
    }
  }
  if (claimToken_.size() < 43) {
    uint8_t random[32];
    for (size_t i = 0; i < sizeof(random); i += 4) {
      const uint32_t r = esp_random();
      memcpy(random + i, &r, 4);
    }
    claimToken_ = base64url(random, sizeof(random));
    saveState();
  }

  const bool hasSecret = !secret_.empty() && !gatewayId_.empty() && !ingestUrl_.empty();
  phase_  = hasSecret ? Phase::Ready : Phase::Register;
  nextMs_ = millis();
  Serial.printf("[EL] Gerät %s, MAC %s, %u Sensor(en), %s\n", deviceName_.c_str(), mac_.c_str(),
                (unsigned)sensors_.size(), hasSecret ? "im Portal eingerichtet" : "noch nicht eingerichtet");
}

void EnergieloggerClient::waitForRetry(int failures, const uint32_t *steps, size_t stepCount) {
  const size_t index = min(static_cast<size_t>(max(failures, 1)), stepCount) - 1;
  nextMs_ = millis() + steps[index] * 1000UL;
}

void EnergieloggerClient::loop(const NetStatus &net) {
  // Auch die Anmeldung braucht die Uhr: TLS prüft die Gültigkeitsdaten des Zertifikats.
  if (!net.online || !net.timeValid || !reached(millis(), nextMs_)) return;
  switch (phase_) {
    case Phase::Register:
      postDiscovery();
      break;
    case Phase::Pending:
      pollDiscovery();
      break;
    case Phase::Ready:
      if (queue_.size() == 0) {
        nextMs_ = millis() + 10000UL;    // readings ist Pflicht: warten, bis es Werte gibt
        return;
      }
      upload(net);
      break;
  }
}

// --- Anmeldung -------------------------------------------------------------------------
void EnergieloggerClient::postDiscovery() {
  JsonDocument doc;
  doc["device_id"]   = deviceName_;
  doc["mac"]         = mac_;
  doc["firmware"]    = cfg_.firmware;
  doc["claim_token"] = claimToken_;
  JsonArray list = doc["sensors"].to<JsonArray>();
  for (size_t i = 0; i < sensors_.size() && i < MAX_DISCOVERY_SENSORS; i++) addSensorDescription(list, sensors_[i]);
  if (sensors_.size() > MAX_DISCOVERY_SENSORS) {
    Serial.printf("[EL] WARNUNG: nur %u von %u Sensoren in der Anmeldung, der Rest folgt im Upload\n",
                  (unsigned)MAX_DISCOVERY_SENSORS, (unsigned)sensors_.size());
  }
  std::string body;
  serializeJson(doc, body);

  std::string response;
  const int code = http_.post(std::string(cfg_.serverUrl) + "/api/discovery", nullptr, 0, body, response);
  handleDiscoveryResponse(code, response);
}

void EnergieloggerClient::pollDiscovery() {
  const HttpHeader headers[] = {{"X-Claim-Token", claimToken_.c_str()}};
  std::string response;
  const int code = http_.get(std::string(cfg_.serverUrl) + "/api/discovery/" + deviceName_, headers, 1, response);
  handleDiscoveryResponse(code, response);
}

void EnergieloggerClient::handleDiscoveryResponse(int code, const std::string &response) {
  JsonDocument doc;
  deserializeJson(doc, response.c_str());     // HTML einer Firewall ergibt einfach leere Felder
  const char *status = doc["status"] | "";

  if (code == 200 && strcmp(status, "approved") == 0) {
    const char *gateway = doc["gateway_id"] | "";
    const char *secret  = doc["secret"] | "";
    const char *url     = doc["ingest_url"] | "";
    if (*gateway && strlen(secret) >= 16 && strncmp(url, "http", 4) == 0) {
      gatewayId_ = gateway;
      secret_    = secret;
      ingestUrl_ = url;
      intervalS_ = constrain(doc["interval_s"] | DEFAULT_INTERVAL_S, 60UL, 3600UL);
      saveState();
      for (size_t i = 0; i < sensors_.size(); i++) reported_[i] = i < MAX_DISCOVERY_SENSORS;
      phase_        = Phase::Ready;
      failures_     = 0;
      unauthorized_ = 0;
      nextMs_       = millis();
      Serial.printf("[EL] Im Portal eingerichtet als %s, Upload alle %lu s\n", gatewayId_.c_str(),
                    (unsigned long)intervalS_);
      return;
    }
    Serial.println("[EL] FEHLER: Antwort \"approved\" ist unvollständig");
    failures_++;
    waitForRetry(failures_, DISCOVERY_RETRY_S, 4);
    return;
  }

  const uint32_t now = millis();
  if (code == 202) {
    const uint32_t retry = constrain(doc["retry_after"] | 30UL, 10UL, 300UL);
    if (phase_ != Phase::Pending) {
      Serial.printf("[EL] Wartet auf Einrichtung im Portal (Gateways -> Neue Geräte, oder Suche nach MAC %s)\n",
                    mac_.c_str());
    }
    phase_    = Phase::Pending;
    failures_ = 0;
    nextMs_   = now + retry * 1000UL;
  } else if (code == 410 && strcmp(status, "rejected") == 0) {
    Serial.println("[EL] Im Portal ignoriert – neue Anmeldung in 25 Stunden");
    phase_  = Phase::Register;
    nextMs_ = now + REJECTED_WAIT_MS;
  } else if (code == 410) {
    Serial.println("[EL] Anmeldung abgelaufen – melde neu an");
    phase_  = Phase::Register;
    nextMs_ = now + 30000UL;
  } else if (code == 400 || code == 413 || code == 422) {
    Serial.printf("[EL] FEHLER: Anmeldung abgelehnt, HTTP %d: %.200s – neuer Versuch in 1 Stunde\n", code,
                  response.c_str());
    phase_  = Phase::Register;
    nextMs_ = now + 3600000UL;
  } else if (code == 429) {
    Serial.println("[EL] Anmeldung: zu viele Anfragen (429) – warte 5 Minuten");
    nextMs_ = now + 300000UL;
  } else {
    if (code > 0) Serial.printf("[EL] Anmeldung: HTTP %d: %.200s\n", code, response.c_str());
    failures_++;
    waitForRetry(failures_, DISCOVERY_RETRY_S, 4);
  }
}

// --- Upload --------------------------------------------------------------------------
std::string EnergieloggerClient::buildUploadBody(size_t count, uint32_t now, const NetStatus &net) {
  JsonDocument doc;
  doc["gateway_id"] = gatewayId_;
  doc["sent_at"]    = now;

  JsonArray readings = doc["readings"].to<JsonArray>();
  for (size_t i = 0; i < count; i++) {
    const Reading    &r = queue_.at(i);
    const SensorInfo &s = sensors_[r.sensor];
    char num[24];
    formatReadingValue(r.value, r.digits, num, sizeof(num));
    JsonObject o = readings.add<JsonObject>();
    o["meter_id"]    = s.key;
    o["measured_at"] = r.measuredAt;
    o["value"]       = serialized(String(num));
    o["unit"]        = s.unit;
  }

  bool anyNew = false;
  for (size_t i = 0; i < sensors_.size(); i++) {
    inUpload_[i] = !reported_[i];
    anyNew = anyNew || inUpload_[i];
  }
  if (anyNew) {
    JsonArray list = doc["sensors"].to<JsonArray>();
    for (size_t i = 0; i < sensors_.size(); i++) {
      if (inUpload_[i]) addSensorDescription(list, sensors_[i]);
    }
  }

  JsonObject diag = doc["diagnostics"].to<JsonObject>();
  diag["firmware"]         = cfg_.firmware;
  diag["uptime_s"]         = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
  diag["reset_reason"]     = resetReasonText();
  diag["free_heap"]        = static_cast<uint32_t>(esp_get_free_heap_size());
  diag["min_free_heap"]    = static_cast<uint32_t>(esp_get_minimum_free_heap_size());
  if (net.hasRssi) diag["wifi_rssi"] = net.rssi;
  diag["queue"]            = static_cast<uint32_t>(queue_.size());
  diag["dropped"]          = queue_.dropped();
  diag["uploads_ok"]       = uploadsOk_;
  diag["uploads_failed"]   = uploadsFailed_;
  diag["uploads_rejected"] = uploadsRejected_;
  diag["last_post_ms"]     = lastPostMs_;

  std::string body;
  serializeJson(doc, body);     // genau einmal: diese Bytes werden signiert und gesendet
  return body;
}

void EnergieloggerClient::upload(const NetStatus &net) {
  const size_t   count = min(queue_.size(), cfg_.maxReadingsPerUpload);
  const uint32_t now   = static_cast<uint32_t>(time(nullptr));
  const std::string body = buildUploadBody(count, now, net);

  const std::string timestamp = std::to_string(now);
  const std::string nonce     = randomNonce();
  const std::string signature = hmacSha256Hex(secret_, timestamp + "\n" + nonce + "\n" + body);
  if (signature.empty()) {
    Serial.println("[EL] FEHLER: Signatur konnte nicht berechnet werden");
    uploadsFailed_++;
    failures_++;
    waitForRetry(failures_, UPLOAD_RETRY_S, 5);
    return;
  }

  const HttpHeader headers[] = {{"X-Gateway-Id", gatewayId_.c_str()},
                                {"X-Timestamp", timestamp.c_str()},
                                {"X-Nonce", nonce.c_str()},
                                {"X-Signature", signature.c_str()}};
  std::string response;
  const uint32_t t0 = millis();
  const int code = http_.post(ingestUrl_, headers, 4, body, response);
  lastPostMs_ = millis() - t0;
  handleUploadResponse(code, response, count);
}

void EnergieloggerClient::handleUploadResponse(int code, const std::string &response, size_t count) {
  JsonDocument doc;
  deserializeJson(doc, response.c_str());
  const char *error = doc["error"] | "";

  if (code == 200) {
    queue_.popFront(count);
    for (size_t i = 0; i < sensors_.size(); i++) reported_[i] = reported_[i] || inUpload_[i];
    uploadsOk_++;
    failures_     = 0;
    unauthorized_ = 0;
    JsonArrayConst rejected = doc["rejected"].as<JsonArrayConst>();
    Serial.printf("[EL] Upload: %u Werte, HTTP 200 (%lu ms), angenommen %d, doppelt %d, abgelehnt %u, offen %u\n",
                  (unsigned)count, (unsigned long)lastPostMs_, doc["accepted"] | 0, doc["duplicates"] | 0,
                  (unsigned)rejected.size(), (unsigned)queue_.size());
    bool unassigned = false;
    uint8_t shown = 0;
    for (JsonObjectConst r : rejected) {
      const char *reason = r["reason"] | "?";
      if (strcmp(reason, "sensor_unassigned") == 0) {
        unassigned = true;
      } else if (shown < 5) {
        Serial.printf("[EL]   abgelehnt: %s (%s)\n", (const char *)(r["meter_id"] | "?"), reason);
        shown++;
      }
    }
    if (unassigned) Serial.println("[EL]   Hinweis: neue Sensoren im Portal am Gateway übernehmen (sensor_unassigned)");
    // Rückstau zügig abbauen, aber innerhalb des Rate-Limits (30 Uploads in 10 Minuten).
    nextMs_ = millis() + (queue_.size() >= cfg_.maxReadingsPerUpload ? cfg_.catchupGapMs : intervalS_ * 1000UL);
    return;
  }

  if (code == 401 && strcmp(error, "unauthorized") == 0 && ++unauthorized_ >= UNAUTHORIZED_LIMIT) {
    Serial.printf("[EL] Secret ungültig (%d x 401) – melde neu an, Werte bleiben erhalten\n", UNAUTHORIZED_LIMIT);
    uploadsFailed_++;
    forgetRegistration(false);
    return;
  }

  if (code == 400 || code == 409 || code == 413 || code == 422) {
    // Endgültig ungültig (bzw. 409: schon angenommen): sonst blockierte der Batch die Warteschlange.
    Serial.printf("[EL] Upload verworfen, HTTP %d: %.200s\n", code, response.c_str());
    queue_.popFront(count);
    uploadsRejected_++;
    failures_ = 0;
    nextMs_   = millis() + intervalS_ * 1000UL;
    return;
  }

  // 401, 403 (Firewall des Hosters), 429, 5xx, Netz: Werte behalten, später erneut.
  if (code > 0) Serial.printf("[EL] Upload: HTTP %d: %.200s – Werte bleiben erhalten\n", code, response.c_str());
  if (code == 401 && strcmp(error, "clock_skew") == 0) Serial.println("[EL]   Uhr weicht mehr als 300 s ab (NTP prüfen)");
  uploadsFailed_++;
  failures_++;
  waitForRetry(failures_, UPLOAD_RETRY_S, 5);
}

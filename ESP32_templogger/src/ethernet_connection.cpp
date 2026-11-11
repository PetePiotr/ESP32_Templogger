// =============================================================================
//  ethernet_connection.cpp  –  Implementierung von EthernetConnection
// =============================================================================
#include "ethernet_connection.h"
#include <ETH.h>
#include <WiFi.h>
#include <time.h>

// Hinweis zu häufigen Gründen, warum die WLAN-Verbindung scheitert.
static const char *disconnectHint(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
      return "SSID nicht gefunden – Schreibweise prüfen; der ESP32-C6 kann nur 2,4 GHz";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
      return "Verschlüsselung passt nicht – WPA2/WPA3-Enterprise (z. B. eduroam) wird nicht unterstützt";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
      return "Signal zu schwach";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "Anmeldung abgelehnt – Passwort prüfen";
    default:
      return "";
  }
}

void EthernetConnection::begin() {
  if (cfg_.wifiEnabled && strlen(cfg_.wifiSsid) == 0) {
    Serial.println("[NET] FEHLER: WLAN-Fallback aktiviert, aber WIFI_SSID ist leer – Fallback aus");
    cfg_.wifiEnabled = false;
  }
  if (cfg_.wifiEnabled) {
    Serial.printf("[NET] WLAN-Fallback aktiv: SSID \"%s\", nach %lu s ohne Ethernet\n",
                  cfg_.wifiSsid, (unsigned long)(cfg_.wifiFallbackDelayMs / 1000));
  }
  ethLastUpMs_ = millis();

  // Ereignisse an diese Instanz weiterleiten (Lambda bindet `this`).
  Network.onEvent([this](arduino_event_id_t event, arduino_event_info_t info) { onEvent(event, info); });
  if (!ETH.begin()) {
    Serial.println("[NET] FEHLER: ETH.begin() fehlgeschlagen – Board-Auswahl prüfen");
  }
}

void EthernetConnection::loop() {
  // NTP erst starten, wenn eine Verbindung besteht. SNTP synchronisiert danach
  // selbstständig periodisch nach.
  if (isOnline() && !ntpStarted_) {
    configTzTime(cfg_.timezone, cfg_.ntpServer1, cfg_.ntpServer2);
    ntpStarted_ = true;
    Serial.println("[NET] NTP-Synchronisation gestartet");
  }
  if (cfg_.wifiEnabled) updateWifiFallback();
}

void EthernetConnection::updateWifiFallback() {
  const uint32_t now = millis();
  if (ethUp_) {
    ethLastUpMs_   = now;
    wifiAttempted_ = false;
    if (wifiActive_) stopWifi();
    return;
  }
  if (wifiUp_ || now - ethLastUpMs_ < cfg_.wifiFallbackDelayMs) return;
  if (wifiAttempted_) {
    const uint32_t since = now - wifiLastAttemptMs_;
    // Ein laufender Aufbau (Anmeldung, DHCP) wird nicht unterbrochen; erst wenn er
    // ATTEMPT_TIMEOUT_MS ohne Ergebnis hängt, gilt er als gescheitert.
    if (wifiBusy_ && since < ATTEMPT_TIMEOUT_MS) return;
    if (!wifiBusy_ && since < cfg_.wifiRetryMs) return;
    if (wifiBusy_ && wifiEvents_.load() == eventsAtAttempt_) {
      Serial.println("[NET] WARNUNG: keine Rückmeldung vom WLAN-Modul (ESP32-C6) seit dem letzten Versuch");
    }
  }
  startWifi();
}

// Kann beim ersten Aufruf blockieren (Start von ESP-Hosted); antwortet der ESP32-C6
// gar nicht, greift im schlimmsten Fall der Task-Watchdog.
void EthernetConnection::startWifi() {
  const bool retry = wifiAttempted_;
  wifiLastAttemptMs_ = millis();
  wifiAttempted_     = true;
  Serial.printf("[NET] Kein Ethernet – %s WLAN \"%s\"\n", retry ? "neuer Versuch mit" : "verbinde", cfg_.wifiSsid);

  if (!wifiPinsSet_) {
    const int8_t *p = cfg_.wifiSdioPins;
    if (!WiFi.setPins(p[0], p[1], p[2], p[3], p[4], p[5], p[6])) {
      Serial.println("[NET] WARNUNG: SDIO-Pins für den ESP32-C6 nicht gesetzt");
    }
    WiFi.persistent(false);     // Zugangsdaten nicht bei jedem Start in den Flash schreiben
    WiFi.setHostname(cfg_.hostname);
    wifiPinsSet_ = true;
  }

  // Hing der letzte Aufbau ohne Ergebnis, ihn beenden; sonst lehnt der Treiber das
  // neue connect mit 0x3007 ESP_ERR_WIFI_CONN ab.
  if (retry && wifiBusy_) {
    wifiActive_ = false;        // die eigene Trennung nicht als Fehlergrund melden
    WiFi.disconnect();
  }

  wifiActive_ = true;           // vor begin(): Ereignisse können sofort eintreffen
  wifiBusy_   = true;
  eventsAtAttempt_ = wifiEvents_.load();
  if (WiFi.begin(cfg_.wifiSsid, cfg_.wifiPassword) == WL_CONNECT_FAILED) {
    // wifiActive_ bleibt gesetzt: Der Treiber kann trotzdem noch verbinden (z. B. sein
    // automatischer zweiter Versuch); dann soll die IP angenommen werden.
    wifiBusy_ = false;
    Serial.printf("[NET] WLAN-Verbindung nicht gestartet, neuer Versuch in %lu s\n",
                  (unsigned long)(cfg_.wifiRetryMs / 1000));
  }
  reportCoprocessorVersion();
}

// Firmware-Stand des ESP32-C6 einmal melden. Der Core fragt ihn beim Start von
// ESP-Hosted selbst ab; hier wird nur das Ergebnis gelesen (keine neue Anfrage).
void EthernetConnection::reportCoprocessorVersion() {
  if (versionReported_) return;
  versionReported_ = true;
  uint32_t hMaj, hMin, hPat, sMaj, sMin, sPat;
  hostedGetHostVersion(&hMaj, &hMin, &hPat);
  hostedGetSlaveVersion(&sMaj, &sMin, &sPat);
  if (sMaj == 0 && sMin == 0 && sPat == 0) {
    Serial.printf("[NET] WARNUNG: Firmware des ESP32-C6 nicht abfragbar – vermutlich älter als ESP-Hosted %lu.%lu.%lu "
                  "des Cores; schlägt WLAN fehl, die C6-Firmware aktualisieren\n",
                  (unsigned long)hMaj, (unsigned long)hMin, (unsigned long)hPat);
  } else if (sMaj != hMaj || sMin != hMin) {
    Serial.printf("[NET] WARNUNG: ESP32-C6 hat ESP-Hosted %lu.%lu.%lu, der Core erwartet %lu.%lu.%lu\n",
                  (unsigned long)sMaj, (unsigned long)sMin, (unsigned long)sPat,
                  (unsigned long)hMaj, (unsigned long)hMin, (unsigned long)hPat);
  } else {
    Serial.printf("[NET] ESP32-C6: ESP-Hosted %lu.%lu.%lu\n",
                  (unsigned long)sMaj, (unsigned long)sMin, (unsigned long)sPat);
  }
}

void EthernetConnection::stopWifi() {
  Serial.println("[NET] Ethernet wieder da – WLAN wird getrennt");
  wifiActive_ = false;
  WiFi.disconnect();
  wifiUp_   = false;
  wifiBusy_ = false;
}

int EthernetConnection::wifiRssi() const { return usingWifi() ? WiFi.RSSI() : 0; }

bool EthernetConnection::timeValid() const {
  return time(nullptr) > 1704067200;   // 01.01.2024 00:00 UTC
}

// Läuft im Event-Task des Cores, nicht in loop().
void EthernetConnection::onEvent(arduino_event_id_t event, const arduino_event_info_t &info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(cfg_.hostname);
      Serial.println("[NET] Ethernet gestartet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[NET] Ethernet-Link aktiv");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.printf("[NET] Ethernet IP: %s\n", ETH.localIP().toString().c_str());
      ethUp_ = true;
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("[NET] Ethernet getrennt");
      ethUp_ = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      wifiEvents_++;
      if (wifiActive_) Serial.println("[NET] WLAN verbunden, warte auf IP-Adresse (DHCP)");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiEvents_++;
      if (!wifiActive_) break;   // verspätetes Ereignis nach stopWifi()
      Serial.printf("[NET] WLAN IP: %s (%d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      wifiUp_   = true;
      wifiBusy_ = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      wifiEvents_++;
      if (wifiUp_) Serial.println("[NET] WLAN: IP-Adresse verloren");
      wifiUp_ = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      wifiEvents_++;
      const uint8_t reason = info.wifi_sta_disconnected.reason;
      if (wifiActive_ && reason != WIFI_REASON_ASSOC_LEAVE) {   // ASSOC_LEAVE = eigene Trennung
        const char *hint = disconnectHint(reason);
        Serial.printf("[NET] WLAN %s: Grund %u (%s)%s%s\n", wifiUp_ ? "getrennt" : "nicht verbunden", reason,
                      WiFi.STA.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)), *hint ? " – " : "", hint);
      }
      wifiUp_   = false;
      wifiBusy_ = false;        // Versuch beendet; der nächste kommt nach wifiRetryMs
      break;
    }
    default:
      break;
  }
}

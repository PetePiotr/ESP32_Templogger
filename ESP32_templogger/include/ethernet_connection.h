// =============================================================================
//  ethernet_connection.h  –  Ethernet-Verbindung, WLAN-Fallback und NTP-Zeit
// -----------------------------------------------------------------------------
//  ETH.begin() ohne Parameter nutzt die Werte der Board-Variante
//  "ESP32P4 Dev Module" (Arduino-Core 3.2.0): PHY IP101, Adresse 1,
//  MDC GPIO31, MDIO GPIO52, Reset GPIO51, RMII-Takt extern an GPIO50.
//  Diese Werte stimmen mit dem Waveshare-Wiki zum ESP32-P4-WIFI6-POE-ETH überein.
//  (Name bewusst nicht "NetworkManager": diese Klasse gibt es im Core bereits.)
//
//  WLAN-Fallback (optional): Der ESP32-P4 hat kein eigenes Funkmodul, WLAN läuft
//  über den ESP32-C6 auf dem Board (ESP-Hosted, SDIO). Ethernet hat Vorrang: WLAN
//  startet erst nach wifiFallbackDelayMs ohne Ethernet und wird getrennt, sobald
//  Ethernet wieder eine IP hat. Beides zugleich würde die Standardroute auf WLAN legen.
//
//  Netzwerkereignisse kommen asynchron aus dem Event-Task des Cores; die
//  Methode onEvent() setzt deshalb nur atomare Flags.
// =============================================================================
#pragma once
#include <Arduino.h>
#include <atomic>
#include <Network.h>   // arduino_event_id_t

class EthernetConnection {
public:
  struct Config {
    const char *hostname;
    const char *ntpServer1;
    const char *ntpServer2;
    const char *timezone;   // POSIX-TZ, nur für Log-Ausgaben (gesendet wird UTC)
    bool         wifiEnabled;
    const char  *wifiSsid;
    const char  *wifiPassword;
    const int8_t *wifiSdioPins;        // CLK, CMD, D0, D1, D2, D3, RESET
    uint32_t     wifiFallbackDelayMs;  // so lange ohne Ethernet, bis WLAN startet
    uint32_t     wifiRetryMs;          // Abstand der Versuche, solange keine IP
  };

  explicit EthernetConnection(const Config &cfg) : cfg_(cfg) {}

  void begin();     // Ethernet starten
  void loop();      // regelmäßig aufrufen: NTP, sobald Netz da ist, und WLAN-Fallback

  bool isOnline() const { return ethUp_.load() || wifiUp_.load(); }
  bool usingWifi() const { return !ethUp_.load() && wifiUp_.load(); }
  int  wifiRssi() const;    // dBm, nur sinnvoll bei usingWifi()
  // Uhrzeit gilt als gültig, sobald sie nach dem 01.01.2024 liegt (NTP erfolgreich).
  bool timeValid() const;

private:
  void onEvent(arduino_event_id_t event, const arduino_event_info_t &info);
  void updateWifiFallback();
  void startWifi();
  void stopWifi();
  void reportCoprocessorVersion();

  // So lange darf ein Verbindungsaufbau (Anmeldung + DHCP) dauern, bevor er als
  // gescheitert gilt und neu gestartet wird.
  static constexpr uint32_t ATTEMPT_TIMEOUT_MS = 30000;

  Config            cfg_;
  std::atomic<bool> ethUp_{false};
  std::atomic<bool> wifiUp_{false};
  std::atomic<bool> wifiActive_{false};      // WLAN gestartet und nicht wieder getrennt
  bool              ntpStarted_ = false;
  bool              wifiAttempted_ = false;  // seit dem Ethernet-Ausfall schon versucht
  bool              wifiPinsSet_ = false;    // SDIO-Pins nur vor dem ersten Start setzbar
  bool              versionReported_ = false;
  std::atomic<uint32_t> wifiEvents_{0};      // WLAN-Ereignisse seit Start (Rückmeldung vom C6)
  uint32_t          eventsAtAttempt_ = 0;
  std::atomic<bool> wifiBusy_{false};        // Verbindungsaufbau läuft (bis IP oder Trennung)
  uint32_t          ethLastUpMs_ = 0;        // zuletzt Ethernet gesehen (ab Start)
  uint32_t          wifiLastAttemptMs_ = 0;
};

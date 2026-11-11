// =============================================================================
//  sim.h  –  Steuerung der Host-Simulation aus den Tests heraus
// -----------------------------------------------------------------------------
//  Alles, was auf dem echten Board Hardware oder Netz ist, liegt hier als
//  einstellbarer Zustand vor:
//    - Uhr:        millis() startet bei reset(startMs); delay() rückt sie vor
//    - NTP:        Zeit gilt erst nach syncDelayMs ab configTzTime() als synchron
//    - Ethernet:   ethLinkUp() / ethLinkDown() lösen die Core-Ereignisse aus
//    - WLAN:       wifi() mit Treiber (ESP32-C6) und Access Point, wifiApLost()
//    - 1-Wire:     addSensor(pin, ...) legt einen DS18B20 an einen Bus
//    - Modbus:     gateway() mit Zählern (Unit-ID -> Register) und Fehlern
//    - HTTP:       jeder POST landet in httpRequests(); Antwort über httpHandler()
//    - Watchdog:   wdt() merkt sich die längste Lücke zwischen zwei Resets
//  reset() setzt den gesamten Zustand zurück (in setUp() jedes Tests aufrufen).
// =============================================================================
#pragma once
#include "Arduino.h"
#include "ModbusClientTCP.h"
#include "Network.h"

namespace sim {

// --- Uhr, Log ------------------------------------------------------------------
void reset(uint32_t startMs = 1000);
std::string &serialLog();
bool logContains(const char *text);
void setEcho(bool on);   // Serial-Ausgaben zusätzlich auf stdout

// --- NTP -----------------------------------------------------------------------
struct Ntp {
  bool     available   = true;
  uint32_t syncDelayMs = 2000;        // Dauer von configTzTime() bis zur gültigen Zeit
  time_t   epochAtBoot = 1791961200;  // Unix-Zeit zum Simulationsstart (14.10.2026)
  bool     configured  = false;
  uint32_t configuredAtMs = 0;
  bool     synced      = false;
  std::string server1, server2;
};
Ntp &ntp();
uint32_t bootMs();

// --- Ethernet --------------------------------------------------------------------
struct Eth {
  bool        beginOk = true;
  bool        begun   = false;
  std::string hostname;
  std::vector<NetworkEventFuncCb> handlers;
};
Eth &eth();
void ethFire(arduino_event_id_t e);
void ethLinkUp();     // CONNECTED + GOT_IP
void ethLinkDown();   // DISCONNECTED

// --- WLAN (ESP32-C6 über ESP-Hosted) ----------------------------------------------
struct Wifi {
  bool        driverOk     = true;    // false: ESP-Hosted startet nicht
  bool        apAvailable  = true;    // false: begin() läuft, aber keine Verbindung
  bool        c6UpdateDue  = false;   // hostedHasUpdate()
  bool        connected    = false;
  int         beginCalls   = 0;
  int         disconnects  = 0;
  int         setPinsCalls = 0;
  int8_t      pins[7]      = {};
  std::string ssid, password, hostname;
};
Wifi &wifi();
void wifiApLost();    // DISCONNECTED

// --- Task-Watchdog ---------------------------------------------------------------
struct Wdt {
  bool     configured = false;
  uint32_t timeoutMs  = 0;
  bool     taskAdded  = false;
  uint32_t resets     = 0;
  uint32_t lastResetMs = 0;
  uint32_t maxGapMs   = 0;
  bool     fired      = false;   // Watchdog wäre abgelaufen -> Neustart
};
Wdt &wdt();

// --- HTTP ------------------------------------------------------------------------
struct HttpRequest {
  std::string url, contentType, apiKey, body;
  bool        tls = false, insecure = false;
  uint32_t    atMs = 0;           // Zeitpunkt des POST (simulierte millis)
};
struct HttpResponse {
  int         code = 200;
  std::string body = "{\"accepted\":true}";
  uint32_t    connectMs = 20;     // Dauer des Verbindungsaufbaus
  uint32_t    latencyMs = 80;     // Zeit bis zur Antwort nach dem Senden
  bool        refused = false;    // Server nicht erreichbar
};
std::vector<HttpRequest> &httpRequests();
std::function<HttpResponse(const HttpRequest &)> &httpHandler();
// Jeden gesendeten Body zusätzlich als Zeile in diese Datei schreiben ("" = aus).
void setHttpDump(const char *path);

// --- Modbus-Gateway --------------------------------------------------------------
enum class Order { HIGH_FIRST, LOW_FIRST };
struct ModbusMeterSim {
  bool     responds = true;
  Error    silentError = GATEWAY_TARGET_NO_RESP;  // was das Gateway meldet, wenn der Zähler schweigt
  bool     shortResponse = false;                  // Antwort um 1 Byte zu kurz
  std::map<uint32_t, uint16_t> regs;               // Schlüssel: FC << 16 | Adresse

  void setU16(uint8_t fc, uint16_t addr, uint16_t v) { regs[(uint32_t(fc) << 16) | addr] = v; }
  void setU32(uint8_t fc, uint16_t addr, uint32_t v, Order o = Order::HIGH_FIRST);
  void setFloat(uint8_t fc, uint16_t addr, float v, Order o = Order::HIGH_FIRST);
};
struct ModbusRequestLog { uint8_t unit; uint8_t fc; uint16_t addr; uint16_t count; uint32_t atMs; };
struct Gateway {
  bool      reachable = true;
  uint32_t  latencyMs = 25;          // Antwortzeit je Anfrage (TCP + RS485)
  uint32_t  rtuTimeoutMs = 300;      // so lange wartet das Gateway auf einen stummen Zähler
  bool      begun = false;
  IPAddress target;
  uint16_t  port = 0;
  std::map<uint8_t, ModbusMeterSim> meters;
  std::vector<ModbusRequestLog> requests;
};
Gateway &gateway();

// --- 1-Wire / DS18B20 ------------------------------------------------------------
struct SimDS18B20 {
  uint8_t  rom[8] = {};
  double   tempC = 20.0;             // tatsächliche Temperatur am Fühler
  bool     connected = true;         // false = abgeklemmt / Leitungsbruch
  uint8_t  eepromConfig = 0x7F;      // Auflösung im EEPROM (0x1F=9 bit ... 0x7F=12 bit)
  uint32_t convMs12 = 620;           // echte Wandlungszeit bei 12 bit (Datenblatt: max. 750)
  bool     clone = false;            // Nachbau: Byte 6 kennzeichnet den Einschaltwert NICHT

  // Fehlerinjektion
  int scratchCrcErrors = 0;          // die nächsten n Scratchpad-Lesungen mit Bitfehler
  int porReads = 0;                  // die nächsten n Lesungen zeigen den Einschaltwert (Spannungseinbruch)
  int romErrorsInSearch = 0;         // die nächsten n Suchläufe brechen an diesem Fühler ab (CRC)

  // interner Zustand
  int16_t  raw = 0x0550;             // Temperaturregister (85 °C nach dem Einschalten)
  bool     por = true;               // noch keine Wandlung seit dem Einschalten
  uint8_t  th = 0x4B, tl = 0x46, config = 0x7F;
  bool     converting = false;
  uint32_t convStartMs = 0;
  int      configWrites = 0;         // Schreibzugriffe auf das Konfigurationsregister

  uint8_t  resolution() const;
  uint32_t conversionMs() const;
  void     update(uint32_t nowMs);   // Wandlung abschließen, falls fertig
  void     scratchpad(uint8_t out[9]);
  std::string romHex() const;
};
struct OneWireBus {
  bool shorted = false;              // Datenleitung auf GND (z. B. Kurzschluss, falsche Verdrahtung)
  std::deque<SimDS18B20> sensors;    // deque: Referenzen bleiben beim Anfügen gültig
};
OneWireBus &bus(uint8_t pin);
// Sensor mit gültiger ROM-ID (Familie 0x28, Seriennummer, CRC) anlegen.
SimDS18B20 &addSensor(uint8_t pin, uint64_t serial, double tempC);
uint8_t crc8(const uint8_t *data, uint8_t len);
// Wert, den ein DS18B20 bei 12 bit für tempC liefert (Vielfaches von 1/16 °C).
double quantize12(double tempC);

}  // namespace sim

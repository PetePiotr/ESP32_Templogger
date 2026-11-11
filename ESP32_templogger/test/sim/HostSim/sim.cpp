// =============================================================================
//  sim.cpp  –  Implementierung der Host-Simulation (siehe sim.h)
// =============================================================================
#include "sim.h"
#include "ETH.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "OneWire.h"
#include "esp_task_wdt.h"

SimSerial      Serial;
NetworkManager Network;
ETHClass       ETH;

namespace sim {

// --- Zustand -------------------------------------------------------------------
static uint32_t g_ms     = 0;
static uint32_t g_bootMs = 0;
static std::string g_log;
static bool     g_echo   = false;
static Ntp      g_ntp;
static Eth      g_eth;
static Wdt      g_wdt;
static Gateway  g_gateway;
static std::map<uint8_t, OneWireBus> g_buses;
static std::vector<HttpRequest> g_http;
static std::function<HttpResponse(const HttpRequest &)> g_httpHandler;
static std::string g_httpDump;

void reset(uint32_t startMs) {
  g_ms = g_bootMs = startMs;
  g_log.clear();
  g_ntp = Ntp();
  g_eth = Eth();
  wifi() = Wifi();
  g_wdt = Wdt();
  // Der Arduino-Core startet den Task-Watchdog bereits beim Booten (Standard 5 s).
  g_wdt.configured = true;
  g_wdt.timeoutMs  = 5000;
  g_gateway = Gateway();
  g_buses.clear();
  g_http.clear();
  g_httpHandler = nullptr;
}

uint32_t millisNow() { return g_ms; }
uint32_t bootMs() { return g_bootMs; }

void advance(uint32_t ms) {
  g_ms += ms;
  if (g_wdt.taskAdded && g_ms - g_wdt.lastResetMs > g_wdt.timeoutMs) g_wdt.fired = true;
}

void onYield() { advance(1); }

std::string &serialLog() { return g_log; }
bool logContains(const char *text) { return g_log.find(text) != std::string::npos; }
void setEcho(bool on) { g_echo = on; }
void setHttpDump(const char *path) { g_httpDump = path ? path : ""; }

// --- NTP -----------------------------------------------------------------------
Ntp &ntp() { return g_ntp; }

void configTz(const char *, const char *s1, const char *s2, const char *) {
  g_ntp.configured     = true;
  g_ntp.configuredAtMs = g_ms;
  g_ntp.server1 = s1 ? s1 : "";
  g_ntp.server2 = s2 ? s2 : "";
}

time_t unixTime(time_t *out) {
  if (!g_ntp.synced && g_ntp.configured && g_ntp.available &&
      g_ms - g_ntp.configuredAtMs >= g_ntp.syncDelayMs) {
    g_ntp.synced = true;
  }
  // Ohne NTP zählt die Uhr des ESP32 ab 1970 (Sekunden seit dem Start).
  const time_t sinceBoot = static_cast<time_t>((g_ms - g_bootMs) / 1000);
  const time_t t = g_ntp.synced ? g_ntp.epochAtBoot + sinceBoot : sinceBoot;
  if (out) *out = t;
  return t;
}

// --- Ethernet ------------------------------------------------------------------
Eth &eth() { return g_eth; }

void ethFire(arduino_event_id_t e) {
  for (auto &h : g_eth.handlers) h(e, arduino_event_info_t{});
}
void ethLinkUp() {
  ethFire(ARDUINO_EVENT_ETH_CONNECTED);
  ethFire(ARDUINO_EVENT_ETH_GOT_IP);
}
void ethLinkDown() { ethFire(ARDUINO_EVENT_ETH_DISCONNECTED); }

// --- WLAN --------------------------------------------------------------------------
Wifi &wifi() {
  static Wifi w;
  return w;
}
void wifiApLost() {
  wifi().connected = false;
  ethFire(ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

// --- Watchdog --------------------------------------------------------------------
Wdt &wdt() { return g_wdt; }

// --- HTTP ------------------------------------------------------------------------
std::vector<HttpRequest> &httpRequests() { return g_http; }
std::function<HttpResponse(const HttpRequest &)> &httpHandler() { return g_httpHandler; }

// --- Modbus ----------------------------------------------------------------------
Gateway &gateway() { return g_gateway; }

void ModbusMeterSim::setU32(uint8_t fc, uint16_t addr, uint32_t v, Order o) {
  const uint16_t hi = static_cast<uint16_t>(v >> 16), lo = static_cast<uint16_t>(v & 0xFFFF);
  setU16(fc, addr,     o == Order::HIGH_FIRST ? hi : lo);
  setU16(fc, addr + 1, o == Order::HIGH_FIRST ? lo : hi);
}
void ModbusMeterSim::setFloat(uint8_t fc, uint16_t addr, float v, Order o) {
  uint32_t u;
  memcpy(&u, &v, sizeof(u));
  setU32(fc, addr, u, o);
}

// --- 1-Wire ----------------------------------------------------------------------
uint8_t crc8(const uint8_t *data, uint8_t len) {   // Dallas/Maxim, Polynom x^8+x^5+x^4+1
  uint8_t crc = 0;
  while (len--) {
    uint8_t in = *data++;
    for (int i = 0; i < 8; i++) {
      const uint8_t mix = (crc ^ in) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      in >>= 1;
    }
  }
  return crc;
}

double quantize12(double tempC) { return std::lround(tempC * 16.0) / 16.0; }

OneWireBus &bus(uint8_t pin) { return g_buses[pin]; }

SimDS18B20 &addSensor(uint8_t pin, uint64_t serial, double tempC) {
  SimDS18B20 s;
  s.rom[0] = 0x28;
  for (int i = 0; i < 6; i++) s.rom[1 + i] = static_cast<uint8_t>(serial >> (8 * i));
  s.rom[7] = crc8(s.rom, 7);
  s.tempC  = tempC;
  s.config = s.eepromConfig;
  g_buses[pin].sensors.push_back(s);
  return g_buses[pin].sensors.back();
}

uint8_t SimDS18B20::resolution() const { return 9 + ((config >> 5) & 0x03); }

uint32_t SimDS18B20::conversionMs() const { return convMs12 >> (12 - resolution()); }

void SimDS18B20::update(uint32_t nowMs) {
  if (!converting || nowMs - convStartMs < conversionMs()) return;
  converting = false;
  long r = std::lround(tempC * 16.0);
  r = std::max(-880L, std::min(2000L, r));                 // Messbereich -55 .. +125 °C
  const int drop = 12 - resolution();                      // nicht genutzte LSBs = 0
  r &= ~((1L << drop) - 1);
  raw = static_cast<int16_t>(r);
  por = false;
}

void SimDS18B20::scratchpad(uint8_t out[9]) {
  if (porReads > 0) {          // Spannungseinbruch: Sensor ist neu gestartet
    porReads--;
    raw = 0x0550;
    por = true;
    config = eepromConfig;
    converting = false;
  }
  const uint8_t lsb = static_cast<uint8_t>(raw & 0xFF);
  out[0] = lsb;
  out[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
  out[2] = th;
  out[3] = tl;
  out[4] = config;
  out[5] = 0xFF;
  // Original-DS18B20: Byte 6 = 0x0C nach dem Einschalten, danach 0x10 - (LSB & 0x0F).
  // Daran erkennt DallasTemperature den Einschaltwert. Viele Nachbauten tun das nicht.
  out[6] = (por && !clone) ? 0x0C : static_cast<uint8_t>(0x10 - (lsb & 0x0F));
  out[7] = 0x10;
  out[8] = crc8(out, 8);
  if (scratchCrcErrors > 0) {   // Störung auf der Leitung: ein Bit kippt
    scratchCrcErrors--;
    out[0] ^= 0x04;
  }
}

std::string SimDS18B20::romHex() const {
  char buf[17];
  for (int i = 0; i < 8; i++) snprintf(buf + 2 * i, 3, "%02X", rom[i]);
  return buf;
}

}  // namespace sim

// =============================================================================
//  Ersatzklassen
// =============================================================================

// --- Serial --------------------------------------------------------------------
int SimSerial::printf(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  print(buf);
  return n;
}

void SimSerial::print(const char *s) {
  sim::g_log += s;
  if (sim::g_echo) fputs(s, stdout);
}

// --- Network / ETH ---------------------------------------------------------------
int NetworkManager::onEvent(NetworkEventFuncCb cb, arduino_event_id_t) {
  sim::g_eth.handlers.push_back(cb);
  return static_cast<int>(sim::g_eth.handlers.size());
}

bool ETHClass::begin() {
  sim::g_eth.begun = true;
  if (sim::g_eth.beginOk) sim::ethFire(ARDUINO_EVENT_ETH_START);
  return sim::g_eth.beginOk;
}
bool ETHClass::setHostname(const char *name) {
  sim::g_eth.hostname = name ? name : "";
  return true;
}
IPAddress ETHClass::localIP() { return IPAddress(192, 168, 1, 77); }

WiFiClass WiFi;
bool WiFiClass::setPins(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst) {
  const int8_t p[7] = {clk, cmd, d0, d1, d2, d3, rst};
  memcpy(sim::wifi().pins, p, sizeof(p));
  sim::wifi().setPinsCalls++;
  return true;
}
bool WiFiClass::setHostname(const char *name) {
  sim::wifi().hostname = name ? name : "";
  return true;
}
wl_status_t WiFiClass::begin(const char *ssid, const char *password) {
  auto &w = sim::wifi();
  w.beginCalls++;
  w.ssid     = ssid;
  w.password = password;
  if (!w.driverOk) return WL_CONNECT_FAILED;
  if (w.apAvailable) {
    w.connected = true;
    sim::ethFire(ARDUINO_EVENT_WIFI_STA_CONNECTED);
    sim::ethFire(ARDUINO_EVENT_WIFI_STA_GOT_IP);
  }
  return WL_DISCONNECTED;
}
bool WiFiClass::disconnect() {
  auto &w = sim::wifi();
  w.disconnects++;
  if (w.connected) sim::wifiApLost();
  return true;
}
IPAddress WiFiClass::localIP() { return IPAddress(192, 168, 2, 77); }
bool hostedHasUpdate() { return sim::wifi().c6UpdateDue; }

// --- Watchdog --------------------------------------------------------------------
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t *cfg) {
  if (sim::g_wdt.configured) return ESP_ERR_INVALID_STATE;
  sim::g_wdt.configured = true;
  sim::g_wdt.timeoutMs  = cfg->timeout_ms;
  return ESP_OK;
}
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t *cfg) {
  if (!sim::g_wdt.configured) return ESP_ERR_INVALID_STATE;
  sim::g_wdt.timeoutMs = cfg->timeout_ms;
  return ESP_OK;
}
esp_err_t esp_task_wdt_add(TaskHandle_t) {
  sim::g_wdt.taskAdded   = true;
  sim::g_wdt.lastResetMs = sim::g_ms;
  return ESP_OK;
}
esp_err_t esp_task_wdt_reset(void) {
  if (!sim::g_wdt.taskAdded) return ESP_ERR_INVALID_STATE;
  const uint32_t gap = sim::g_ms - sim::g_wdt.lastResetMs;
  if (gap > sim::g_wdt.maxGapMs) sim::g_wdt.maxGapMs = gap;
  sim::g_wdt.lastResetMs = sim::g_ms;
  sim::g_wdt.resets++;
  return ESP_OK;
}

// --- HTTPClient --------------------------------------------------------------------
static bool urlUsable(const char *url) {
  return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

bool HTTPClient::begin(NetworkClient &, const char *url) {
  url_ = url ? url : "";
  tls_ = false;
  return urlUsable(url);
}
bool HTTPClient::begin(NetworkClientSecure &client, const char *url) {
  url_      = url ? url : "";
  tls_      = true;
  insecure_ = client.insecure;
  return urlUsable(url);
}
void HTTPClient::addHeader(const String &name, const String &value) {
  headers_[name.str()] = value.str();
}

int HTTPClient::POST(const String &payload) {
  sim::HttpRequest req;
  req.url         = url_;
  req.contentType = headers_["Content-Type"];
  req.apiKey      = headers_["X-Api-Key"];
  req.body        = payload.str();
  req.tls         = tls_;
  req.insecure    = insecure_;
  req.atMs        = sim::g_ms;
  sim::g_http.push_back(req);
  if (!sim::g_httpDump.empty()) {
    if (FILE *f = fopen(sim::g_httpDump.c_str(), "ab")) {
      fprintf(f, "%s\n", req.body.c_str());
      fclose(f);
    }
  }

  const sim::HttpResponse resp = sim::g_httpHandler ? sim::g_httpHandler(req) : sim::HttpResponse();
  const uint32_t connectTimeout = connectTimeoutMs_ > 0 ? static_cast<uint32_t>(connectTimeoutMs_) : 5000;
  if (resp.refused) {
    sim::advance(resp.connectMs < connectTimeout ? resp.connectMs : connectTimeout);
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  if (resp.connectMs > connectTimeout) {
    sim::advance(connectTimeout);
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  sim::advance(resp.connectMs);
  if (resp.latencyMs > timeoutMs_) {
    sim::advance(timeoutMs_);
    return HTTPC_ERROR_READ_TIMEOUT;
  }
  sim::advance(resp.latencyMs);
  respBody_ = resp.body;
  return resp.code;
}

String HTTPClient::errorToString(int code) {
  switch (code) {
    case HTTPC_ERROR_CONNECTION_REFUSED: return String("connection refused");
    case HTTPC_ERROR_READ_TIMEOUT:       return String("read Timeout");
    default:                             return String("unknown");
  }
}

// --- eModbus -------------------------------------------------------------------------
ModbusError::operator const char *() const {
  switch (err_) {
    case SUCCESS:                return "Success";
    case ILLEGAL_DATA_ADDRESS:   return "Illegal data address";
    case GATEWAY_PATH_UNAVAIL:   return "Gateway: Path unavailable";
    case GATEWAY_TARGET_NO_RESP: return "Gateway: Target device failed to respond";
    case TIMEOUT:                return "Timeout";
    case PACKET_LENGTH_ERROR:    return "Packet length error";
    case IP_CONNECTION_FAILED:   return "IP connection failed";
    default:                     return "Error";
  }
}

void ModbusClientTCP::begin(int) { sim::g_gateway.begun = true; }

bool ModbusClientTCP::setTarget(IPAddress host, uint16_t port, uint32_t, uint32_t) {
  sim::g_gateway.target = host;
  sim::g_gateway.port   = port;
  return true;
}

static ModbusMessage errorMessage(uint8_t id, uint8_t fc, Error e) {
  return ModbusMessage({id, static_cast<uint8_t>(fc | 0x80), static_cast<uint8_t>(e)});
}

ModbusMessage ModbusClientTCP::syncRequest(uint32_t, uint8_t serverID, FunctionCode fc,
                                           uint16_t addr, uint16_t count) {
  sim::Gateway &g = sim::g_gateway;
  g.requests.push_back({serverID, static_cast<uint8_t>(fc), addr, count, sim::g_ms});

  if (!g.reachable) {                           // TCP-Verbindung zum Gateway scheitert
    sim::advance(timeoutMs_);
    return errorMessage(serverID, fc, IP_CONNECTION_FAILED);
  }
  auto it = g.meters.find(serverID);
  const bool silent = (it == g.meters.end()) || !it->second.responds;
  if (silent) {                                 // Zähler am RS485-Bus antwortet nicht
    const Error e = (it == g.meters.end()) ? GATEWAY_TARGET_NO_RESP : it->second.silentError;
    if (e == TIMEOUT || g.rtuTimeoutMs + g.latencyMs > timeoutMs_) {
      sim::advance(timeoutMs_);
      return errorMessage(serverID, fc, TIMEOUT);
    }
    sim::advance(g.rtuTimeoutMs + g.latencyMs);
    return errorMessage(serverID, fc, e);
  }

  sim::advance(g.latencyMs);
  sim::ModbusMeterSim &m = it->second;
  std::vector<uint8_t> d = {serverID, static_cast<uint8_t>(fc), static_cast<uint8_t>(2 * count)};
  for (uint16_t i = 0; i < count; i++) {
    auto r = m.regs.find((uint32_t(fc) << 16) | uint16_t(addr + i));
    if (r == m.regs.end()) return errorMessage(serverID, fc, ILLEGAL_DATA_ADDRESS);
    d.push_back(static_cast<uint8_t>(r->second >> 8));
    d.push_back(static_cast<uint8_t>(r->second & 0xFF));
  }
  if (m.shortResponse) d.pop_back();
  return ModbusMessage(d);
}

// --- OneWire (Byte-Ebene, gegen die simulierten DS18B20) ----------------------------
OneWire::OneWire(uint8_t pin) : pin_(pin) {}

sim::OneWireBus &OneWire::bus() { return sim::bus(pin_); }

uint8_t OneWire::crc8(const uint8_t *addr, uint8_t len) { return sim::crc8(addr, len); }

uint8_t OneWire::reset() {
  sim::OneWireBus &b = bus();
  for (auto &s : b.sensors) s.update(sim::g_ms);
  mode_   = Mode::IDLE;
  target_ = NONE;
  if (b.shorted) return 0;
  for (auto &s : b.sensors) if (s.connected) return 1;   // Presence-Puls
  return 0;
}

void OneWire::select(const uint8_t rom[8]) {
  target_ = NONE;
  auto &ss = bus().sensors;
  for (size_t i = 0; i < ss.size(); i++) {
    if (ss[i].connected && memcmp(ss[i].rom, rom, 8) == 0) target_ = static_cast<int>(i);
  }
  mode_ = Mode::AWAIT_FUNCTION;
}

void OneWire::skip() {
  target_ = ALL;
  mode_   = Mode::AWAIT_FUNCTION;
}

void OneWire::write(uint8_t v, uint8_t) {
  auto &ss = bus().sensors;
  auto forTargets = [&](const std::function<void(sim::SimDS18B20 &)> &fn) {
    if (target_ == ALL) {
      for (auto &s : ss) if (s.connected) fn(s);
    } else if (target_ >= 0) {
      fn(ss[target_]);
    }
  };

  if (mode_ == Mode::AWAIT_FUNCTION) {
    switch (v) {
      case 0x44:   // Convert T
        forTargets([](sim::SimDS18B20 &s) { s.converting = true; s.convStartMs = sim::g_ms; });
        mode_ = Mode::CONVERT;
        break;
      case 0xBE: { // Read Scratchpad: Antwort = UND-Verknüpfung aller Sender, ohne Sender 0xFF
        memset(buf_, 0xFF, sizeof(buf_));
        forTargets([&](sim::SimDS18B20 &s) {
          uint8_t sp[9];
          s.update(sim::g_ms);
          s.scratchpad(sp);
          for (int i = 0; i < 9; i++) buf_[i] &= sp[i];
        });
        bufPos_ = 0;
        mode_   = Mode::READ_SCRATCH;
        break;
      }
      case 0x4E:   // Write Scratchpad (TH, TL, Konfiguration)
        bufPos_ = 0;
        mode_   = Mode::WRITE_SCRATCH;
        break;
      case 0x48:   // Copy Scratchpad -> EEPROM
        forTargets([](sim::SimDS18B20 &s) { s.eepromConfig = s.config; });
        mode_ = Mode::IDLE;
        break;
      case 0xB8:   // Recall EEPROM
        forTargets([](sim::SimDS18B20 &s) { s.config = s.eepromConfig; });
        mode_ = Mode::IDLE;
        break;
      case 0xB4:   // Read Power Supply
        mode_ = Mode::READ_POWER;
        break;
      default:
        mode_ = Mode::IDLE;
        break;
    }
    return;
  }
  if (mode_ == Mode::WRITE_SCRATCH) {
    const uint8_t pos = bufPos_++;
    forTargets([&](sim::SimDS18B20 &s) {
      if (pos == 0) s.th = v;
      else if (pos == 1) s.tl = v;
      else if (pos == 2) { s.config = static_cast<uint8_t>((v & 0x60) | 0x1F); s.configWrites++; }
    });
  }
}

void OneWire::write_bytes(const uint8_t *buf, uint16_t count, bool) {
  for (uint16_t i = 0; i < count; i++) write(buf[i]);
}

uint8_t OneWire::read() {
  if (bus().shorted) return 0x00;
  if (mode_ == Mode::READ_SCRATCH && bufPos_ < 9) return buf_[bufPos_++];
  return 0xFF;   // niemand sendet: Pull-up hält die Leitung auf 1
}

void OneWire::read_bytes(uint8_t *buf, uint16_t count) {
  for (uint16_t i = 0; i < count; i++) buf[i] = read();
}

void OneWire::write_bit(uint8_t) {}

uint8_t OneWire::read_bit() {
  if (bus().shorted) return 0;
  if (mode_ == Mode::READ_POWER) return 1;   // externe Versorgung (kein Parasite-Power)
  if (mode_ == Mode::CONVERT) {               // 0 solange noch ein Sensor wandelt
    auto &ss = bus().sensors;
    for (auto &s : ss) {
      s.update(sim::g_ms);
      if (s.connected && s.converting) return 0;
    }
  }
  return 1;
}

bool OneWire::search(uint8_t *newAddr, bool) {
  auto &b = bus();
  if (b.shorted) return false;
  // Reihenfolge der Suche = Reihenfolge der Sensoren am Bus (vereinfachend).
  size_t seen = 0;
  for (auto &s : b.sensors) {
    if (!s.connected) continue;
    if (seen++ < searchPos_) continue;
    // Wie OneWireNg: Ein Bitfehler fällt bei der internen CRC-Prüfung auf und
    // beendet die Suche mit false, die verfälschte ROM-ID wird nie geliefert.
    if (s.romErrorsInSearch > 0) {
      s.romErrorsInSearch--;
      reset_search();
      return false;
    }
    memcpy(newAddr, s.rom, 8);
    searchPos_++;
    return true;
  }
  return false;
}

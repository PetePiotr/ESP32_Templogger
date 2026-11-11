// =============================================================================
//  modbus_meters.cpp  –  Implementierung von ModbusMeters
// -----------------------------------------------------------------------------
//  Es werden synchrone Anfragen (syncRequest) verwendet: einfach nachzu-
//  vollziehen. Antwortet ein Zähler nicht, werden seine übrigen Register im
//  Zyklus übersprungen; ist das Gateway nicht erreichbar, endet der Zyklus.
//
//  Antwortaufbau (eModbus ModbusMessage) für FC 03/04:
//    Byte 0: Server-ID, Byte 1: Funktionscode, Byte 2: Anzahl Datenbytes,
//    ab Byte 3: Registerinhalte, je Register 2 Byte, High-Byte zuerst.
// =============================================================================
#include "modbus_meters.h"
#include <cmath>
#include <esp_task_wdt.h>

// Reihenfolge der automatischen Suche: Werkseinstellungen der Eastron-Zähler zuerst
// (Datenblatt SDM230: 9600; ESPHome-Doku nennt für SDM230M/SDM120M 2400).
static const uint32_t BAUD_CANDIDATES[] = {9600, 2400, 4800, 19200, 38400, 1200};

static const char *serialConfigText(uint32_t c) {
  switch (c) {
    case SERIAL_8N1: return "8N1";
    case SERIAL_8E1: return "8E1";
    case SERIAL_8O1: return "8O1";
    case SERIAL_8N2: return "8N2";
    default:         return "?";
  }
}

uint8_t ModbusMeters::registerCount(RegType t) {
  return (t == RegType::UINT16 || t == RegType::INT16) ? 1 : 2;
}

bool ModbusMeters::decode(const MeterRegister &r, uint16_t w0, uint16_t w1, double &out) {
  const uint16_t hi = (r.order == WordOrder::HIGH_FIRST) ? w0 : w1;
  const uint16_t lo = (r.order == WordOrder::HIGH_FIRST) ? w1 : w0;
  const uint32_t u  = (static_cast<uint32_t>(hi) << 16) | lo;

  double v;
  switch (r.type) {
    case RegType::FLOAT32: { float f; memcpy(&f, &u, sizeof(f)); v = f; break; }
    case RegType::UINT32:  v = static_cast<double>(u);                           break;
    case RegType::INT32:   v = static_cast<double>(static_cast<int32_t>(u));     break;
    case RegType::UINT16:  v = static_cast<double>(w0);                          break;
    case RegType::INT16:   v = static_cast<double>(static_cast<int16_t>(w0));    break;
    default: return false;
  }
  if (std::isnan(v) || std::isinf(v)) return false;   // ungültige Rohwerte verwerfen
  out = v * r.scale;
  return true;
}

// Fehler, bei denen ein einzelner Zähler nicht antwortet.
bool ModbusMeters::isUnreachable(Error e) {
  return e == TIMEOUT || e == GATEWAY_PATH_UNAVAIL || e == GATEWAY_TARGET_NO_RESP;
}

SensorInfo ModbusMeters::sensorInfo(uint16_t index) const {
  const MeterRegister &r = cfg_.registers[index];
  return {keys_[index].c_str(), r.sensorType, r.unit, "modbus", addresses_[index].c_str(),
          cfg_.manufacturer, cfg_.model};
}

void ModbusMeters::begin() {
  keys_.clear();
  addresses_.clear();
  for (size_t i = 0; i < cfg_.registerCount; i++) {
    const std::string id = std::to_string(cfg_.registers[i].meter);
    keys_.push_back("modbus_" + id + "." + cfg_.registers[i].quantity);
    addresses_.push_back(id);
  }
  const bool ok = cfg_.transport == ModbusTransport::RTU ? beginRtu() : beginTcp();
  if (!ok) client_ = nullptr;
}

// --- RTU über TTL-RS485-Wandler ------------------------------------------------------
void ModbusMeters::startRtu(uint32_t baud) {
  // Reihenfolge laut eModbus: Puffer setzen, dann UART starten, dann Client.
  // begin() beendet einen schon laufenden Client-Task selbst.
  Serial.flush();               // Log-Ausgaben vor dem Umkonfigurieren vollständig senden
  Serial1.end();
  RTUutils::prepareHardwareSerial(Serial1);
  Serial1.begin(baud, cfg_.serialConfig, cfg_.rxPin, cfg_.txPin);
  rtuClient_.setTimeout(cfg_.timeoutMs);
  rtuClient_.begin(Serial1);
  baud_ = baud;
}

// Je Baudrate einmal das erste Register abfragen; die erste mit Antwort gilt.
uint32_t ModbusMeters::findBaudRate() {
  if (cfg_.registerCount == 0) return 0;
  const MeterRegister &probe = cfg_.registers[0];
  Serial.printf("[MB] Baudrate wird gesucht (Zähler %u, Register 0x%04X) ...\n", probe.meter, probe.address);
  for (uint32_t baud : BAUD_CANDIDATES) {
    startRtu(baud);
    double v;
    const Error err = readRegister(probe, v, true);
    if (err == SUCCESS) {
      Serial.printf("[MB]   %lu Baud: Antwort erhalten -> in config.h RS485_BAUD = %lu eintragen\n",
                    (unsigned long)baud, (unsigned long)baud);
      return baud;
    }
    // Eine Fehlerantwort des Zählers (z. B. ungültige Adresse) zeigt ebenfalls, dass
    // die Baudrate stimmt: Er hat das Telegramm verstanden.
    if (err != TIMEOUT && err != CRC_ERROR && err != PACKET_LENGTH_ERROR) {
      Serial.printf("[MB]   %lu Baud: Zähler antwortet mit Fehler %02X – Baudrate passt, Register prüfen\n",
                    (unsigned long)baud, (int)err);
      return baud;
    }
    Serial.printf("[MB]   %lu Baud: keine gültige Antwort\n", (unsigned long)baud);
  }
  return 0;
}

bool ModbusMeters::beginRtu() {
  // TX/RX (aus der Board-Variante, hier GPIO 37/38) sind UART0 des Seriellen Monitors.
  // Legt man UART1 darauf, verliert die Konsole ihre Leitungen und zeigt nur noch Müll.
  const bool onConsole = cfg_.txPin == TX || cfg_.txPin == RX || cfg_.rxPin == TX || cfg_.rxPin == RX;
  if (onConsole || cfg_.txPin == cfg_.rxPin || cfg_.txPin < 0 || cfg_.rxPin < 0) {
    Serial.printf("[MB] FEHLER: RS485-Pins TX=GPIO%d RX=GPIO%d ungültig%s – Zählerabfrage deaktiviert, "
                  "RS485_TX_PIN/RS485_RX_PIN in config.h ändern (z. B. 22/23)\n",
                  cfg_.txPin, cfg_.rxPin,
                  onConsole ? " (GPIO 37/38 sind der Serielle Monitor)" : "");
    return false;
  }
  client_ = &rtuClient_;
  uint32_t baud = cfg_.baud;
  if (baud == 0) {
    baud = findBaudRate();
    if (baud == 0) {
      Serial.println("[MB] FEHLER: bei keiner Baudrate eine Antwort – Verdrahtung prüfen "
                     "(A/B vertauscht? TX/RX gekreuzt? Adresse, Parität, 230 V am Zähler?); "
                     "es wird mit 9600 Baud weiter versucht");
      baud = 9600;
    }
  }
  if (baud != baud_) startRtu(baud);
  Serial.printf("[MB] RTU: UART1 TX=GPIO%d RX=GPIO%d, %lu Baud %s, DE/RE %s, %u Register konfiguriert\n",
                cfg_.txPin, cfg_.rxPin, (unsigned long)baud_, serialConfigText(cfg_.serialConfig),
                cfg_.dePin >= 0 ? String(cfg_.dePin).c_str() : "automatisch (Wandler)",
                (unsigned)cfg_.registerCount);
  return true;
}

// --- TCP über Modbus-LAN-Gateway ---------------------------------------------------
bool ModbusMeters::beginTcp() {
  if (cfg_.gatewayIp == IPAddress(0, 0, 0, 0)) {
    Serial.println("[MB] FEHLER: Gateway-IP ist 0.0.0.0 – Zählerabfrage deaktiviert, "
                   "MODBUS_GATEWAY_IP in config.h prüfen");
    return false;
  }
  client_ = &tcpClient_;
  tcpClient_.setTimeout(cfg_.timeoutMs);
  // Nach einem Wechsel Ethernet <-> WLAN hängt die alte TCP-Verbindung an der alten
  // IP. Ohne das bliebe sie offen und jede Anfrage liefe bis zum Timeout.
  tcpClient_.closeConnectionOnTimeouts(3);
  tcpClient_.begin();                              // startet den eModbus-Worker-Task
  tcpClient_.setTarget(cfg_.gatewayIp, cfg_.gatewayPort);
  Serial.printf("[MB] Gateway %s:%u, %u Register konfiguriert\n",
                cfg_.gatewayIp.toString().c_str(), cfg_.gatewayPort,
                (unsigned)cfg_.registerCount);
  return true;
}

// --- Lesen ----------------------------------------------------------------------------
Error ModbusMeters::readRegister(const MeterRegister &r, double &value, bool quiet) {
  const uint8_t words = registerCount(r.type);
  ModbusMessage resp = client_->syncRequest(token_++, r.meter,
                                            r.function == 3 ? READ_HOLD_REGISTER
                                                            : READ_INPUT_REGISTER,
                                            r.address, words);
  const Error err = resp.getError();
  if (err != SUCCESS) {
    if (!quiet) {
      ModbusError me(err);
      Serial.printf("[MB] Zähler %u %s (Reg 0x%04X): Fehler %02X – %s\n",
                    r.meter, r.quantity, r.address, (int)err, (const char *)me);
    }
    return err;
  }
  // Länge prüfen: 3 Kopfbytes + 2 Byte je Register
  if (resp.size() < 3u + 2u * words) {
    if (!quiet) {
      Serial.printf("[MB] Zähler %u %s: Antwort zu kurz (%u Byte)\n",
                    r.meter, r.quantity, (unsigned)resp.size());
    }
    return PACKET_LENGTH_ERROR;
  }
  uint16_t w0 = 0, w1 = 0;
  const uint16_t idx = resp.get(3, w0);
  if (words == 2) resp.get(idx, w1);

  if (!decode(r, w0, w1, value)) {
    if (!quiet) {
      Serial.printf("[MB] Zähler %u %s: Wert nicht dekodierbar (0x%04X 0x%04X)\n",
                    r.meter, r.quantity, w0, w1);
    }
    return UNDEFINED_ERROR;
  }
  return SUCCESS;
}

uint16_t ModbusMeters::read(ReadingSink &out) {
  if (client_ == nullptr) {
    errorCount_ += cfg_.registerCount;
    Serial.println("[MB] Zählerabfrage übersprungen: Modbus nicht eingerichtet");
    return 0;
  }
  uint16_t ok = 0;
  int skipMeter = -1;               // Adresse, die in diesem Zyklus nicht antwortet
  for (size_t i = 0; i < cfg_.registerCount; i++) {
    const MeterRegister &r = cfg_.registers[i];
    if (r.meter == skipMeter) { errorCount_++; continue; }

    esp_task_wdt_reset();           // jede Anfrage kann bis zum Timeout blockieren
    double v = 0.0;
    const Error err = readRegister(r, v);
    if (err == SUCCESS) {
      out.add(static_cast<uint16_t>(i), v, digitsFor(r.type));
      ok++;
      continue;
    }
    errorCount_++;
    if (err == IP_CONNECTION_FAILED) {             // Gateway selbst nicht erreichbar
      Serial.println("[MB] Gateway nicht erreichbar – Zählerabfrage abgebrochen");
      errorCount_ += cfg_.registerCount - i - 1;
      break;
    }
    if (isUnreachable(err)) {
      skipMeter = r.meter;
      if (cfg_.transport == ModbusTransport::RTU) {
        Serial.printf("[MB] Zähler %u antwortet nicht (%lu Baud %s) – A/B, TX/RX, Adresse und "
                      "Baudrate prüfen; übrige Register übersprungen\n",
                      r.meter, (unsigned long)baud_, serialConfigText(cfg_.serialConfig));
      }
    }
  }
  return ok;
}

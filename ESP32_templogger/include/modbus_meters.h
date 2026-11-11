// =============================================================================
//  modbus_meters.h  –  Stromzähler über Modbus (RTU direkt oder TCP-Gateway)
// -----------------------------------------------------------------------------
//  Bibliothek: eModbus (github.com/eModbus/eModbus), Klassen ModbusClientRTU und
//  ModbusClientTCP (eModbus verlangt beim Kompilieren zusätzlich "Async TCP")
//
//  Zwei Übertragungswege, Auswahl in config.h (MODBUS_TRANSPORT):
//    RTU: ESP-UART1 --TTL--> RS485-Wandler --RS485--> Zähler
//         Baudrate, Parität und Pins müssen zum Zähler passen; Baudrate 0 = beim
//         Start automatisch suchen.
//    TCP: ESP --(Modbus TCP, Port 502)--> Gateway --(RS485)--> Zähler
//         Die "Unit-ID" im TCP-Telegramm wählt am Gateway den Zähler am RS485-Bus.
//
//  Sensorschlüssel im Energielogger: modbus_<Unit-ID>.<Messgröße>, z. B. modbus_1.energy
// =============================================================================
#pragma once
#include <Arduino.h>
#include <Network.h>
#include <ModbusClientRTU.h>
#include <ModbusClientTCP.h>
#include <string>
#include <vector>
#include "measurement_source.h"

enum class RegType : uint8_t {
  FLOAT32,   // IEEE 754, 2 Register
  UINT32,    // 2 Register
  INT32,     // 2 Register
  UINT16,    // 1 Register
  INT16      // 1 Register
};

enum class WordOrder : uint8_t {
  HIGH_FIRST,  // "Big Endian"  – z. B. Eastron SDM
  LOW_FIRST    // "Word swapped" – bei manchen Herstellern
};

enum class ModbusTransport : uint8_t {
  RTU,          // direkt über TTL-RS485-Wandler an UART1
  TCP_GATEWAY   // über ein Modbus-LAN-Gateway
};

// Ein zu lesendes Register = eine Messgröße eines Zählers (Einträge in config.h).
struct MeterRegister {
  uint8_t     meter;      // Zähler = Modbus-Adresse am RS485-Bus (1..247)
  const char *quantity;   // Messgröße im Schlüssel, z. B. "energy", "voltage"
  const char *sensorType; // Art laut Schnittstelle: energy, power, voltage, current, other
  const char *unit;       // Einheit, z. B. "W", "kWh", "V"
  uint8_t     function;   // 3 = Holding Register, 4 = Input Register
  uint16_t    address;    // Registeradresse (0-basiert, wie im Telegramm)
  RegType     type;
  WordOrder   order;
  double      scale;      // Rohwert * scale = Wert in `unit`. double, damit z. B.
                          // 123456789 * 0.001 exakt 123456.789 ergibt (mit float:
                          // 123456.7949). In config.h OHNE "f" schreiben (0.001).
};

class ModbusMeters : public MeasurementSource {
public:
  struct Config {
    ModbusTransport      transport;
    // --- nur RTU ---
    int8_t               txPin;           // ESP-TX -> RXD/DI des Wandlers
    int8_t               rxPin;           // ESP-RX <- TXD/RO des Wandlers
    int8_t               dePin;           // DE/RE des Wandlers, -1 = Wandler schaltet selbst um
    uint32_t             baud;            // 0 = beim Start automatisch suchen
    uint32_t             serialConfig;    // SERIAL_8N1, SERIAL_8E1, SERIAL_8O1, SERIAL_8N2
    // --- nur TCP ---
    IPAddress            gatewayIp;
    uint16_t             gatewayPort;
    // --- beide ---
    uint32_t             timeoutMs;       // je Anfrage
    const MeterRegister *registers;       // Register desselben Zählers hintereinander
    size_t               registerCount;
    const char          *manufacturer;    // optional, für die Zuordnung im Portal
    const char          *model;           // optional
  };

  explicit ModbusMeters(const Config &cfg)
      : cfg_(cfg), tcpClient_(tcp_), rtuClient_(cfg.dePin) {}

  // --- MeasurementSource ---
  void        begin() override;
  uint16_t    sensorCount() const override { return cfg_.registerCount; }
  SensorInfo  sensorInfo(uint16_t index) const override;
  uint16_t    read(ReadingSink &out) override;
  uint32_t    errorCount() const override { return errorCount_; }

  // --- Hilfsfunktionen (öffentlich und statisch, damit einzeln testbar) ---
  static uint8_t registerCount(RegType t);
  // Rohregister -> physikalischer Wert (double: 32-bit-Zählerstände bleiben exakt).
  static bool decode(const MeterRegister &r, uint16_t w0, uint16_t w1, double &out);
  // Gültige Stellen fürs JSON: float32-Quellen 7, Ganzzahlregister exakt.
  static uint8_t digitsFor(RegType t) { return t == RegType::FLOAT32 ? 7 : 10; }

private:
  bool  beginRtu();
  bool  beginTcp();
  void  startRtu(uint32_t baud);
  uint32_t findBaudRate();
  Error readRegister(const MeterRegister &r, double &value, bool quiet = false);
  static bool isUnreachable(Error e);

  Config          cfg_;
  NetworkClient   tcp_;          // muss vor tcpClient_ stehen (Initialisierungsreihenfolge)
  ModbusClientTCP tcpClient_;
  ModbusClientRTU rtuClient_;
  ModbusClient   *client_ = nullptr;   // der aktive Client, nullptr = keine Abfragen
  uint32_t        token_      = 1;     // fortlaufende Kennung je Anfrage
  uint32_t        errorCount_ = 0;
  uint32_t        baud_       = 0;     // tatsächlich verwendete Baudrate (RTU)
  std::vector<std::string> keys_, addresses_;   // je Register, siehe begin()
};

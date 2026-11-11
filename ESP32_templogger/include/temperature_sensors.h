// =============================================================================
//  temperature_sensors.h  –  DS18B20-Temperaturen an einem oder mehreren 1-Wire-Bussen
// -----------------------------------------------------------------------------
//  Bibliotheken:  OneWireNg (pstolarz)  +  DallasTemperature (Miles Burton)
//  WICHTIG: NICHT die Bibliothek "OneWire" von Paul Stoffregen installieren –
//  sie kompiliert für den ESP32-P4 nicht (direkte GPIO-Registerzugriffe).
//  OneWireNg bringt eine kompatible OneWire.h mit, die DallasTemperature nutzt.
//
//  Sensorschlüssel im Energielogger: aus SENSOR_MAP (temp_1 ... temp_12), sonst
//  temp_<ROM-ID>. So bleibt der Schlüssel auch nach einem Neustart gleich.
// =============================================================================
#pragma once
#include <Arduino.h>
#include <memory>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "measurement_source.h"

// Zuordnung ROM-ID -> Sensorschlüssel (Eintrag in config.h).
struct SensorMapEntry {
  const char *key;      // max. 32 Zeichen, z. B. "temp_1"
  uint8_t     rom[8];
};

class TemperatureSensors : public MeasurementSource {
public:
  static constexpr uint8_t MAX_BUSES           = 4;
  static constexpr uint8_t MAX_SENSORS_PER_BUS = 16;

  struct Config {
    const uint8_t        *pins;           // GPIO je 1-Wire-Bus
    uint8_t               busCount;       // <= MAX_BUSES
    const SensorMapEntry *sensorMap;      // optionale Schlüssel
    size_t                sensorMapCount;
    uint8_t               resolutionBits; // 9..12
    float                 minC, maxC;     // Plausibilitätsfenster
    float                 plausibleDelta85C;
  };

  explicit TemperatureSensors(const Config &cfg) : cfg_(cfg) {}

  // --- MeasurementSource ---
  void       begin() override;
  uint16_t   sensorCount() const override { return sensorCount_; }
  SensorInfo sensorInfo(uint16_t index) const override;
  uint16_t   read(ReadingSink &out) override;
  uint32_t   errorCount() const override { return errorCount_; }

  // --- Hilfsfunktionen (öffentlich, damit sie einzeln testbar sind) ---
  // ROM-ID als 16-stelliger Hex-String, out >= 17 Byte.
  static void romToHex(const uint8_t *rom, char *out);
  // Grund für das Verwerfen von t, oder nullptr, wenn t gültig ist.
  // lastValid = letzter gültiger Wert dieses Sensors (NAN = noch keiner).
  const char *rejectReason(float t, float lastValid) const;

private:
  struct Sensor {
    uint8_t       bus;         // Index in cfg_.pins
    DeviceAddress rom;
    float         lastValid;   // NAN = noch kein gültiger Wert
    char          key[33];     // Schlüssel im Energielogger
    char          address[17]; // ROM-ID als Hex
  };

  static constexpr uint8_t SEARCH_PASSES = 3;     // Suchläufe je Bus, IDs werden zusammengeführt

  uint8_t     searchBus(uint8_t b, Sensor *out);
  const char *mappedKey(const uint8_t *rom) const;
  void        warnMissingMappedSensors() const;

  Config   cfg_;
  std::unique_ptr<OneWire>           wire_[MAX_BUSES];
  std::unique_ptr<DallasTemperature> dallas_[MAX_BUSES];
  Sensor   sensors_[MAX_BUSES * MAX_SENSORS_PER_BUS];
  uint8_t  sensorCount_ = 0;
  uint32_t errorCount_  = 0;
};

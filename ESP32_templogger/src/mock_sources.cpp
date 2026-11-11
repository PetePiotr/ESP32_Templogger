// =============================================================================
//  mock_sources.cpp  –  Implementierung der Testwerte
// =============================================================================
#include "mock_sources.h"

// Dreieck 0..40..0 mit 80 Zyklen Periode.
static uint16_t triangle(uint32_t cycle) {
  const uint32_t p = cycle % 80;
  return p <= 40 ? p : 80 - p;
}

SensorInfo MockTemperatures::sensorInfo(uint16_t index) const {
  static const SensorInfo SENSORS[] = {
    {"temp_mock_1", "temperature", "degC", "mock", "MOCK-1", nullptr, "MOCK"},
    {"temp_mock_2", "temperature", "degC", "mock", "MOCK-2", nullptr, "MOCK"},
  };
  return SENSORS[index];
}

uint16_t MockTemperatures::read(ReadingSink &out) {
  // Schrittweite 1/16 °C: bleibt ein Vielfaches von 0,0625 wie beim DS18B20.
  const double offsetC = triangle(cycle_++) / 16.0;
  out.add(0, 62.0 + offsetC, 10);
  out.add(1, 48.0 + offsetC, 10);
  return 2;
}

SensorInfo MockMeter::sensorInfo(uint16_t index) const {
  static const SensorInfo SENSORS[] = {
    {"modbus_247.power",      "power",   "W",   "mock", "247", nullptr, "MOCK"},
    {"modbus_247.energy",     "energy",  "kWh", "mock", "247", nullptr, "MOCK"},
    {"modbus_247.voltage_l1", "voltage", "V",   "mock", "247", nullptr, "MOCK"},
  };
  return SENSORS[index];
}

uint16_t MockMeter::read(ReadingSink &out) {
  const uint16_t step = triangle(cycle_++);
  const double powerW = 1500.0 + step * 10.0;
  energyKwh_ += powerW * cycleSeconds_ / 3600000.0;                  // Leistung über einen Zyklus
  out.add(0, powerW, 7);                                             // wie ein float32-Register
  out.add(1, energyKwh_, 7);
  out.add(2, 230.0 + step * 0.05, 7);
  return 3;
}

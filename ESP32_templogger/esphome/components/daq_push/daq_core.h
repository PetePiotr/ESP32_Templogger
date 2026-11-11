// =============================================================================
//  daq_core.h  –  Fachlogik des DAQ-Loggers, unabhängig von ESPHome
// -----------------------------------------------------------------------------
//  Enthält alles, was ohne Hardware geprüft werden kann (Host-Tests in
//  test/test_esphome_core):
//    - Plausibilitätsprüfung der Temperaturen inkl. 85-°C-Regel
//    - Dekodierung von Modbus-Rohregistern (float32, (u)int16/32, Wortreihenfolge,
//      Skalierung als double -> 32-bit-Zählerstände bleiben exakt)
//    - Zahlenformat im JSON und Aufbau des Requests nach API_SCHNITTSTELLE.md v0.3
//  Die ESPHome-Anbindung (Takt, Sensoren, HTTP) steht in daq_push.h/.cpp.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace daq {

// --- Temperaturen ----------------------------------------------------------------
struct TempLimits {
  float minC = -40.0f;
  float maxC = 110.0f;
  float plausibleDelta85C = 5.0f;   // 85,0 °C nur, wenn der letzte Wert so nah lag
};

// Grund für das Verwerfen von t, oder nullptr, wenn t gültig ist.
// lastValid = letzter gültiger Wert dieses Sensors (NAN = noch keiner).
// NAN steht für "keine Antwort / CRC falsch / Einschaltwert erkannt"
// (so meldet es der ESPHome-Baustein dallas_temp).
const char *temperatureRejectReason(float t, float lastValid, const TempLimits &lim);

// Temperatur fürs JSON: 4 Nachkommastellen (DS18B20: Vielfache von 0,0625 -> exakt).
std::string formatTemperature(float t);

// --- Modbus ----------------------------------------------------------------------
enum class RegType : uint8_t { FLOAT32, UINT32, INT32, UINT16, INT16 };
enum class WordOrder : uint8_t { HIGH_FIRST, LOW_FIRST };

struct RegisterFormat {
  RegType   type  = RegType::FLOAT32;
  WordOrder order = WordOrder::HIGH_FIRST;
  double    scale = 1.0;
};

uint8_t registerCount(RegType t);

// Hex-Text wie vom modbus_controller-Textsensor mit raw_encode: HEXBYTES
// (z. B. "44e54000") in Bytes wandeln. false bei falscher Länge/Zeichen.
bool parseHexBytes(const std::string &hex, std::vector<uint8_t> &out);

// Registerbytes (High-Byte zuerst, wie im Telegramm) -> physikalischer Wert.
// false bei zu wenigen Bytes, NaN oder Unendlich.
bool decodeRegisters(const RegisterFormat &f, const std::vector<uint8_t> &bytes, double &out);

// Zahl fürs JSON: float32-Quellen 7 gültige Stellen, Ganzzahlregister bis 10 Stellen.
std::string formatMeterValue(const RegisterFormat &f, double value);

// --- Request ---------------------------------------------------------------------
struct TempReading {
  std::string rom;    // 16 Hex-Zeichen, Familiencode zuerst (z. B. "28FF641E8C160384")
  std::string name;   // "" -> null
  std::string value;  // bereits formatiert (formatTemperature)
};
struct MeterReading {
  uint8_t     meter;  // Modbus-Adresse des Zählers
  std::string quantity, unit;
  std::string value;  // bereits formatiert (formatMeterValue)
};
struct Status {
  uint32_t uptimeS = 0, uploadOk = 0, uploadErr = 0, skipped = 0, readErr = 0;
};

std::string buildRequestJson(const std::string &deviceId, const std::string &firmware, uint32_t ts,
                             const std::vector<TempReading> &temps, const std::vector<MeterReading> &meters,
                             const Status &status);

}  // namespace daq

// =============================================================================
//  daq_core.cpp  –  Implementierung der Fachlogik (siehe daq_core.h)
// =============================================================================
#include "daq_core.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace daq {

const char *temperatureRejectReason(float t, float lastValid, const TempLimits &lim) {
  if (std::isnan(t))                 return "keine Antwort/CRC/Einschaltwert";
  if (t < lim.minC || t > lim.maxC)  return "unplausibel";
  // 85,0 °C ist der Einschaltwert des DS18B20. Original-Sensoren erkennt schon
  // dallas_temp (liefert dann NAN); diese Regel fängt Nachbauten ab. Da ein
  // Heizungsvorlauf wirklich 85 °C haben kann, wird 85,0 nur verworfen, wenn
  // noch kein gültiger Wert vorliegt oder der letzte zu weit entfernt war.
  if (t == 85.0f && (std::isnan(lastValid) || std::fabs(t - lastValid) > lim.plausibleDelta85C)) {
    return "Einschaltwert 85 °C";
  }
  return nullptr;
}

std::string formatTemperature(float t) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(t));
  return buf;
}

uint8_t registerCount(RegType t) {
  return (t == RegType::UINT16 || t == RegType::INT16) ? 1 : 2;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHexBytes(const std::string &hex, std::vector<uint8_t> &out) {
  out.clear();
  if (hex.size() % 2 != 0) return false;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int hi = hexNibble(hex[i]), lo = hexNibble(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return true;
}

bool decodeRegisters(const RegisterFormat &f, const std::vector<uint8_t> &bytes, double &out) {
  const size_t words = registerCount(f.type);
  if (bytes.size() < 2 * words) return false;
  const uint16_t w0 = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
  const uint16_t w1 = words == 2 ? static_cast<uint16_t>((bytes[2] << 8) | bytes[3]) : 0;
  const uint16_t hi = (f.order == WordOrder::HIGH_FIRST) ? w0 : w1;
  const uint16_t lo = (f.order == WordOrder::HIGH_FIRST) ? w1 : w0;
  const uint32_t u  = (static_cast<uint32_t>(hi) << 16) | lo;

  double v;
  switch (f.type) {
    case RegType::FLOAT32: { float x; memcpy(&x, &u, sizeof(x)); v = x; break; }
    case RegType::UINT32:  v = static_cast<double>(u);                        break;
    case RegType::INT32:   v = static_cast<double>(static_cast<int32_t>(u));  break;
    case RegType::UINT16:  v = static_cast<double>(w0);                       break;
    case RegType::INT16:   v = static_cast<double>(static_cast<int16_t>(w0)); break;
    default: return false;
  }
  if (std::isnan(v) || std::isinf(v)) return false;
  out = v * f.scale;
  return true;
}

std::string formatMeterValue(const RegisterFormat &f, double value) {
  char buf[24];
  snprintf(buf, sizeof(buf), f.type == RegType::FLOAT32 ? "%.7g" : "%.10g", value);
  return buf;
}

std::string buildRequestJson(const std::string &deviceId, const std::string &firmware, uint32_t ts,
                             const std::vector<TempReading> &temps, const std::vector<MeterReading> &meters,
                             const Status &status) {
  JsonDocument doc;
  doc["device_id"] = deviceId;
  doc["firmware"]  = firmware;
  doc["ts"]        = ts;

  JsonArray t = doc["temperatures"].to<JsonArray>();
  for (const TempReading &r : temps) {
    JsonObject o = t.add<JsonObject>();
    o["sensor"] = r.rom;
    if (r.name.empty()) o["name"] = nullptr; else o["name"] = r.name;
    o["c"] = serialized(r.value);              // Rohtext: genau 4 Nachkommastellen
  }

  JsonArray m = doc["meter_readings"].to<JsonArray>();
  for (const MeterReading &r : meters) {
    JsonObject o = m.add<JsonObject>();
    o["meter"]    = r.meter;
    o["quantity"] = r.quantity;
    o["unit"]     = r.unit;
    o["v"]        = serialized(r.value);
  }

  JsonObject s = doc["status"].to<JsonObject>();
  s["uptime_s"]   = status.uptimeS;
  s["upload_ok"]  = status.uploadOk;
  s["upload_err"] = status.uploadErr;
  s["skipped"]    = status.skipped;
  s["read_err"]   = status.readErr;

  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace daq

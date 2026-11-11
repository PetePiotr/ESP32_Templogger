// =============================================================================
//  Tests für die Fachlogik der ESPHome-Variante (esphome/components/daq_push/daq_core)
// -----------------------------------------------------------------------------
//  daq_core ist frei von ESPHome und läuft deshalb unverändert auf dem PC.
//  Der erzeugte Request wird mit derselben Prüfung wie die Arduino-Variante
//  gegen API_SCHNITTSTELLE.md v0.3 geprüft (api_check.h).
// =============================================================================
#include <unity.h>
#include <cstring>
#include "api_check.h"
#include "daq_core.h"

using namespace daq;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> hex(const char *h) {
  std::vector<uint8_t> b;
  TEST_ASSERT_TRUE(parseHexBytes(h, b));
  return b;
}
static std::string decoded(RegisterFormat f, const char *h) {
  double v = 0;
  TEST_ASSERT_TRUE_MESSAGE(decodeRegisters(f, hex(h), v), h);
  return formatMeterValue(f, v);
}

// --- Temperaturen ----------------------------------------------------------------
void test_temperatur_plausibilitaet() {
  TempLimits lim;
  TEST_ASSERT_NULL(temperatureRejectReason(62.3125f, NAN, lim));
  TEST_ASSERT_NULL(temperatureRejectReason(-12.3125f, NAN, lim));
  TEST_ASSERT_NOT_NULL(temperatureRejectReason(NAN, 60.0f, lim));        // dallas_temp: Fehler
  TEST_ASSERT_NOT_NULL(temperatureRejectReason(125.0f, NAN, lim));
  TEST_ASSERT_NOT_NULL(temperatureRejectReason(-55.0f, NAN, lim));
  TEST_ASSERT_NULL(temperatureRejectReason(110.0f, NAN, lim));
}

void test_85_grad_regel() {
  TempLimits lim;
  TEST_ASSERT_NOT_NULL(temperatureRejectReason(85.0f, NAN, lim));       // erster Wert
  TEST_ASSERT_NOT_NULL(temperatureRejectReason(85.0f, 62.0f, lim));     // Sprung > 5 K
  TEST_ASSERT_NULL(temperatureRejectReason(85.0f, 84.5f, lim));         // stetiger Anstieg
  TEST_ASSERT_NULL(temperatureRejectReason(85.0625f, NAN, lim));        // nicht genau 85,0
}

void test_temperaturformat() {
  TEST_ASSERT_EQUAL_STRING("62.3125", formatTemperature(62.3125f).c_str());
  TEST_ASSERT_EQUAL_STRING("-12.3125", formatTemperature(-12.3125f).c_str());
  TEST_ASSERT_EQUAL_STRING("40.0000", formatTemperature(40.0f).c_str());
}

// --- Modbus ----------------------------------------------------------------------
void test_hexbytes_wie_modbus_textsensor() {
  TEST_ASSERT_EQUAL(4, hex("44e54000").size());
  TEST_ASSERT_EQUAL_HEX8(0xE5, hex("44E54000")[1]);
  std::vector<uint8_t> b;
  TEST_ASSERT_FALSE(parseHexBytes("44e5400", b));    // ungerade Länge
  TEST_ASSERT_FALSE(parseHexBytes("44x54000", b));   // kein Hex
}

void test_float32_beide_wortreihenfolgen() {
  // 1834.5f = 0x44E55000
  TEST_ASSERT_EQUAL_STRING("1834.5", decoded({RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0}, "44e55000").c_str());
  TEST_ASSERT_EQUAL_STRING("1834.5", decoded({RegType::FLOAT32, WordOrder::LOW_FIRST, 1.0}, "500044e5").c_str());
  // 12345.678f -> 7 gültige Stellen
  TEST_ASSERT_EQUAL_STRING("12345.68", decoded({RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0}, "4640e6b6").c_str());
}

void test_ganzzahlen_und_skalierung_exakt() {
  TEST_ASSERT_EQUAL_STRING("123456789", decoded({RegType::UINT32, WordOrder::HIGH_FIRST, 1.0}, "075bcd15").c_str());
  TEST_ASSERT_EQUAL_STRING("4294967295", decoded({RegType::UINT32, WordOrder::LOW_FIRST, 1.0}, "ffffffff").c_str());
  TEST_ASSERT_EQUAL_STRING("-123456", decoded({RegType::INT32, WordOrder::HIGH_FIRST, 1.0}, "fffe1dc0").c_str());
  TEST_ASSERT_EQUAL_STRING("-500", decoded({RegType::INT16, WordOrder::HIGH_FIRST, 1.0}, "fe0c").c_str());
  TEST_ASSERT_EQUAL_STRING("65535", decoded({RegType::UINT16, WordOrder::HIGH_FIRST, 1.0}, "ffff").c_str());
  // Skalierung als double: keine float-Rundungsfehler
  TEST_ASSERT_EQUAL_STRING("123456.789", decoded({RegType::UINT32, WordOrder::HIGH_FIRST, 0.001}, "075bcd15").c_str());
  TEST_ASSERT_EQUAL_STRING("49.99", decoded({RegType::UINT16, WordOrder::HIGH_FIRST, 0.01}, "1387").c_str());
}

void test_ungueltige_register_werden_verworfen() {
  double v;
  TEST_ASSERT_FALSE(decodeRegisters({RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0}, hex("7fc00000"), v));  // NaN
  TEST_ASSERT_FALSE(decodeRegisters({RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0}, hex("7f800000"), v));  // +Inf
  TEST_ASSERT_FALSE(decodeRegisters({RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0}, hex("44e5"), v));      // zu kurz
}

// --- Request ---------------------------------------------------------------------
void test_request_entspricht_api() {
  std::vector<TempReading> t = {{"28FF641E8C160384", "T01_VL_Kessel", formatTemperature(62.3125f)},
                                {"28FF0A2B3C4D5E61", "", formatTemperature(48.0625f)}};
  std::vector<MeterReading> m = {{1, "P_ges", "W", "1834.5"}, {1, "E_import", "kWh", "12345.68"}};
  Status s{86400, 8630, 9, 1, 0};
  const std::string body = buildRequestJson("heizung01", "2.0.0-esphome", 1791961200u, t, m, s);

  // ROM-IDs im Beispiel aus API_SCHNITTSTELLE.md haben keine gültige CRC ->
  // nur diese Meldungen sind hier zulässig.
  std::string err = apicheck::checkApiBody(body);
  std::string other;
  size_t pos = 0;
  while (pos < err.size()) {
    size_t nl = err.find('\n', pos);
    std::string line = err.substr(pos, nl - pos);
    if (line.rfind("ROM-CRC falsch", 0) != 0) other += line + "\n";
    pos = nl + 1;
  }
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", other.c_str(), body.c_str());
  TEST_ASSERT_TRUE(body.find("\"name\":null") != std::string::npos);
  TEST_ASSERT_TRUE(body.find("\"c\":62.3125") != std::string::npos);
  TEST_ASSERT_TRUE(body.find("\"v\":12345.68") != std::string::npos);
  TEST_MESSAGE(body.c_str());
}

void test_request_mit_leeren_listen() {
  const std::string body = buildRequestJson("heizung01", "2.0.0-esphome", 1791961200u, {}, {}, Status{});
  TEST_ASSERT_EQUAL_STRING("", apicheck::checkApiBody(body).c_str());
  TEST_ASSERT_TRUE(body.find("\"temperatures\":[]") != std::string::npos);
  TEST_ASSERT_TRUE(body.find("\"meter_readings\":[]") != std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_temperatur_plausibilitaet);
  RUN_TEST(test_85_grad_regel);
  RUN_TEST(test_temperaturformat);
  RUN_TEST(test_hexbytes_wie_modbus_textsensor);
  RUN_TEST(test_float32_beide_wortreihenfolgen);
  RUN_TEST(test_ganzzahlen_und_skalierung_exakt);
  RUN_TEST(test_ungueltige_register_werden_verworfen);
  RUN_TEST(test_request_entspricht_api);
  RUN_TEST(test_request_mit_leeren_listen);
  return UNITY_END();
}

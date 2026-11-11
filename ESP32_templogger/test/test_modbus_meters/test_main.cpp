// =============================================================================
//  Tests für ModbusMeters (echte Klasse gegen ein simuliertes Modbus-TCP-Gateway)
// -----------------------------------------------------------------------------
//  Die Registerinhalte werden wie beim echten Zähler als 16-bit-Wörter abgelegt
//  (sim::ModbusMeterSim). Geprüft wird der Wert im JSON-Rohtext, denn genau
//  dieser Text kommt bei der API an.
// =============================================================================
#include <unity.h>
#include <ArduinoJson.h>
#include "sim.h"
#include "modbus_meters.h"

static const IPAddress GW(192, 168, 1, 50);

struct Cycle {
  uint16_t    n = 0;
  std::string json;
};
static Cycle runCycle(ModbusMeters &m) {
  JsonDocument doc;
  Cycle c;
  c.n = m.readInto(doc["meter_readings"].to<JsonArray>());
  serializeJson(doc, c.json);
  return c;
}
static bool has(const Cycle &c, const std::string &fragment) {
  return c.json.find(fragment) != std::string::npos;
}
#define ASSERT_JSON_HAS(c, frag) TEST_ASSERT_TRUE_MESSAGE(has(c, frag), (std::string("erwartet: ") + frag + "\nJSON: " + (c).json).c_str())

static ModbusMeters::Config cfgFor(const MeterRegister *regs, size_t n) {
  return {GW, 502, 1000, regs, n};
}

void setUp() { sim::reset(); }
void tearDown() {}

// -----------------------------------------------------------------------------
// Fall 1: Eastron SDM630 wie in config.h (Input-Register, float32, Big Endian).
static const MeterRegister SDM630[] = {
  {1, "P_ges",    "W",   4, 0x0034, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {1, "E_import", "kWh", 4, 0x0048, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {1, "U_L1",     "V",   4, 0x0000, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
};

void test_sdm630_float32_big_endian() {
  auto &m = sim::gateway().meters[1];
  m.setFloat(4, 0x0034, 1834.5f);
  m.setFloat(4, 0x0048, 12345.678f);
  m.setFloat(4, 0x0000, 230.1f);
  ModbusMeters mm(cfgFor(SDM630, 3));
  mm.begin();
  TEST_ASSERT_TRUE(sim::gateway().begun);
  TEST_ASSERT_TRUE(sim::gateway().target == GW);
  TEST_ASSERT_EQUAL_UINT16(502, sim::gateway().port);

  Cycle c = runCycle(mm);
  TEST_ASSERT_EQUAL_UINT16(3, c.n);
  ASSERT_JSON_HAS(c, "{\"meter\":1,\"quantity\":\"P_ges\",\"unit\":\"W\",\"v\":1834.5}");
  ASSERT_JSON_HAS(c, "\"v\":12345.68");     // float32: 7 gültige Stellen
  ASSERT_JSON_HAS(c, "\"v\":230.1}");
  TEST_ASSERT_EQUAL_UINT32(0, mm.errorCount());
  // Funktionscode und Registeranzahl je Anfrage
  TEST_ASSERT_EQUAL_UINT8(4, sim::gateway().requests[0].fc);
  TEST_ASSERT_EQUAL_UINT16(2, sim::gateway().requests[0].count);
  TEST_MESSAGE(("Beispiel: " + c.json).c_str());
}

// Fall 2: Zähler mit vertauschter Wortreihenfolge ("word swapped").
void test_float32_wortreihenfolge_low_first() {
  static const MeterRegister R[] = {
    {2, "P_ges", "W", 3, 100, RegType::FLOAT32, WordOrder::LOW_FIRST, 1.0}};
  sim::gateway().meters[2].setFloat(3, 100, -4321.25f, sim::Order::LOW_FIRST);
  ModbusMeters mm(cfgFor(R, 1));
  mm.begin();
  Cycle c = runCycle(mm);
  ASSERT_JSON_HAS(c, "\"v\":-4321.25");
  TEST_ASSERT_EQUAL_UINT8(3, sim::gateway().requests[0].fc);
}

// Fall 3: Ganzzahlregister ohne Skalierung – Zählerstände müssen exakt bleiben.
void test_ganzzahlregister_exakt() {
  static const MeterRegister R[] = {
    {3, "E_Wh",   "Wh", 3, 0, RegType::UINT32, WordOrder::HIGH_FIRST, 1.0},
    {3, "E_max",  "Wh", 3, 2, RegType::UINT32, WordOrder::LOW_FIRST,  1.0},
    {3, "P_rueck","W",  3, 4, RegType::INT16,  WordOrder::HIGH_FIRST, 1.0},
    {3, "Q",      "var",3, 5, RegType::INT32,  WordOrder::HIGH_FIRST, 1.0},
    {3, "Status", "-",  3, 7, RegType::UINT16, WordOrder::HIGH_FIRST, 1.0}};
  auto &m = sim::gateway().meters[3];
  m.setU32(3, 0, 123456789u);
  m.setU32(3, 2, 4294967295u, sim::Order::LOW_FIRST);
  m.setU16(3, 4, static_cast<uint16_t>(-500));
  m.setU32(3, 5, static_cast<uint32_t>(-123456));
  m.setU16(3, 7, 65535);
  ModbusMeters mm(cfgFor(R, 5));
  mm.begin();
  Cycle c = runCycle(mm);
  TEST_ASSERT_EQUAL_UINT16(5, c.n);
  ASSERT_JSON_HAS(c, "\"v\":123456789}");
  ASSERT_JSON_HAS(c, "\"v\":4294967295}");
  ASSERT_JSON_HAS(c, "\"v\":-500}");
  ASSERT_JSON_HAS(c, "\"v\":-123456}");
  ASSERT_JSON_HAS(c, "\"v\":65535}");
}

// Fall 4: Typische Skalierungen aus Registertabellen (0,001 kWh/Bit, 0,01 Hz/Bit).
// Erwartung: 123456789 x 0,001 = 123456.789 kWh, 4999 x 0,01 = 49.99 Hz.
void test_skalierte_ganzzahlregister() {
  static const MeterRegister R[] = {
    {4, "E_import", "kWh", 3, 0, RegType::UINT32, WordOrder::HIGH_FIRST, 0.001},
    {4, "f",        "Hz",  3, 2, RegType::UINT16, WordOrder::HIGH_FIRST, 0.01}};
  auto &m = sim::gateway().meters[4];
  m.setU32(3, 0, 123456789u);
  m.setU16(3, 2, 4999);
  ModbusMeters mm(cfgFor(R, 2));
  mm.begin();
  Cycle c = runCycle(mm);
  TEST_ASSERT_EQUAL_UINT16(2, c.n);
  ASSERT_JSON_HAS(c, "\"v\":123456.789}");
  ASSERT_JSON_HAS(c, "\"v\":49.99}");
}

// Fall 5: Zähler liefert NaN / Unendlich (z. B. Messung noch nicht bereit).
void test_nan_und_unendlich_werden_verworfen() {
  static const MeterRegister R[] = {
    {1, "A", "W", 4, 0, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
    {1, "B", "W", 4, 2, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
    {1, "C", "W", 4, 4, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0}};
  auto &m = sim::gateway().meters[1];
  m.setU32(4, 0, 0x7FC00000u);   // NaN
  m.setU32(4, 2, 0x7F800000u);   // +Inf
  m.setFloat(4, 4, 12.5f);
  ModbusMeters mm(cfgFor(R, 3));
  mm.begin();
  Cycle c = runCycle(mm);
  TEST_ASSERT_EQUAL_UINT16(1, c.n);
  TEST_ASSERT_EQUAL_UINT32(2, mm.errorCount());
  TEST_ASSERT_FALSE(has(c, "nan") || has(c, "inf") || has(c, "NaN"));
  ASSERT_JSON_HAS(c, "\"quantity\":\"C\"");
}

// Drei Zähler mit je drei Registern für die Ausfalltests.
static const MeterRegister THREE[] = {
  {1, "P", "W", 4, 0, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {1, "E", "kWh", 4, 2, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {1, "U", "V", 4, 4, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {2, "P", "W", 4, 0, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {2, "E", "kWh", 4, 2, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {2, "U", "V", 4, 4, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {3, "P", "W", 4, 0, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {3, "E", "kWh", 4, 2, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {3, "U", "V", 4, 4, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0}};
static void fillThree() {
  for (uint8_t id = 1; id <= 3; id++)
    for (uint16_t a = 0; a <= 4; a += 2) sim::gateway().meters[id].setFloat(4, a, 100.0f * id + a);
}

// Fall 6: Zähler 2 ist am RS485-Bus abgeklemmt; das Gateway meldet 0x0B.
// Seine übrigen Register werden NICHT mehr angefragt, Zähler 1 und 3 normal.
void test_zaehler_antwortet_nicht_gateway_meldet_0x0B() {
  fillThree();
  sim::gateway().meters[2].responds = false;
  ModbusMeters mm(cfgFor(THREE, 9));
  mm.begin();
  Cycle c = runCycle(mm);
  TEST_ASSERT_EQUAL_UINT16(6, c.n);
  TEST_ASSERT_EQUAL_UINT32(3, mm.errorCount());
  TEST_ASSERT_EQUAL_size_t(7, sim::gateway().requests.size());   // 3 + 1 + 3
  TEST_ASSERT_FALSE(has(c, "\"meter\":2"));
  TEST_ASSERT_TRUE(sim::logContains("Fehler 0B"));
}

// Fall 7: Gateway antwortet gar nicht für Zähler 2 -> Client-Timeout (1 s).
// Es darf nur EIN Timeout Zeit kosten, nicht drei.
void test_timeout_kostet_nur_einmal_zeit() {
  fillThree();
  sim::gateway().meters[2].responds = false;
  sim::gateway().meters[2].silentError = TIMEOUT;
  ModbusMeters mm(cfgFor(THREE, 9));
  mm.begin();
  const uint32_t t0 = millis();
  Cycle c = runCycle(mm);
  const uint32_t dt = millis() - t0;
  TEST_ASSERT_EQUAL_UINT16(6, c.n);
  TEST_ASSERT_UINT32_WITHIN(200, 1000 + 6 * 25, dt);
}

// Fall 8: Gateway selbst ist aus (kein Strom / falsche IP) -> Abbruch nach der
// ersten Anfrage, alle Register zählen als Fehler.
void test_gateway_nicht_erreichbar_bricht_ab() {
  fillThree();
  sim::gateway().reachable = false;
  ModbusMeters mm(cfgFor(THREE, 9));
  mm.begin();
  Cycle c = runCycle(mm);
  TEST_ASSERT_EQUAL_UINT16(0, c.n);
  TEST_ASSERT_EQUAL_STRING("{\"meter_readings\":[]}", c.json.c_str());
  TEST_ASSERT_EQUAL_size_t(1, sim::gateway().requests.size());
  TEST_ASSERT_EQUAL_UINT32(9, mm.errorCount());
  TEST_ASSERT_TRUE(sim::logContains("Gateway nicht erreichbar"));
}

// Fall 9: Falsche Registeradresse in config.h -> Ausnahme 0x02 nur für dieses
// Register; die übrigen Register desselben Zählers werden weiter gelesen.
void test_falsche_registeradresse_betrifft_nur_ein_register() {
  fillThree();
  sim::gateway().meters[1].regs.erase((4u << 16) | 2);
  sim::gateway().meters[1].regs.erase((4u << 16) | 3);
  ModbusMeters mm(cfgFor(THREE, 3));
  mm.begin();
  Cycle c = runCycle(mm);
  TEST_ASSERT_EQUAL_UINT16(2, c.n);
  ASSERT_JSON_HAS(c, "\"quantity\":\"U\"");
  TEST_ASSERT_TRUE(sim::logContains("Fehler 02"));
}

// Fall 10: Gateway liefert eine verstümmelte (zu kurze) Antwort.
void test_zu_kurze_antwort_wird_verworfen() {
  fillThree();
  sim::gateway().meters[1].shortResponse = true;
  ModbusMeters mm(cfgFor(THREE, 3));
  mm.begin();
  Cycle c = runCycle(mm);
  TEST_ASSERT_EQUAL_UINT16(0, c.n);
  TEST_ASSERT_TRUE(sim::logContains("Antwort zu kurz"));
}

// Fall 11: Zähler kommt nach Ausfall zurück -> im nächsten Zyklus wieder dabei.
void test_zaehler_kommt_zurueck() {
  fillThree();
  sim::gateway().meters[3].responds = false;
  ModbusMeters mm(cfgFor(THREE, 9));
  mm.begin();
  TEST_ASSERT_EQUAL_UINT16(6, runCycle(mm).n);
  sim::gateway().meters[3].responds = true;
  TEST_ASSERT_EQUAL_UINT16(9, runCycle(mm).n);
}

// Gateway-IP 0.0.0.0 (Konfigurationsfehler): gar nicht erst abfragen, laut melden,
// jedes Register zählt als Lesefehler.
void test_gateway_ip_null_wird_abgefangen() {
  auto &m = sim::gateway().meters[1];
  m.setFloat(4, 0x0034, 1834.5f);
  m.setFloat(4, 0x0048, 12345.678f);
  m.setFloat(4, 0x0000, 230.1f);
  ModbusMeters mm({IPAddress(0, 0, 0, 0), 502, 1000, SDM630, 3});
  mm.begin();
  TEST_ASSERT_TRUE(sim::logContains("Gateway-IP ist 0.0.0.0"));
  TEST_ASSERT_FALSE(sim::gateway().begun);
  TEST_ASSERT_EQUAL_UINT16(0, runCycle(mm).n);
  TEST_ASSERT_EQUAL_UINT32(3, mm.errorCount());
  TEST_ASSERT_TRUE(sim::logContains("keine gültige Gateway-IP"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sdm630_float32_big_endian);
  RUN_TEST(test_float32_wortreihenfolge_low_first);
  RUN_TEST(test_ganzzahlregister_exakt);
  RUN_TEST(test_skalierte_ganzzahlregister);
  RUN_TEST(test_nan_und_unendlich_werden_verworfen);
  RUN_TEST(test_zaehler_antwortet_nicht_gateway_meldet_0x0B);
  RUN_TEST(test_timeout_kostet_nur_einmal_zeit);
  RUN_TEST(test_gateway_nicht_erreichbar_bricht_ab);
  RUN_TEST(test_falsche_registeradresse_betrifft_nur_ein_register);
  RUN_TEST(test_zu_kurze_antwort_wird_verworfen);
  RUN_TEST(test_zaehler_kommt_zurueck);
  RUN_TEST(test_gateway_ip_null_wird_abgefangen);
  return UNITY_END();
}

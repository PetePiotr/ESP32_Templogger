// =============================================================================
//  Tests für TemperatureSensors (echte Klasse + echte DallasTemperature)
// -----------------------------------------------------------------------------
//  Die DS18B20 sind auf 1-Wire-Byte-Ebene simuliert (test/sim/HostSim).
//  Jeder Test beschreibt einen realen Fall an der Heizungsanlage.
// =============================================================================
#include <unity.h>
#include <ArduinoJson.h>
#include "sim.h"
#include "temperature_sensors.h"

static const uint8_t PINS[] = {20, 21};
static SensorMapEntry g_map[16];
static size_t g_mapCount = 0;

static TemperatureSensors::Config makeConfig(uint8_t buses = 2) {
  return {PINS, buses, g_map, g_mapCount, 12, -40.0f, 110.0f, 5.0f};
}

static void nameSensor(const sim::SimDS18B20 &s, const char *name) {
  g_map[g_mapCount].name = name;
  memcpy(g_map[g_mapCount].rom, s.rom, 8);
  g_mapCount++;
}

// Ein Zyklus: readInto() in ein frisches JSON, als Text serialisieren und –
// wie bei der API – wieder einlesen ("c" steht als Rohtext im Dokument).
struct Cycle {
  uint16_t    n = 0;
  std::string json;
  JsonDocument parsed;
  JsonObjectConst find(const std::string &rom) const {
    for (JsonObjectConst o : parsed["temperatures"].as<JsonArrayConst>())
      if (rom == o["sensor"].as<const char *>()) return o;
    return JsonObjectConst();
  }
};
static Cycle runCycle(TemperatureSensors &t) {
  Cycle c;
  JsonDocument doc;
  c.n = t.readInto(doc["temperatures"].to<JsonArray>());
  serializeJson(doc, c.json);
  deserializeJson(c.parsed, c.json);
  return c;
}

void setUp() {
  sim::reset();
  g_mapCount = 0;
}
void tearDown() {}

// -----------------------------------------------------------------------------
// Fall 1: Ausbau laut Aufgabe – 12 Fühler an zwei Bussen, 10 davon benannt,
// darunter ein Außenfühler mit Minusgraden.
void test_zwoelf_sensoren_an_zwei_bussen() {
  std::vector<sim::SimDS18B20 *> s;
  const double temps[12] = {62.3, 48.07, 55.5, 41.2, 70.0, 35.94, 21.4375,
                            60.1, 45.0, 30.3, 19.8, -12.3};
  for (int i = 0; i < 7; i++) s.push_back(&sim::addSensor(20, 0x1000 + i, temps[i]));
  for (int i = 7; i < 12; i++) s.push_back(&sim::addSensor(21, 0x2000 + i, temps[i]));
  static const char *names[10] = {"T01_VL_Kessel", "T02_RL_Kessel", "T03_VL_HK1", "T04_RL_HK1",
                                  "T05_Speicher_oben", "T06_Speicher_unten", "T07_Raum",
                                  "T08_VL_HK2", "T09_RL_HK2", "T10_WW"};
  for (int i = 0; i < 10; i++) nameSensor(*s[i], names[i]);

  TemperatureSensors t(makeConfig());
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(12, t.expectedCount());

  Cycle c = runCycle(t);
  TEST_ASSERT_EQUAL_UINT16(12, c.n);
  TEST_ASSERT_EQUAL_UINT32(0, t.errorCount());
  for (int i = 0; i < 12; i++) {
    JsonObjectConst o = c.find(s[i]->romHex());
    TEST_ASSERT_FALSE_MESSAGE(o.isNull(), s[i]->romHex().c_str());
    TEST_ASSERT_EQUAL_DOUBLE(sim::quantize12(temps[i]), o["c"].as<double>());
    if (i < 10) TEST_ASSERT_EQUAL_STRING(names[i], o["name"].as<const char *>());
    else        TEST_ASSERT_TRUE(o["name"].isNull());
  }
  // Rohtext: 4 Nachkommastellen, negative Werte korrekt
  TEST_ASSERT_TRUE(c.json.find("\"c\":62.3125") != std::string::npos);
  TEST_ASSERT_TRUE(c.json.find("\"c\":-12.3125") != std::string::npos);
  TEST_MESSAGE(("Beispiel: " + c.json.substr(0, 160) + " ...").c_str());
}

// Fall 2: Neue Fühler stehen ab Werk oft auf 12 bit, manche auf 9 bit (EEPROM).
// begin() muss alle auf 12 bit stellen, sonst fehlen die 1/16-°C-Stufen.
void test_aufloesung_wird_auf_12_bit_gestellt() {
  auto &a = sim::addSensor(20, 0x11, 21.4375);
  a.eepromConfig = 0x1F; a.config = 0x1F;      // 9 bit
  auto &b = sim::addSensor(20, 0x12, 21.4375); // 12 bit

  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT8(12, a.resolution());
  TEST_ASSERT_EQUAL_INT(1, a.configWrites);    // nur der abweichende Fühler wird beschrieben
  TEST_ASSERT_EQUAL_INT(0, b.configWrites);

  Cycle c = runCycle(t);
  TEST_ASSERT_EQUAL_DOUBLE(21.4375, c.find(a.romHex())["c"].as<double>());
}

// Fall 3: Eine Messung muss die volle Wandlungszeit abwarten (12 bit: bis 750 ms).
// Zwei Busse werden nacheinander gewandelt.
void test_wandlungszeit_wird_abgewartet() {
  sim::addSensor(20, 0x21, 50.0);
  sim::addSensor(21, 0x22, 50.0);
  TemperatureSensors t(makeConfig());
  t.begin();
  const uint32_t t0 = millis();
  Cycle c = runCycle(t);
  const uint32_t dt = millis() - t0;
  TEST_ASSERT_EQUAL_UINT16(2, c.n);
  TEST_ASSERT_UINT32_WITHIN(150, 2 * 620, dt);   // 2 Busse x 620 ms Wandlung
  TEST_ASSERT_EQUAL_DOUBLE(50.0, c.find(sim::bus(20).sensors[0].romHex())["c"].as<double>());
}

// Fall 4: Wackelkontakt -> Original-DS18B20 startet neu und liefert den
// Einschaltwert 85 °C (Byte 6 = 0x0C). DallasTemperature erkennt das selbst
// und meldet -127; die Firmware verwirft den Wert.
void test_einschaltwert_original_sensor_wird_verworfen() {
  auto &s = sim::addSensor(20, 0x31, 62.0);
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);

  s.porReads = 1;
  Cycle c = runCycle(t);
  TEST_ASSERT_EQUAL_UINT16(0, c.n);
  TEST_ASSERT_EQUAL_UINT32(1, t.errorCount());
  TEST_ASSERT_FALSE(c.json.find("85.0000") != std::string::npos);
  TEST_ASSERT_TRUE(sim::logContains("keine Antwort/CRC"));

  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);    // nächster Zyklus wieder gültig
}

// Fall 5: Nachbau-Sensor (kennzeichnet den Einschaltwert nicht). Hier greift
// die 85-°C-Regel der Firmware: 85,0 nach 62 °C ist ein Sprung > 5 K.
void test_einschaltwert_bei_nachbau_greift_85_grad_regel() {
  auto &s = sim::addSensor(20, 0x41, 62.0);
  s.clone = true;
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);

  s.porReads = 1;
  Cycle c = runCycle(t);
  TEST_ASSERT_EQUAL_UINT16(0, c.n);
  TEST_ASSERT_TRUE(sim::logContains("Einschaltwert 85"));
}

// Fall 6: Heizungsvorlauf erreicht wirklich 85 °C (stetiger Anstieg) -> gültig.
void test_echte_85_grad_im_vorlauf_werden_akzeptiert() {
  auto &s = sim::addSensor(20, 0x51, 82.0);
  TemperatureSensors t(makeConfig(1));
  t.begin();
  for (double v : {82.0, 83.5, 84.5, 85.0, 85.0, 85.5, 85.0}) {
    s.tempC = v;
    Cycle c = runCycle(t);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, c.n, "85 °C nach stetigem Anstieg verworfen");
    TEST_ASSERT_EQUAL_DOUBLE(v, c.find(s.romHex())["c"].as<double>());
  }
  TEST_ASSERT_EQUAL_UINT32(0, t.errorCount());
}

// Fall 7 (bekannte Grenze der 85-°C-Regel): Steht der Vorlauf beim Start
// genau auf 85,0 °C, wird der Wert verworfen, bis er sich ändert.
void test_genau_85_grad_beim_start_wird_bis_zur_aenderung_verworfen() {
  auto &s = sim::addSensor(20, 0x61, 85.0);
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(0, runCycle(t).n);
  TEST_ASSERT_EQUAL_UINT16(0, runCycle(t).n);
  s.tempC = 84.9375;
  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);
  s.tempC = 85.0;
  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);
  TEST_ASSERT_EQUAL_UINT32(2, t.errorCount());
}

// Fall 8: Fühler wird im Betrieb abgeklemmt (Leitungsbruch) und wieder angeschlossen.
void test_sensor_abgeklemmt_und_wieder_angeschlossen() {
  auto &a = sim::addSensor(20, 0x71, 40.0);
  auto &b = sim::addSensor(20, 0x72, 41.0);
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(2, runCycle(t).n);

  b.connected = false;
  Cycle c = runCycle(t);
  TEST_ASSERT_EQUAL_UINT16(1, c.n);
  TEST_ASSERT_FALSE(c.find(a.romHex()).isNull());
  TEST_ASSERT_TRUE(c.find(b.romHex()).isNull());
  TEST_ASSERT_EQUAL_UINT32(1, t.errorCount());

  b.connected = true;
  TEST_ASSERT_EQUAL_UINT16(2, runCycle(t).n);
}

// Fall 9: Störimpuls (z. B. Umwälzpumpe schaltet) -> Bitfehler im Scratchpad.
// Die CRC-Prüfung muss den Wert verwerfen; ein falscher Wert darf nie durchgehen.
void test_bitfehler_im_scratchpad_wird_verworfen() {
  auto &s = sim::addSensor(20, 0x81, 55.0);
  TemperatureSensors t(makeConfig(1));
  t.begin();
  s.scratchCrcErrors = 1;
  Cycle c = runCycle(t);
  TEST_ASSERT_EQUAL_UINT16(0, c.n);
  TEST_ASSERT_EQUAL_UINT32(1, t.errorCount());
  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);
}

// Fall 10: Ein Bus ist beim Start kurzgeschlossen; der andere arbeitet normal.
void test_ein_bus_kurzgeschlossen_anderer_arbeitet() {
  sim::addSensor(20, 0x91, 30.0);
  sim::bus(20).shorted = true;
  sim::addSensor(21, 0x92, 31.0);
  TemperatureSensors t(makeConfig());
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(1, t.expectedCount());
  TEST_ASSERT_TRUE(sim::logContains("Bus 0 (GPIO 20): 0 Sensor(en)"));
  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);
}

// Fall 11: Kein Fühler angeschlossen (Pull-up fehlt / Stecker ab).
void test_keine_sensoren_gefunden() {
  TemperatureSensors t(makeConfig());
  t.begin();
  TEST_ASSERT_TRUE(sim::logContains("keine Sensoren gefunden"));
  Cycle c = runCycle(t);
  TEST_ASSERT_EQUAL_UINT16(0, c.n);
  TEST_ASSERT_EQUAL_STRING("{\"temperatures\":[]}", c.json.c_str());
}

// Fall 12: Werte außerhalb des Plausibilitätsfensters -40..110 °C.
void test_unplausible_werte_werden_verworfen() {
  auto &s = sim::addSensor(20, 0xA1, 125.0);
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(0, runCycle(t).n);
  s.tempC = -55.0;
  TEST_ASSERT_EQUAL_UINT16(0, runCycle(t).n);
  s.tempC = 110.0;
  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);
  TEST_ASSERT_TRUE(sim::logContains("unplausibel"));
}

// Fall 13: Mehr als 16 Fühler an einem Bus -> nur die ersten 16 werden genutzt.
void test_mehr_als_16_sensoren_an_einem_bus() {
  for (int i = 0; i < 18; i++) sim::addSensor(20, 0xB00 + i, 20.0 + i);
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(16, t.expectedCount());
  TEST_ASSERT_TRUE(sim::logContains("nur die ersten 16"));
  TEST_ASSERT_EQUAL_UINT16(16, runCycle(t).n);
}

// Fall 14: Bitfehler während der Bussuche beim Start (lange Leitung). Die
// Störung trifft die 3 Suchen in dallas.begin() (die Bibliothek wiederholt, solange
// sie nichts findet) und die ersten beiden eigenen Suchläufe, erst der dritte
// kommt durch. Erwartung: alle 3 Fühler in Betrieb,
// auch der gestörte Fühler wird auf 12 bit gestellt, und die Störung wird gemeldet.
void test_romfehler_bei_der_suche_wird_wiederholt() {
  auto &a = sim::addSensor(20, 0xC1, 40.0625);
  sim::addSensor(20, 0xC2, 41.0);
  sim::addSensor(20, 0xC3, 42.0);
  a.eepromConfig = 0x1F; a.config = 0x1F;      // ab Werk 9 bit
  a.romErrorsInSearch = 3 + 2;
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(3, t.expectedCount());
  TEST_ASSERT_TRUE(sim::logContains("uneinheitlich"));
  TEST_ASSERT_EQUAL_UINT8(12, a.resolution());
  Cycle c = runCycle(t);
  TEST_ASSERT_EQUAL_UINT16(3, c.n);
  TEST_ASSERT_EQUAL_DOUBLE(40.0625, c.find(a.romHex())["c"].as<double>());
}

// Fall 15: Eine ROM-ID ist dauerhaft gestört (z. B. defekter Fühler). Jede Suche
// bricht dort ab, also fehlen auch die Fühler dahinter. Das ist über die
// OneWire-Schnittstelle nicht zu umgehen; die benannten Fühler werden aber gemeldet.
void test_dauerhaft_gestoerte_rom_meldet_fehlende_fuehler() {
  auto &ok     = sim::addSensor(20, 0xD1, 40.0);
  auto &bad    = sim::addSensor(20, 0xD2, 41.0);
  auto &hinten = sim::addSensor(20, 0xD3, 42.0);
  bad.romErrorsInSearch = 1000;
  nameSensor(ok, "T_ok");
  nameSensor(bad, "T_defekt");
  nameSensor(hinten, "T_hinten");
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(1, t.expectedCount());
  TEST_ASSERT_FALSE(sim::logContains("T_ok ("));
  TEST_ASSERT_TRUE(sim::logContains("T_defekt ("));
  TEST_ASSERT_TRUE(sim::logContains("T_hinten ("));
  TEST_ASSERT_EQUAL_UINT16(1, runCycle(t).n);
}

// Fall 16: Nur einzelne Suchläufe brechen in der Mitte ab. Die IDs mehrerer Läufe
// dürfen sich nicht doppeln, und am Ende fehlt kein Fühler.
void test_abbruch_in_der_mitte_ohne_doppelte_fuehler() {
  auto &a = sim::addSensor(20, 0xE1, 40.0);
  auto &b = sim::addSensor(20, 0xE2, 41.0);
  auto &c = sim::addSensor(20, 0xE3, 42.0);
  b.romErrorsInSearch = 2;
  nameSensor(a, "T_a");
  nameSensor(b, "T_b");
  nameSensor(c, "T_c");
  TemperatureSensors t(makeConfig(1));
  t.begin();
  TEST_ASSERT_EQUAL_UINT16(3, t.expectedCount());
  TEST_ASSERT_TRUE(sim::logContains("uneinheitlich"));
  TEST_ASSERT_FALSE(sim::logContains("nicht gefunden"));
  TEST_ASSERT_EQUAL_UINT16(3, runCycle(t).n);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zwoelf_sensoren_an_zwei_bussen);
  RUN_TEST(test_aufloesung_wird_auf_12_bit_gestellt);
  RUN_TEST(test_wandlungszeit_wird_abgewartet);
  RUN_TEST(test_einschaltwert_original_sensor_wird_verworfen);
  RUN_TEST(test_einschaltwert_bei_nachbau_greift_85_grad_regel);
  RUN_TEST(test_echte_85_grad_im_vorlauf_werden_akzeptiert);
  RUN_TEST(test_genau_85_grad_beim_start_wird_bis_zur_aenderung_verworfen);
  RUN_TEST(test_sensor_abgeklemmt_und_wieder_angeschlossen);
  RUN_TEST(test_bitfehler_im_scratchpad_wird_verworfen);
  RUN_TEST(test_ein_bus_kurzgeschlossen_anderer_arbeitet);
  RUN_TEST(test_keine_sensoren_gefunden);
  RUN_TEST(test_unplausible_werte_werden_verworfen);
  RUN_TEST(test_mehr_als_16_sensoren_an_einem_bus);
  RUN_TEST(test_romfehler_bei_der_suche_wird_wiederholt);
  RUN_TEST(test_dauerhaft_gestoerte_rom_meldet_fehlende_fuehler);
  RUN_TEST(test_abbruch_in_der_mitte_ohne_doppelte_fuehler);
  return UNITY_END();
}

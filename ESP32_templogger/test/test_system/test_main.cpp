// =============================================================================
//  Systemtests: DaqLogger mit den Einstellungen aus config.h
// -----------------------------------------------------------------------------
//  Läuft wie main.cpp: begin(), dann loop() + delay(10) – nur mit simulierter
//  Uhr. Jeder gesendete Request wird gegen API_SCHNITTSTELLE.md geprüft.
//  Die Bodies des 1-h-Dauertests landen zusätzlich in sim_requests.jsonl
//  (für test/check_requests_with_api.py).
// =============================================================================
#include <unity.h>
#include <ArduinoJson.h>
#include "api_check.h"
#include "daq_logger.h"
#include "sim.h"

// Werte aus config.h (hier nur zum Vergleichen wiederholt)
static const char *CFG_URL = "http://192.168.1.10:8000/api/v1/measurements";
static const char *CFG_KEY = "CHANGE_ME_LONG_RANDOM_STRING";

static std::unique_ptr<DaqLogger> logger;

// Anlage wie geplant: 12 Fühler (7 + 5) an GPIO 20/21, ein SDM630 an Unit-ID 1.
static void buildPlant() {
  for (int i = 0; i < 7; i++) sim::addSensor(20, 0x100 + i, 40.0 + i * 3.1);
  for (int i = 0; i < 5; i++) sim::addSensor(21, 0x200 + i, 20.0 + i * 2.7);
  auto &m = sim::gateway().meters[1];
  m.setFloat(4, 0x0034, 1834.5f);
  m.setFloat(4, 0x0048, 12345.678f);
  m.setFloat(4, 0x0000, 230.1f);
}

// Wie main.cpp: setup()
static void boot() {
  logger.reset(new DaqLogger());
  logger->begin();
}
// Wie main.cpp: loop() mit delay(10), für die angegebene (simulierte) Zeit
static void runFor(uint32_t ms) {
  const uint32_t start = millis();
  while (millis() - start < ms) {
    logger->loop();
    delay(10);
  }
}

static JsonDocument parse(const sim::HttpRequest &r) {
  JsonDocument d;
  deserializeJson(d, r.body);
  return d;
}

// 10-s-Takt: Einzelne POSTs dürfen um das loop()-Raster (10 ms) schwanken,
// über viele Zyklen darf sich aber keine Abweichung aufsummieren (Drift).
static void assertTenSecondGrid(size_t from) {
  const auto &req = sim::httpRequests();
  for (size_t i = from + 1; i < req.size(); i++)
    TEST_ASSERT_UINT32_WITHIN(15, 10000, req[i].atMs - req[i - 1].atMs);
  const uint32_t n = static_cast<uint32_t>(req.size() - from - 1);
  TEST_ASSERT_UINT32_WITHIN(15, 10000 * n, req.back().atMs - req[from].atMs);
}

static void assertAllBodiesValid() {
  for (size_t i = 0; i < sim::httpRequests().size(); i++) {
    const std::string err = apicheck::checkApiBody(sim::httpRequests()[i].body);
    if (!err.empty()) {
      const std::string msg = "Request " + std::to_string(i) + " verletzt die API:\n" + err +
                              sim::httpRequests()[i].body;
      TEST_FAIL_MESSAGE(msg.c_str());
    }
  }
}

void setUp() {
  sim::reset();
  sim::setHttpDump("");
}
void tearDown() {
  // In JEDEM Szenario: Watchdog nie abgelaufen, alle Bodies API-konform.
  TEST_ASSERT_FALSE_MESSAGE(sim::wdt().fired, "Task-Watchdog wäre abgelaufen (Neustart)");
  assertAllBodiesValid();
  logger.reset();
}

// -----------------------------------------------------------------------------
// Fall 1: Normaler Start: Ethernet, NTP, erster Request mit allen Werten.
void test_normaler_start_und_erster_request() {
  buildPlant();
  boot();
  TEST_ASSERT_TRUE(sim::eth().begun);
  TEST_ASSERT_EQUAL_STRING("heizung01", sim::eth().hostname.c_str());
  TEST_ASSERT_EQUAL_UINT32(60000, sim::wdt().timeoutMs);
  TEST_ASSERT_TRUE(sim::wdt().taskAdded);

  sim::ethLinkUp();
  runFor(15000);
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", sim::ntp().server1.c_str());
  TEST_ASSERT_TRUE(sim::httpRequests().size() >= 1);

  const sim::HttpRequest &r = sim::httpRequests().back();
  TEST_ASSERT_EQUAL_STRING(CFG_URL, r.url.c_str());
  TEST_ASSERT_EQUAL_STRING("application/json", r.contentType.c_str());
  TEST_ASSERT_EQUAL_STRING(CFG_KEY, r.apiKey.c_str());
  TEST_ASSERT_FALSE(r.tls);

  JsonDocument d = parse(r);
  TEST_ASSERT_EQUAL_STRING("heizung01", d["device_id"]);
  TEST_ASSERT_EQUAL_STRING("0.4.0", d["firmware"]);
  TEST_ASSERT_EQUAL(12, d["temperatures"].size());
  TEST_ASSERT_EQUAL(3, d["meter_readings"].size());
  TEST_ASSERT_EQUAL_UINT32(0, d["status"]["read_err"].as<uint32_t>());
  TEST_MESSAGE(("Beispiel-Request (" + std::to_string(r.body.size()) + " Byte): " + r.body).c_str());
}

// Fall 2: Start ohne Netzwerkkabel, dann Link, NTP braucht 12 s.
// Zyklen ohne Netz bzw. ohne gültige Uhrzeit werden NICHT gesendet, aber gezählt.
void test_start_ohne_netz_und_ohne_ntp() {
  buildPlant();
  sim::ntp().syncDelayMs = 12000;
  boot();
  runFor(25000);                                // Zyklen bei 0, 10, 20 s: kein Netz
  TEST_ASSERT_EQUAL_size_t(0, sim::httpRequests().size());
  TEST_ASSERT_TRUE(sim::logContains("Zyklus übersprungen: kein Netz"));

  sim::ethLinkUp();                             // ab 25 s; NTP gültig ab ca. 37 s
  runFor(20000);                                // Zyklus 30 s: keine Zeit, 40 s: Senden
  TEST_ASSERT_TRUE(sim::logContains("Uhrzeit noch nicht synchron"));
  TEST_ASSERT_EQUAL_size_t(1, sim::httpRequests().size());
  JsonDocument d = parse(sim::httpRequests()[0]);
  TEST_ASSERT_EQUAL_UINT32(4, d["status"]["skipped"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(0, d["status"]["upload_ok"].as<uint32_t>());
}

// Fall 3: Dauerbetrieb 1 Stunde: 360 Requests, ts genau alle 10 s, Zähler stimmen.
void test_dauerbetrieb_eine_stunde() {
  remove("sim_requests.jsonl");
  sim::setHttpDump("sim_requests.jsonl");
  buildPlant();
  boot();
  sim::ethLinkUp();
  runFor(3000);                                 // NTP synchronisieren lassen
  const size_t before = sim::httpRequests().size();
  runFor(3600000);
  const auto &req = sim::httpRequests();
  TEST_ASSERT_EQUAL_size_t(360, req.size() - before);

  uint32_t lastTs = 0;
  for (size_t i = before; i < req.size(); i++) {
    JsonDocument d = parse(req[i]);
    const uint32_t ts = d["ts"];
    if (i > before) TEST_ASSERT_EQUAL_UINT32_MESSAGE(lastTs + 10, ts, "ts nicht im 10-s-Raster");
    lastTs = ts;
    TEST_ASSERT_EQUAL_UINT32(i, d["status"]["upload_ok"].as<uint32_t>());
    TEST_ASSERT_EQUAL(12, d["temperatures"].size());
    TEST_ASSERT_EQUAL(3, d["meter_readings"].size());
  }
  assertTenSecondGrid(before);   // 360 Zyklen ohne Drift
  TEST_ASSERT_LESS_THAN_UINT32(60000, sim::wdt().maxGapMs);
  sim::setHttpDump("");
}

// Fall 4: API nicht erreichbar (Server aus) für 3 Zyklen.
// Kein Zwischenspeicher: die 3 Zyklen fehlen, upload_err zählt mit.
void test_api_voruebergehend_nicht_erreichbar() {
  buildPlant();
  boot();
  sim::ethLinkUp();
  int call = 0;
  sim::httpHandler() = [&](const sim::HttpRequest &) {
    sim::HttpResponse r;
    call++;
    r.refused = (call >= 3 && call <= 5);
    return r;
  };
  runFor(80000);
  const auto &req = sim::httpRequests();
  TEST_ASSERT_TRUE(req.size() >= 7);
  JsonDocument after = parse(req[5]);           // erster Request nach dem Ausfall
  TEST_ASSERT_EQUAL_UINT32(3, after["status"]["upload_err"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(2, after["status"]["upload_ok"].as<uint32_t>());
  // keine Wiederholung: jeder ts genau einmal
  for (size_t i = 1; i < req.size(); i++)
    TEST_ASSERT_TRUE(parse(req[i])["ts"].as<uint32_t>() > parse(req[i - 1])["ts"].as<uint32_t>());
  TEST_ASSERT_TRUE(sim::logContains("Senden fehlgeschlagen: -1"));
}

// Fall 5: API antwortet mit 401 (falscher Schlüssel) bzw. 500 (Serverfehler).
void test_api_antwortet_mit_fehlercodes() {
  buildPlant();
  boot();
  sim::ethLinkUp();
  int call = 0;
  sim::httpHandler() = [&](const sim::HttpRequest &) {
    sim::HttpResponse r;
    call++;
    if (call == 1) { r.code = 401; r.body = "{\"error\":\"invalid api key\"}"; }
    if (call == 2) { r.code = 500; r.body = "Internal Server Error"; }
    return r;
  };
  runFor(35000);
  TEST_ASSERT_TRUE(sim::logContains("HTTP 401, Antwort: {\"error\":\"invalid api key\"}"));
  TEST_ASSERT_TRUE(sim::logContains("HTTP 500"));
  JsonDocument d = parse(sim::httpRequests()[2]);
  TEST_ASSERT_EQUAL_UINT32(2, d["status"]["upload_err"].as<uint32_t>());
}

// Fall 6: Netzwerkkabel wird 35 s lang gezogen (bzw. LTE-Router startet neu).
void test_ethernet_unterbrechung() {
  buildPlant();
  boot();
  sim::ethLinkUp();
  runFor(25000);
  const size_t sent = sim::httpRequests().size();
  sim::ethLinkDown();
  runFor(35000);
  TEST_ASSERT_EQUAL_size_t(sent, sim::httpRequests().size());   // nichts gesendet
  sim::ethLinkUp();
  runFor(10000);
  TEST_ASSERT_EQUAL_size_t(sent + 1, sim::httpRequests().size());
  JsonDocument d = parse(sim::httpRequests().back());
  TEST_ASSERT_TRUE(d["status"]["skipped"].as<uint32_t>() >= 3);
}

// Fall 7: Modbus-Gateway ausgefallen: Temperaturen werden weiter gesendet,
// meter_readings ist leer, read_err steigt je Zyklus um 3.
void test_gateway_ausgefallen_temperaturen_laufen_weiter() {
  buildPlant();
  sim::gateway().reachable = false;
  boot();
  sim::ethLinkUp();
  runFor(25000);
  const auto &req = sim::httpRequests();
  TEST_ASSERT_TRUE(req.size() >= 2);
  JsonDocument a = parse(req[0]), b = parse(req[1]);
  TEST_ASSERT_EQUAL(12, b["temperatures"].size());
  TEST_ASSERT_EQUAL(0, b["meter_readings"].size());
  TEST_ASSERT_EQUAL_UINT32(3, b["status"]["read_err"].as<uint32_t>() - a["status"]["read_err"].as<uint32_t>());
}

// Fall 8: Ungünstigster regulärer Zyklus: langsame API (4,5 s) und Zähler im
// Timeout – der 10-s-Takt muss trotzdem exakt gehalten werden.
void test_langsame_api_und_zaehler_timeout_halten_den_takt() {
  buildPlant();
  sim::gateway().meters[1].responds = false;
  sim::gateway().meters[1].silentError = TIMEOUT;
  sim::httpHandler() = [](const sim::HttpRequest &) {
    sim::HttpResponse r;
    r.latencyMs = 4500;
    return r;
  };
  boot();
  sim::ethLinkUp();
  runFor(120000);
  const auto &req = sim::httpRequests();
  TEST_ASSERT_TRUE(req.size() >= 10);
  assertTenSecondGrid(0);
}

// Fall 9: LTE sehr langsam: Verbindungsaufbau 4,9 s + Antwort 4,9 s.
// Ein Zyklus dauert dann > 10 s. Es darf weder der Watchdog auslösen noch ein
// Zeitstempel doppelt gesendet werden.
void test_sehr_langsames_netz_zyklus_ueber_10_s() {
  buildPlant();
  sim::httpHandler() = [](const sim::HttpRequest &) {
    sim::HttpResponse r;
    r.connectMs = 4900;
    r.latencyMs = 4900;
    return r;
  };
  boot();
  sim::ethLinkUp();
  runFor(300000);
  const auto &req = sim::httpRequests();
  TEST_ASSERT_TRUE(req.size() >= 20);
  for (size_t i = 1; i < req.size(); i++) {
    TEST_ASSERT_TRUE(parse(req[i])["ts"].as<uint32_t>() > parse(req[i - 1])["ts"].as<uint32_t>());
  }
  TEST_ASSERT_TRUE(sim::logContains("ms gesamt"));
  char msg[96];
  snprintf(msg, sizeof(msg), "%u Requests in 300 s (ideal 30)", (unsigned)req.size());
  TEST_MESSAGE(msg);
}

// Fall 10: millis()-Überlauf nach 49,7 Tagen Laufzeit.
void test_millis_ueberlauf_nach_49_tagen() {
  sim::reset(0xFFFFFFFFu - 30000);              // Überlauf ca. 30 s nach dem Start
  buildPlant();
  boot();
  sim::ethLinkUp();
  runFor(120000);
  const auto &req = sim::httpRequests();
  TEST_ASSERT_TRUE(req.size() >= 11);
  assertTenSecondGrid(0);
  for (size_t i = 1; i < req.size(); i++) {
    TEST_ASSERT_EQUAL_UINT32(parse(req[i - 1])["ts"].as<uint32_t>() + 10, parse(req[i])["ts"].as<uint32_t>());
  }
}

// Fall 11: Alle Fühler ausgefallen (z. B. Pull-up-Widerstand defekt) und
// Gateway aus: Der Request geht trotzdem raus, mit leeren Listen.
void test_alles_ausgefallen_leere_listen() {
  sim::gateway().reachable = false;
  boot();
  sim::ethLinkUp();
  runFor(15000);
  TEST_ASSERT_TRUE(sim::httpRequests().size() >= 1);
  JsonDocument d = parse(sim::httpRequests().back());
  TEST_ASSERT_EQUAL(0, d["temperatures"].size());
  TEST_ASSERT_EQUAL(0, d["meter_readings"].size());
  TEST_ASSERT_TRUE(d["temperatures"].is<JsonArray>());
}

// Fall 12: Fühler fällt im Betrieb aus -> fehlt im JSON, read_err steigt.
void test_sensorausfall_im_betrieb() {
  buildPlant();
  boot();
  sim::ethLinkUp();
  runFor(15000);
  sim::bus(21).sensors[2].connected = false;
  runFor(10000);
  JsonDocument d = parse(sim::httpRequests().back());
  TEST_ASSERT_EQUAL(11, d["temperatures"].size());
  TEST_ASSERT_EQUAL_UINT32(1, d["status"]["read_err"].as<uint32_t>());
}

// Die IP aus config.h muss beim Gateway ankommen. Die Reihenfolge globaler
// Konstruktoren lässt sich hier nicht nachstellen (logger entsteht zur Laufzeit);
// dagegen schützt das constexpr in config.h schon beim Kompilieren.
void test_gateway_ip_aus_config_kommt_an() {
  buildPlant();
  boot();
  TEST_ASSERT_TRUE(sim::gateway().target == IPAddress(192, 168, 1, 50));
  TEST_ASSERT_FALSE(sim::logContains("Gateway-IP ist 0.0.0.0"));
}

// Mock-Modus ohne jede Hardware (keine Fühler, Gateway aus): 2 Temperaturen und
// 1 Zähler kommen trotzdem API-konform an, klar als Testwerte erkennbar und veränderlich.
void test_mock_modus_sendet_testwerte_ohne_hardware() {
  sim::gateway().reachable = false;
  logger.reset(new DaqLogger(true));
  logger->begin();
  TEST_ASSERT_TRUE(sim::logContains("MOCK-MODUS"));
  TEST_ASSERT_FALSE(sim::gateway().begun);        // Modbus wird gar nicht gestartet

  sim::ethLinkUp();
  runFor(25000);
  TEST_ASSERT_TRUE(sim::httpRequests().size() >= 2);
  assertAllBodiesValid();

  JsonDocument a = parse(sim::httpRequests()[0]), b = parse(sim::httpRequests()[1]);
  TEST_ASSERT_EQUAL_STRING("0.4.0-mock", a["firmware"]);
  TEST_ASSERT_EQUAL(2, a["temperatures"].size());
  TEST_ASSERT_EQUAL_STRING("MOCK_T01", a["temperatures"][0]["name"]);
  TEST_ASSERT_EQUAL_STRING("MOCK_T02", a["temperatures"][1]["name"]);
  TEST_ASSERT_EQUAL(3, a["meter_readings"].size());
  for (JsonObject m : a["meter_readings"].as<JsonArray>()) TEST_ASSERT_EQUAL(247, m["meter"].as<int>());
  TEST_ASSERT_EQUAL_UINT32(0, b["status"]["read_err"].as<uint32_t>());

  // Werte ändern sich, der Energiezähler läuft nur vorwärts.
  TEST_ASSERT_TRUE(b["temperatures"][0]["c"].as<double>() > a["temperatures"][0]["c"].as<double>());
  TEST_ASSERT_TRUE(b["meter_readings"][0]["v"].as<double>() > a["meter_readings"][0]["v"].as<double>());
  TEST_ASSERT_TRUE(b["meter_readings"][1]["v"].as<double>() >= a["meter_readings"][1]["v"].as<double>());
  TEST_MESSAGE(("MOCK-BODY " + sim::httpRequests()[0].body).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_normaler_start_und_erster_request);
  RUN_TEST(test_start_ohne_netz_und_ohne_ntp);
  RUN_TEST(test_dauerbetrieb_eine_stunde);
  RUN_TEST(test_api_voruebergehend_nicht_erreichbar);
  RUN_TEST(test_api_antwortet_mit_fehlercodes);
  RUN_TEST(test_ethernet_unterbrechung);
  RUN_TEST(test_gateway_ausgefallen_temperaturen_laufen_weiter);
  RUN_TEST(test_langsame_api_und_zaehler_timeout_halten_den_takt);
  RUN_TEST(test_sehr_langsames_netz_zyklus_ueber_10_s);
  RUN_TEST(test_millis_ueberlauf_nach_49_tagen);
  RUN_TEST(test_alles_ausgefallen_leere_listen);
  RUN_TEST(test_sensorausfall_im_betrieb);
  RUN_TEST(test_gateway_ip_aus_config_kommt_an);
  RUN_TEST(test_mock_modus_sendet_testwerte_ohne_hardware);
  return UNITY_END();
}

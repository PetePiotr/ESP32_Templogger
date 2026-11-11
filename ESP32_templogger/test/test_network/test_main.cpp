// =============================================================================
//  Tests für den WLAN-Fallback von EthernetConnection (simulierte Ereignisse)
// -----------------------------------------------------------------------------
//  Ethernet hat Vorrang: WLAN startet erst nach der Wartezeit ohne Ethernet und
//  wird getrennt, sobald Ethernet zurück ist. Ereignisse kommen hier synchron.
// =============================================================================
#include <unity.h>
#include "ethernet_connection.h"
#include "sim.h"

static const int8_t SDIO_PINS[7] = {18, 19, 14, 15, 16, 17, 54};
static const uint32_t DELAY_MS = 30000;
static const uint32_t RETRY_MS = 30000;

static EthernetConnection::Config makeConfig(bool enabled = true, const char *ssid = "Anlage") {
  return {"heizung01", "ntp1", "ntp2", "UTC0", enabled, ssid, "geheim", SDIO_PINS, DELAY_MS, RETRY_MS};
}

static void runFor(EthernetConnection &net, uint32_t ms) {
  const uint32_t start = millis();
  while (millis() - start < ms) {
    net.loop();
    delay(10);
  }
}

void setUp() { sim::reset(); }
void tearDown() {}

// Fall 1: Standard in config.h: Fallback aus, WLAN wird nie angefasst.
void test_fallback_aus_startet_kein_wlan() {
  EthernetConnection net(makeConfig(false));
  net.begin();
  runFor(net, 120000);
  TEST_ASSERT_EQUAL_INT(0, sim::wifi().beginCalls);
  TEST_ASSERT_EQUAL_INT(0, sim::wifi().setPinsCalls);
  TEST_ASSERT_FALSE(net.isOnline());
}

// Fall 2: Ethernet kommt innerhalb der Wartezeit (normaler Start).
void test_ethernet_rechtzeitig_da_kein_wlan() {
  EthernetConnection net(makeConfig());
  net.begin();
  runFor(net, DELAY_MS - 5000);
  sim::ethLinkUp();
  runFor(net, 120000);
  TEST_ASSERT_EQUAL_INT(0, sim::wifi().beginCalls);
  TEST_ASSERT_TRUE(net.isOnline());
}

// Fall 3: Start ohne Ethernet-Kabel: WLAN erst nach der Wartezeit, dann läuft auch NTP.
void test_start_ohne_ethernet_nutzt_wlan() {
  EthernetConnection net(makeConfig());
  net.begin();
  runFor(net, DELAY_MS - 1000);
  TEST_ASSERT_EQUAL_INT(0, sim::wifi().beginCalls);
  TEST_ASSERT_FALSE(net.isOnline());

  runFor(net, 2000);
  TEST_ASSERT_EQUAL_INT(1, sim::wifi().beginCalls);
  TEST_ASSERT_EQUAL_STRING("Anlage", sim::wifi().ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("geheim", sim::wifi().password.c_str());
  TEST_ASSERT_EQUAL_STRING("heizung01", sim::wifi().hostname.c_str());
  TEST_ASSERT_EQUAL_INT8_ARRAY(SDIO_PINS, sim::wifi().pins, 7);
  TEST_ASSERT_TRUE(net.isOnline());
  TEST_ASSERT_TRUE(sim::logContains("[NET] WLAN IP:"));
  TEST_ASSERT_FALSE(sim::logContains("geheim"));   // Passwort nie im Log

  runFor(net, 100);
  TEST_ASSERT_TRUE(sim::ntp().configured);
}

// Fall 4: Kabel fällt im Betrieb aus: die Wartezeit zählt ab dem Ausfall.
void test_ausfall_im_betrieb() {
  EthernetConnection net(makeConfig());
  net.begin();
  sim::ethLinkUp();
  runFor(net, 120000);
  sim::ethLinkDown();
  runFor(net, DELAY_MS - 1000);
  TEST_ASSERT_EQUAL_INT(0, sim::wifi().beginCalls);
  TEST_ASSERT_FALSE(net.isOnline());
  runFor(net, 2000);
  TEST_ASSERT_EQUAL_INT(1, sim::wifi().beginCalls);
  TEST_ASSERT_TRUE(net.isOnline());
}

// Fall 5: Ethernet kommt zurück: WLAN wird getrennt. Beim nächsten Ausfall startet
// es wieder, die SDIO-Pins werden aber nur einmal gesetzt (später nicht mehr erlaubt).
void test_rueckkehr_von_ethernet_trennt_wlan() {
  EthernetConnection net(makeConfig());
  net.begin();
  runFor(net, DELAY_MS + 1000);
  TEST_ASSERT_TRUE(sim::wifi().connected);

  sim::ethLinkUp();
  runFor(net, 100);
  TEST_ASSERT_EQUAL_INT(1, sim::wifi().disconnects);
  TEST_ASSERT_FALSE(sim::wifi().connected);
  TEST_ASSERT_TRUE(net.isOnline());
  TEST_ASSERT_TRUE(sim::logContains("WLAN wird getrennt"));

  sim::ethLinkDown();
  TEST_ASSERT_FALSE(net.isOnline());
  runFor(net, DELAY_MS + 1000);
  TEST_ASSERT_EQUAL_INT(2, sim::wifi().beginCalls);
  TEST_ASSERT_EQUAL_INT(1, sim::wifi().setPinsCalls);
  TEST_ASSERT_TRUE(net.isOnline());
}

// Fall 6: Access Point nicht erreichbar: Versuche im festen Abstand, nicht in jeder
// loop(). Sobald er da ist, kommt die Verbindung, und die Versuche hören auf.
void test_access_point_fehlt_wird_wiederholt() {
  sim::wifi().apAvailable = false;
  EthernetConnection net(makeConfig());
  net.begin();
  runFor(net, DELAY_MS + 2 * RETRY_MS + 1000);
  TEST_ASSERT_EQUAL_INT(3, sim::wifi().beginCalls);
  TEST_ASSERT_FALSE(net.isOnline());

  sim::wifi().apAvailable = true;
  runFor(net, RETRY_MS + 1000);
  TEST_ASSERT_EQUAL_INT(4, sim::wifi().beginCalls);
  TEST_ASSERT_TRUE(net.isOnline());
  runFor(net, 3 * RETRY_MS);
  TEST_ASSERT_EQUAL_INT(4, sim::wifi().beginCalls);
}

// Fall 7: WLAN bricht während des Fallbacks ab: offline, dann neuer Versuch.
void test_wlan_abbruch_im_fallback() {
  EthernetConnection net(makeConfig());
  net.begin();
  runFor(net, DELAY_MS + 1000);
  TEST_ASSERT_TRUE(net.isOnline());
  sim::wifi().apAvailable = false;
  sim::wifiApLost();
  TEST_ASSERT_FALSE(net.isOnline());
  TEST_ASSERT_TRUE(sim::logContains("[NET] WLAN getrennt"));

  sim::wifi().apAvailable = true;
  runFor(net, RETRY_MS + 1000);
  TEST_ASSERT_EQUAL_INT(2, sim::wifi().beginCalls);
  TEST_ASSERT_TRUE(net.isOnline());
}

// Fall 8: ESP32-C6 startet nicht: Fehler melden, weiter versuchen, Ethernet bleibt nutzbar.
void test_esp32c6_startet_nicht() {
  sim::wifi().driverOk = false;
  EthernetConnection net(makeConfig());
  net.begin();
  runFor(net, DELAY_MS + RETRY_MS + 1000);
  TEST_ASSERT_EQUAL_INT(2, sim::wifi().beginCalls);
  TEST_ASSERT_TRUE(sim::logContains("FEHLER: WLAN nicht gestartet"));
  TEST_ASSERT_FALSE(net.isOnline());

  sim::ethLinkUp();
  runFor(net, 100);
  TEST_ASSERT_TRUE(net.isOnline());
  TEST_ASSERT_EQUAL_INT(0, sim::wifi().disconnects);   // war nie aktiv
}

// Fall 9: Fallback aktiviert, aber SSID vergessen: laut melden, WLAN bleibt aus.
void test_leere_ssid_schaltet_fallback_ab() {
  EthernetConnection net(makeConfig(true, ""));
  net.begin();
  TEST_ASSERT_TRUE(sim::logContains("WIFI_SSID ist leer"));
  runFor(net, DELAY_MS + 1000);
  TEST_ASSERT_EQUAL_INT(0, sim::wifi().beginCalls);
}

// Fall 10: Firmware des ESP32-C6 älter als vom Core erwartet: Hinweis im Log.
void test_veraltete_c6_firmware_wird_gemeldet() {
  sim::wifi().c6UpdateDue = true;
  EthernetConnection net(makeConfig());
  net.begin();
  runFor(net, DELAY_MS + 1000);
  TEST_ASSERT_TRUE(sim::logContains("Firmware des ESP32-C6"));
  TEST_ASSERT_TRUE(net.isOnline());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fallback_aus_startet_kein_wlan);
  RUN_TEST(test_ethernet_rechtzeitig_da_kein_wlan);
  RUN_TEST(test_start_ohne_ethernet_nutzt_wlan);
  RUN_TEST(test_ausfall_im_betrieb);
  RUN_TEST(test_rueckkehr_von_ethernet_trennt_wlan);
  RUN_TEST(test_access_point_fehlt_wird_wiederholt);
  RUN_TEST(test_wlan_abbruch_im_fallback);
  RUN_TEST(test_esp32c6_startet_nicht);
  RUN_TEST(test_leere_ssid_schaltet_fallback_ab);
  RUN_TEST(test_veraltete_c6_firmware_wird_gemeldet);
  return UNITY_END();
}

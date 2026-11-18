// =============================================================================
//  config.h  –  Zentrale Konfiguration des DAQ-Loggers
// -----------------------------------------------------------------------------
//  ALLE anpassbaren Werte stehen hier. Werte mit  >>> ANPASSEN <<<  vor dem
//  ersten Einsatz prüfen. Diese Datei wird nur von daq_logger.cpp eingebunden;
//  dort werden die Werte über Konstruktoren an die Klassen übergeben.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "temperature_sensors.h"   // SensorMapEntry
#include "modbus_meters.h"         // MeterRegister, RegType, WordOrder

#define FIRMWARE_VERSION "0.5.0"

// -----------------------------------------------------------------------------
//  1) Geräte-Identität und Energielogger (Schnittstelle: martins/.../README.md, Anhang A)
// -----------------------------------------------------------------------------
// Gerätename im Portal = DEVICE_ID-<letzte 3 MAC-Bytes>, z. B. pastler-daq-d65a0e.
// Nur Kleinbuchstaben, Ziffern und "-", höchstens 24 Zeichen.
static const char *DEVICE_ID = "pastler-daq";                            // >>> ANPASSEN <<<

// Ein Secret braucht es hier nicht: Das Gerät meldet sich selbst an und speichert das
// Secret im Flash, sobald es im Portal unter "Neue Geräte" eingerichtet wurde.
static const char *EL_SERVER_URL   = "https://portal.energielogger.de";
static const char *HTTP_USER_AGENT = "pastler-daq/" FIRMWARE_VERSION;

// Werksreset der Portal-Anmeldung: true -> beim ersten Start nach dem Flashen werden
// Secret, gateway_id und claim_token gelöscht; das Gerät meldet sich neu an und
// erscheint im Portal wieder unter "Neue Geräte". Wirkt je Firmware-Build nur EINMAL,
// weitere Neustarts mit derselben Firmware löschen nichts. Danach auf false stellen,
// sonst setzt der nächste Build mit true erneut zurück.
constexpr bool FACTORY_RESET = false;                                    // >>> ANPASSEN <<<

// -----------------------------------------------------------------------------
//  2) Netzwerk und Zeit
// -----------------------------------------------------------------------------
// Ethernet-Parameter kommen aus der Board-Variante, siehe ethernet_connection.h.
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "ptbtime1.ptb.de";
static const char *TZ_INFO      = "CET-1CEST,M3.5.0,M10.5.0/3";   // nur für Logs

// WLAN als Fallback (über den ESP32-C6 auf dem Board). Ethernet hat immer Vorrang:
// WLAN startet erst nach WIFI_FALLBACK_DELAY_MS ohne Ethernet und wird getrennt,
// sobald Ethernet wieder eine IP hat. Das Modbus-Gateway muss auch aus dem WLAN
// erreichbar sein, sonst fehlen in dieser Zeit die Zählerwerte.
constexpr bool ENABLE_WIFI_FALLBACK = true;                             // >>> ANPASSEN <<<
static const char *WIFI_SSID     = "Peters Gerät";                                   // >>> ANPASSEN <<<
static const char *WIFI_PASSWORD = "peterpanther";                                   // >>> ANPASSEN <<<
constexpr uint32_t WIFI_FALLBACK_DELAY_MS = 10UL * 1000;
constexpr uint32_t WIFI_RETRY_MS          = 10UL * 1000;  // neuer Versuch, solange keine IP
// SDIO zum ESP32-C6 auf dem Waveshare-Board: CLK, CMD, D0, D1, D2, D3, RESET
constexpr int8_t WIFI_SDIO_PINS[7] = {18, 19, 14, 15, 16, 17, 54};

// -----------------------------------------------------------------------------
//  3) Zeitverhalten
// -----------------------------------------------------------------------------
// Messabstand: alle Sensoren und Zähler lesen und in die Warteschlange legen. Die
// Schnittstelle empfiehlt 1-5 Minuten für Temperaturen und Leistung. Gesendet wird
// davon unabhängig im Takt des Servers (interval_s, Standard 15 Minuten).
constexpr uint32_t CYCLE_INTERVAL_MS  = 60UL * 1000;                     // >>> ANPASSEN <<<

// Warteschlange bis zur Bestätigung durch den Server: 16 Byte je Wert im PSRAM.
// 20000 Werte reichen bei 15 Sensoren und 1 Messung/min für rund 22 Stunden ohne Netz.
constexpr size_t   QUEUE_MAX_READINGS      = 20000;
constexpr size_t   MAX_READINGS_PER_UPLOAD = 300;       // Server erlaubt höchstens 500
// Abstand der Uploads beim Abbau eines Rückstaus. Rate-Limit des Servers:
// 30 Uploads in 10 Minuten je Gerät, also mindestens 20 s.
constexpr uint32_t UPLOAD_CATCHUP_MS  = 30UL * 1000;

constexpr uint32_t HTTP_TIMEOUT_MS    = 10000;          // wie im ESPHome-Package von Herrn Pfenning
constexpr uint32_t WATCHDOG_TIMEOUT_S = 60;             // > längste blockierende Aktion

// -----------------------------------------------------------------------------
//  4) 1-Wire-Temperatursensoren (DS18B20)
// -----------------------------------------------------------------------------
// GPIOs der 1-Wire-Busse (max. 4). Je Bus ein 4,7-kOhm-Pull-up nach 3,3 V.
// Laut Waveshare-Wiki belegt (NICHT verwenden): 7, 8 (I2C), 9-13, 53 (Audio),
// 39-44 (SD-Karte), 28-35, 49-52 (Ethernet). Pins mit dem Header-Pinout
// abgleichen!                                                           >>> ANPASSEN <<<
static const uint8_t ONEWIRE_PINS[] = {20, 21};
constexpr uint8_t ONEWIRE_BUS_COUNT = sizeof(ONEWIRE_PINS) / sizeof(ONEWIRE_PINS[0]);
constexpr uint8_t TEMP_RESOLUTION_BITS = 12;          // 12 bit = 0,0625 °C, 750 ms

// Zuordnung ROM-ID -> Sensorschlüssel im Energielogger (temp_1 ... temp_12). Der
// Schlüssel gehört fest zum Messpunkt; wird ein Fühler getauscht, bleibt er gleich.
// Fühler ohne Eintrag heißen temp_<ROM-ID>; das ist stabil, passt aber nicht zur
// Konvention. Einmal starten, ROM-IDs aus dem Seriellen Monitor hier eintragen und
// im Portal übernehmen. Busse werden nur beim Start abgesucht.
static const SensorMapEntry SENSOR_MAP[] = {
  // {"temp_1",  {0x28, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // {"temp_2",  {0x28, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};
constexpr size_t SENSOR_MAP_COUNT = sizeof(SENSOR_MAP) / sizeof(SENSOR_MAP[0]);

// Plausibilitätsgrenzen für Heizungstemperaturen (alles außerhalb = Fehlwert).
constexpr float TEMP_MIN_C = -40.0f;
constexpr float TEMP_MAX_C = 110.0f;
// 85,0 °C (Einschaltwert des DS18B20) nur akzeptieren, wenn der letzte gültige
// Wert desselben Sensors höchstens so weit entfernt war.
constexpr float TEMP_85_PLAUSIBLE_DELTA_C = 5.0f;

// -----------------------------------------------------------------------------
//  5) Modbus-Stromzähler
// -----------------------------------------------------------------------------
constexpr bool ENABLE_MODBUS = true;
// RTU: Zähler direkt über einen TTL-RS485-Wandler an UART1.
// TCP_GATEWAY: Zähler hinter einem Modbus-LAN-Gateway (Modbus TCP).
constexpr ModbusTransport MODBUS_TRANSPORT = ModbusTransport::RTU;       // >>> ANPASSEN <<<
constexpr uint32_t MODBUS_TIMEOUT_MS = 1000;                             // je Anfrage

// --- RTU (TTL-RS485-Wandler) ---
// Verdrahtung: ESP-TX -> RXD (bzw. DI), ESP-RX <- TXD (bzw. RO), 3V3 -> VCC, GND -> GND;
// Wandler A+ -> Zähler Klemme 5 (A), B- -> Klemme 6 (B), GND/PE -> Klemme 7.
// NICHT GPIO 37/38 ("TXD"/"RXD" im Pinout): das ist UART0 des Seriellen Monitors
// (USB-C "UART"). Frei am Header sind z. B. GPIO 22 und 23.               >>> ANPASSEN <<<
constexpr int8_t RS485_TX_PIN = 22;   // Header: rechte Reihe, gegenüber GPIO21
constexpr int8_t RS485_RX_PIN = 23;   // Header: linke Reihe, unter SCL/GPIO8
// Wandler mit automatischer Richtungsumschaltung (nur VCC/GND/TXD/RXD): -1.
// Wandler mit DE- und RE-Pin (z. B. MAX485, Beschriftung DI/RO/DE/RE): DE und RE
// verbinden und an diesen GPIO legen.
constexpr int8_t RS485_DE_PIN = -1;                                      // >>> ANPASSEN <<<
// Baudrate wie am Zähler eingestellt (SDM230-Menü "bd"): 1200, 2400, 4800, 9600,
// 19200 oder 38400. 0 = beim Start automatisch suchen (das Ergebnis steht im Log;
// danach fest eintragen, das spart beim Start einige Sekunden).
constexpr uint32_t RS485_BAUD = 9600;                                       // >>> ANPASSEN <<<
// Parität und Stoppbits wie am Zähler (Menü "PrtY"): SERIAL_8N1 (ab Werk),
// SERIAL_8E1, SERIAL_8O1 oder SERIAL_8N2.
constexpr uint32_t RS485_SERIAL_CONFIG = SERIAL_8N1;                     // >>> ANPASSEN <<<

// --- TCP (Modbus-LAN-Gateway) ---
// Bewusst constexpr und KEIN IPAddress: DaqLogger entsteht als globales Objekt in
// einer anderen Übersetzungseinheit und würde eine noch nicht initialisierte
// IPAddress als 0.0.0.0 kopieren (Reihenfolge globaler Konstruktoren).
constexpr uint8_t MODBUS_GATEWAY_IP[4] = {192, 168, 1, 50};
constexpr uint16_t MODBUS_GATEWAY_PORT = 502;

// Eastron SDM230-Modbus (einphasig): Input-Register (Funktion 4), float32, High-Word zuerst.
// Spalten: Zähler(Modbus-Adresse, am Zähler "Add"), Messgröße für den Schlüssel, Art,
//          Einheit, Funktion(3/4), Adresse, Typ, Wortreihenfolge, Skalierung.
// Register eines Zählers hintereinander; das erste Register dient auch der Baudratensuche.
// Schlüssel im Energielogger: modbus_<Adresse>.<Messgröße>, z. B. modbus_1.energy.
// Energie immer als Gesamtstand (Zählerstand), nie als Verbrauch seit dem letzten Wert.
static const MeterRegister METER_REGISTERS[] = {
  {1, "voltage", "voltage", "V",   4, 0x0000, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {1, "current", "current", "A",   4, 0x0006, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {1, "power",   "power",   "W",   4, 0x000C, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  {1, "energy",  "energy",  "kWh", 4, 0x0048, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  // weitere SDM230-Register:
  // {1, "energy_export", "energy", "kWh", 4, 0x004A, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
  // {1, "frequency",     "other",  "Hz",  4, 0x0046, RegType::FLOAT32, WordOrder::HIGH_FIRST, 1.0},
};
static const char *METER_MANUFACTURER = "Eastron";
static const char *METER_MODEL        = "SDM230";                        // >>> ANPASSEN <<<
constexpr size_t METER_REGISTER_COUNT = sizeof(METER_REGISTERS) / sizeof(METER_REGISTERS[0]);

// -----------------------------------------------------------------------------
//  6) Testbetrieb
// -----------------------------------------------------------------------------
// true: statt echter Messungen Testwerte senden (2 Temperaturfühler, 1 Stromzähler),
// z. B. um das Portal ohne angeschlossene Hardware zu prüfen. Netz, NTP und die
// Einrichtung im Portal werden weiterhin gebraucht. 1-Wire und Modbus bleiben unberührt.
// Die Werte sind erkennbar: firmware "<Version>-mock", Fühler "temp_mock_1/2", Zähler 247.
// Für den echten Betrieb unbedingt wieder auf false stellen!
constexpr bool SEND_MOCK_VALUES = false;                                 // >>> ANPASSEN <<<

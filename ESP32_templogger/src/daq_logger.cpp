// =============================================================================
//  daq_logger.cpp  –  Implementierung von DaqLogger
// =============================================================================
#include "daq_logger.h"
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "config.h"

namespace {
// Schreibt die Werte einer Quelle mit gemeinsamem Zeitstempel in die Warteschlange.
class QueueSink : public ReadingSink {
public:
  QueueSink(ReadingQueue &queue, uint16_t offset, uint32_t measuredAt)
      : queue_(queue), offset_(offset), measuredAt_(measuredAt) {}
  void add(uint16_t sensor, double value, uint8_t digits) override {
    queue_.push({value, measuredAt_, static_cast<uint16_t>(offset_ + sensor), digits});
  }

private:
  ReadingQueue &queue_;
  uint16_t      offset_;
  uint32_t      measuredAt_;
};
}  // namespace

DaqLogger::DaqLogger() : DaqLogger(SEND_MOCK_VALUES) {}

DaqLogger::DaqLogger(bool mockValues)
    : network_({DEVICE_ID, NTP_SERVER_1, NTP_SERVER_2, TZ_INFO,
                ENABLE_WIFI_FALLBACK, WIFI_SSID, WIFI_PASSWORD, WIFI_SDIO_PINS,
                WIFI_FALLBACK_DELAY_MS, WIFI_RETRY_MS}),
      temperatures_({ONEWIRE_PINS, ONEWIRE_BUS_COUNT,
                     SENSOR_MAP, SENSOR_MAP_COUNT, TEMP_RESOLUTION_BITS,
                     TEMP_MIN_C, TEMP_MAX_C, TEMP_85_PLAUSIBLE_DELTA_C}),
      meters_({MODBUS_TRANSPORT,
               RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN, RS485_BAUD, RS485_SERIAL_CONFIG,
               IPAddress(MODBUS_GATEWAY_IP[0], MODBUS_GATEWAY_IP[1],
                         MODBUS_GATEWAY_IP[2], MODBUS_GATEWAY_IP[3]),
               MODBUS_GATEWAY_PORT, MODBUS_TIMEOUT_MS,
               METER_REGISTERS, METER_REGISTER_COUNT, METER_MANUFACTURER, METER_MODEL}),
      mockMeter_(CYCLE_INTERVAL_MS / 1000.0),
      api_({HTTP_USER_AGENT, HTTP_TIMEOUT_MS}),
      client_({EL_SERVER_URL, DEVICE_ID, mockValues ? FIRMWARE_VERSION "-mock" : FIRMWARE_VERSION,
               MAX_READINGS_PER_UPLOAD, UPLOAD_CATCHUP_MS, FACTORY_RESET,
               __DATE__ " " __TIME__},     // Build-Kennung: Werksreset nur einmal je Build
              api_, queue_),
      mockValues_(mockValues) {
  // Welche Messquellen aktiv sind, wird hier an EINER Stelle entschieden.
  if (mockValues_) {
    addSource(&mockTemperatures_);
    addSource(&mockMeter_);
    return;
  }
  addSource(&temperatures_);
  if (ENABLE_MODBUS) addSource(&meters_);
}

void DaqLogger::addSource(MeasurementSource *source) {
  if (sourceCount_ < MAX_SOURCES) sources_[sourceCount_++] = source;
}

void DaqLogger::setupWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms     = WATCHDOG_TIMEOUT_S * 1000;
  cfg.idle_core_mask = 0;      // Idle-Tasks nicht überwachen
  cfg.trigger_panic  = true;   // bei Ablauf: Neustart
  // Ist der Watchdog vom Core schon gestartet, wird er umkonfiguriert,
  // sonst neu initialisiert.
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) {
    esp_task_wdt_init(&cfg);
  }
  esp_task_wdt_add(nullptr);   // aktuellen Task (loop) überwachen
}

void DaqLogger::begin() {
  Serial.printf("\n=== DAQ-Logger %s, Firmware %s ===\n", DEVICE_ID, FIRMWARE_VERSION);
  Serial.printf("[SYS] Reset-Grund: %d\n", (int)esp_reset_reason());
  if (mockValues_) {
    Serial.println("[SYS] MOCK-MODUS: sendet Testwerte statt Messwerten (SEND_MOCK_VALUES in config.h)");
  }

  network_.begin();

  std::vector<SensorInfo> sensors;
  for (uint8_t i = 0; i < sourceCount_; i++) {
    sources_[i]->begin();
    sourceOffset_[i] = static_cast<uint16_t>(sensors.size());
    for (uint16_t s = 0; s < sources_[i]->sensorCount(); s++) sensors.push_back(sources_[i]->sensorInfo(s));
  }

  const size_t capacity = queue_.begin(QUEUE_MAX_READINGS);
  if (capacity < QUEUE_MAX_READINGS) {
    Serial.printf("[SYS] WARNUNG: Warteschlange nur %u statt %u Werte (zu wenig Speicher)\n",
                  (unsigned)capacity, (unsigned)QUEUE_MAX_READINGS);
  }

  client_.begin(sensors);
  setupWatchdog();
  cycleStartMs_ = millis() - CYCLE_INTERVAL_MS;   // ersten Zyklus sofort starten
}

void DaqLogger::loop() {
  esp_task_wdt_reset();
  network_.loop();

  const uint32_t now = millis();
  if (now - cycleStartMs_ >= CYCLE_INTERVAL_MS) {
    // Fester Takt ohne Drift. Lief ein Zyklus länger als ein Intervall,
    // wird auf "jetzt" neu ausgerichtet statt Zyklen nachzuholen.
    cycleStartMs_ = (now - cycleStartMs_ < 2 * CYCLE_INTERVAL_MS)
                        ? cycleStartMs_ + CYCLE_INTERVAL_MS : now;
    if (network_.timeValid()) {
      measure();
    } else {
      skippedCycles_++;
      Serial.println("[SYS] Zyklus übersprungen: Uhrzeit noch nicht synchron (NTP)");
    }
    esp_task_wdt_reset();
  }

  client_.loop({network_.isOnline(), network_.timeValid(), network_.usingWifi(), network_.wifiRssi()});
}

void DaqLogger::measure() {
  const uint32_t ts = static_cast<uint32_t>(time(nullptr));   // UTC, für alle Werte
  const uint32_t t0 = millis();

  String summary = mockValues_ ? "MOCK  " : "";
  uint32_t sourceErrors = 0;
  for (uint8_t i = 0; i < sourceCount_; i++) {
    QueueSink sink(queue_, sourceOffset_[i], ts);
    const uint16_t n = sources_[i]->read(sink);
    esp_task_wdt_reset();
    summary += String(n) + "/" + sources_[i]->sensorCount() + "  ";
    sourceErrors += sources_[i]->errorCount();
  }
  Serial.printf("[SYS] Messung: %sWerte, %lu Lesefehler seit Start, %u in der Warteschlange (%lu ms)\n",
                summary.c_str(), (unsigned long)sourceErrors, (unsigned)queue_.size(),
                (unsigned long)(millis() - t0));
}

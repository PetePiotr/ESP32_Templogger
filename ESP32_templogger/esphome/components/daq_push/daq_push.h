// =============================================================================
//  daq_push.h  –  ESPHome-Komponente: alle 10 s messen und sofort senden
// -----------------------------------------------------------------------------
//  Ablauf je Zyklus (update(), Takt = update_interval):
//    1. Ohne Netz oder ohne gültige NTP-Zeit: Zyklus auslassen (skipped++)
//    2. Zeitstempel nehmen, alle DS18B20 (dallas_temp) und alle Modbus-Zähler
//       (modbus_controller) gezielt anstoßen – deren eigenes update_interval
//       steht auf "never", damit nur dieser Takt gilt
//    3. collect_time warten (Wandlung 750 ms, RS485-Abfragen), dabei
//       eintreffende Werte über Callbacks mitschreiben
//    4. Nur Werte verwenden, die in DIESEM Zyklus eingetroffen und plausibel
//       sind; alles andere zählt als read_err. JSON bauen, einmal POSTen.
//  Kein Zwischenspeicher, keine Wiederholung (Entscheidung des Nutzers).
// =============================================================================
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/http_request/http_request.h"
#include "daq_core.h"

namespace esphome::daq_push {

class DaqPush : public PollingComponent {
 public:
  // --- Konfiguration (aus __init__.py) ---
  void set_device_id(const std::string &v) { device_id_ = v; }
  void set_firmware(const std::string &v) { firmware_ = v; }
  void set_url(const std::string &v) { url_ = v; }
  void set_api_key(const std::string &v) { api_key_ = v; }
  void set_collect_time(uint32_t ms) { collect_time_ms_ = ms; }
  void set_temp_limits(float min_c, float max_c, float delta85) { limits_ = {min_c, max_c, delta85}; }
  void set_time(time::RealTimeClock *t) { time_ = t; }
  void set_http(http_request::HttpRequestComponent *h) { http_ = h; }

  // Ein DS18B20: Sensor (Messwert) + derselbe Baustein als PollingComponent (Anstoßen).
  void add_temperature(sensor::Sensor *s, PollingComponent *poll, const std::string &rom, const std::string &name);
  // Ein Modbus-Register: Rohbytes als Hex-Text (modbus_controller-Textsensor).
  void add_meter_register(text_sensor::TextSensor *raw, uint8_t meter, const std::string &quantity,
                          const std::string &unit, daq::RegType type, daq::WordOrder order, double scale);
  // Modbus-Zähler, die je Zyklus abgefragt werden.
  void add_modbus_controller(PollingComponent *c) { controllers_.push_back(c); }

  // --- Component ---
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  struct Temp {
    sensor::Sensor   *sensor;
    PollingComponent *poll;
    std::string       rom, name;
    float             value      = NAN;
    bool              fresh      = false;   // in diesem Zyklus eingetroffen
    float             last_valid = NAN;
  };
  struct Meter {
    text_sensor::TextSensor *raw;
    uint8_t                  meter;
    std::string              quantity, unit;
    daq::RegisterFormat      format;
    std::string              hex;
    bool                     fresh = false;
  };

  void send_();

  std::string device_id_, firmware_, url_, api_key_;
  uint32_t    collect_time_ms_ = 2000;
  daq::TempLimits limits_;
  time::RealTimeClock *time_ = nullptr;
  http_request::HttpRequestComponent *http_ = nullptr;

  std::vector<Temp>              temps_;
  std::vector<Meter>             meters_;
  std::vector<PollingComponent *> controllers_;

  bool     collecting_ = false;   // Zyklus läuft (zwischen Anstoßen und Senden)
  uint32_t cycle_ts_   = 0;       // Unix-Zeit (UTC) des laufenden Zyklus
  daq::Status status_;
};

}  // namespace esphome::daq_push

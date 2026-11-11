// =============================================================================
//  daq_push.cpp  –  Implementierung der ESPHome-Komponente (siehe daq_push.h)
// =============================================================================
#include "daq_push.h"
#include <esp_timer.h>
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

namespace esphome::daq_push {

static const char *const TAG = "daq_push";

void DaqPush::add_temperature(sensor::Sensor *s, PollingComponent *poll, const std::string &rom,
                              const std::string &name) {
  Temp t;
  t.sensor = s;
  t.poll   = poll;
  t.rom    = rom;
  t.name   = name;
  temps_.push_back(t);
}

void DaqPush::add_meter_register(text_sensor::TextSensor *raw, uint8_t meter, const std::string &quantity,
                                 const std::string &unit, daq::RegType type, daq::WordOrder order,
                                 double scale) {
  Meter m;
  m.raw      = raw;
  m.meter    = meter;
  m.quantity = quantity;
  m.unit     = unit;
  m.format   = {type, order, scale};
  meters_.push_back(m);
}

void DaqPush::setup() {
  // Callbacks erst hier registrieren: die Vektoren wachsen danach nicht mehr,
  // die Indizes bleiben gültig.
  for (size_t i = 0; i < temps_.size(); i++) {
    temps_[i].sensor->add_on_raw_state_callback([this, i](float v) {
      temps_[i].value = v;
      temps_[i].fresh = true;
    });
  }
  for (size_t i = 0; i < meters_.size(); i++) {
    meters_[i].raw->add_on_raw_state_callback([this, i](const std::string &hex) {
      meters_[i].hex   = hex;
      meters_[i].fresh = true;
    });
  }
}

void DaqPush::dump_config() {
  ESP_LOGCONFIG(TAG, "DAQ-Push:");
  ESP_LOGCONFIG(TAG, "  Gerät: %s, Firmware %s", device_id_.c_str(), firmware_.c_str());
  ESP_LOGCONFIG(TAG, "  URL: %s", url_.c_str());
  ESP_LOGCONFIG(TAG, "  Sammelzeit: %u ms", (unsigned) collect_time_ms_);
  ESP_LOGCONFIG(TAG, "  Temperaturen: %u, Zählerregister: %u, Zähler: %u", (unsigned) temps_.size(),
                (unsigned) meters_.size(), (unsigned) controllers_.size());
  for (const Temp &t : temps_)
    ESP_LOGCONFIG(TAG, "    %s -> %s", t.rom.c_str(), t.name.empty() ? "(ohne Namen)" : t.name.c_str());
  LOG_UPDATE_INTERVAL(this);
}

void DaqPush::update() {
  if (collecting_) {
    ESP_LOGW(TAG, "Vorheriger Zyklus läuft noch – Zyklus ausgelassen (collect_time zu lang?)");
    status_.skipped++;
    return;
  }
  if (!network::is_connected()) {
    status_.skipped++;
    ESP_LOGW(TAG, "Zyklus übersprungen: kein Netz");
    return;
  }
  const ESPTime now = time_->now();
  if (!now.is_valid()) {
    status_.skipped++;
    ESP_LOGW(TAG, "Zyklus übersprungen: Uhrzeit noch nicht synchron (NTP)");
    return;
  }

  // Ein Zeitstempel für alle Werte dieses Zyklus (Messbeginn, UTC).
  cycle_ts_   = static_cast<uint32_t>(now.timestamp);
  collecting_ = true;
  for (Temp &t : temps_) t.fresh = false;
  for (Meter &m : meters_) m.fresh = false;

  for (Temp &t : temps_) t.poll->update();                 // Wandlung starten (je 750 ms)
  for (PollingComponent *c : controllers_) c->update();     // RS485-Abfragen einreihen

  this->set_timeout("send", collect_time_ms_, [this]() { this->send_(); });
}

void DaqPush::send_() {
  collecting_ = false;

  std::vector<daq::TempReading> temps;
  for (Temp &t : temps_) {
    const float v = t.fresh ? t.value : NAN;
    if (const char *reason = daq::temperatureRejectReason(v, t.last_valid, limits_)) {
      status_.readErr++;
      ESP_LOGW(TAG, "%s verworfen (%s%s)", t.rom.c_str(), t.fresh ? "" : "kein Wert, ", reason);
      continue;
    }
    t.last_valid = v;
    temps.push_back({t.rom, t.name, daq::formatTemperature(v)});
  }

  std::vector<daq::MeterReading> meters;
  for (Meter &m : meters_) {
    std::vector<uint8_t> bytes;
    double v;
    if (!m.fresh || !daq::parseHexBytes(m.hex, bytes) || !daq::decodeRegisters(m.format, bytes, v)) {
      status_.readErr++;
      ESP_LOGW(TAG, "Zähler %u %s: %s", m.meter, m.quantity.c_str(),
               m.fresh ? "Wert nicht dekodierbar" : "keine Antwort in diesem Zyklus");
      continue;
    }
    meters.push_back({m.meter, m.quantity, m.unit, daq::formatMeterValue(m.format, v)});
  }

  status_.uptimeS = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
  const std::string body = daq::buildRequestJson(device_id_, firmware_, cycle_ts_, temps, meters, status_);

  std::vector<http_request::Header> headers = {{"Content-Type", "application/json"}, {"X-Api-Key", api_key_}};
  const uint32_t t0 = millis();
  auto container = http_->post(url_, body, headers);
  int code = -1;
  if (container) {
    code = container->status_code;
    container->end();
  }
  if (http_request::is_success(code)) {
    status_.uploadOk++;
  } else {
    status_.uploadErr++;
  }
  ESP_LOGI(TAG, "Zyklus: Temperaturen %u/%u, Zähler %u/%u, HTTP %d (%u ms), %u Byte", (unsigned) temps.size(),
           (unsigned) temps_.size(), (unsigned) meters.size(), (unsigned) meters_.size(), code,
           (unsigned) (millis() - t0), (unsigned) body.size());
}

}  // namespace esphome::daq_push

// =============================================================================
//  mock_sources.h  –  Testwerte statt echter Messungen (SEND_MOCK_VALUES in config.h)
// -----------------------------------------------------------------------------
//  Zwei Messquellen ohne Hardware, gleiches Format wie die echten Quellen.
//  Die Werte ändern sich je Zyklus und sind im Portal klar zu erkennen:
//    - MockTemperatures: 2 Fühler "temp_mock_1/2", Modell "MOCK"
//    - MockMeter:        1 Zähler mit Slave-ID 247 (kollidiert nicht mit echtem Zähler 1)
// =============================================================================
#pragma once
#include <Arduino.h>
#include "measurement_source.h"

class MockTemperatures : public MeasurementSource {
public:
  void       begin() override {}
  uint16_t   sensorCount() const override { return 2; }
  SensorInfo sensorInfo(uint16_t index) const override;
  uint16_t   read(ReadingSink &out) override;
  uint32_t   errorCount() const override { return 0; }

private:
  uint32_t cycle_ = 0;
};

class MockMeter : public MeasurementSource {
public:
  explicit MockMeter(double cycleSeconds) : cycleSeconds_(cycleSeconds) {}

  void       begin() override {}
  uint16_t   sensorCount() const override { return 3; }
  SensorInfo sensorInfo(uint16_t index) const override;
  uint16_t   read(ReadingSink &out) override;
  uint32_t   errorCount() const override { return 0; }

private:
  double   cycleSeconds_;
  uint32_t cycle_     = 0;
  double   energyKwh_ = 12345.0;
};

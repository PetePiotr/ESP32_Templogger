// =============================================================================
//  daq_esp32p4.ino  –  DAQ-Logger: 1-Wire-Temperaturen + Modbus-Stromzähler
// -----------------------------------------------------------------------------
//  Hardware : Waveshare ESP32-P4-WIFI6-POE-ETH
//  Software : Arduino-ESP32-Core >= 3.2.0, Board "ESP32P4 Dev Module"
//  Libraries: OneWireNg, DallasTemperature, eModbus (+ Async TCP), ArduinoJson 7
//             -> Versionen und Installation siehe LIESMICH.md
//
//  Die gesamte Logik steckt in der Klasse DaqLogger (daq_logger.h);
//  alle Einstellungen stehen in config.h.
// =============================================================================
#include "daq_logger.h"

DaqLogger logger;

void setup() {
  Serial.begin(115200);
  delay(1500);   // Zeit zum Öffnen des Seriellen Monitors
  logger.begin();
}

void loop() {
  logger.loop();
  delay(10);     // anderen Tasks Rechenzeit lassen
}

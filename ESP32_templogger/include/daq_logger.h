// =============================================================================
//  daq_logger.h  –  Ablaufsteuerung: messen, puffern, an den Energielogger senden
// -----------------------------------------------------------------------------
//  DaqLogger setzt das System aus seinen Teilen zusammen (Komposition):
//
//    DaqLogger
//     ├── EthernetConnection   Ethernet + NTP, optional WLAN-Fallback
//     ├── TemperatureSensors   ┐ implementieren MeasurementSource und werden
//     ├── ModbusMeters         ┘ nur über diese Schnittstelle benutzt
//     ├── ReadingQueue         Messwerte bis zur Bestätigung des Servers
//     ├── ApiClient            HTTPS-Transport
//     └── EnergieloggerClient  Anmeldung im Portal und signierter Upload
//
//  Messen und Senden laufen unabhängig: Alle cycleIntervalMs wird gemessen, sobald
//  die Uhr gültig ist, auch ohne Netz. Gesendet wird im Takt des Servers (interval_s).
// =============================================================================
#pragma once
#include <Arduino.h>
#include <vector>
#include "api_client.h"
#include "energielogger_client.h"
#include "ethernet_connection.h"
#include "mock_sources.h"
#include "modbus_meters.h"
#include "reading_queue.h"
#include "temperature_sensors.h"

class DaqLogger {
public:
  DaqLogger();             // baut alle Teile aus den Werten in config.h
  // Wie DaqLogger(), aber mit ausdrücklicher Wahl: true = Testwerte statt Messungen.
  explicit DaqLogger(bool mockValues);

  void begin();            // aus setup() aufrufen
  void loop();             // aus loop() aufrufen

private:
  static constexpr uint8_t MAX_SOURCES = 4;

  void addSource(MeasurementSource *source);
  void measure();
  void setupWatchdog();

  EthernetConnection  network_;
  TemperatureSensors  temperatures_;
  ModbusMeters        meters_;
  MockTemperatures    mockTemperatures_;
  MockMeter           mockMeter_;
  ReadingQueue        queue_;
  ApiClient           api_;
  EnergieloggerClient client_;
  bool                mockValues_;

  MeasurementSource *sources_[MAX_SOURCES] = {};
  uint16_t           sourceOffset_[MAX_SOURCES] = {};   // erster Sensorindex je Quelle
  uint8_t            sourceCount_ = 0;

  uint32_t cycleStartMs_  = 0;   // Startzeit des letzten Zyklus (millis)
  uint32_t skippedCycles_ = 0;   // Zyklen ohne gültige Zeit
};

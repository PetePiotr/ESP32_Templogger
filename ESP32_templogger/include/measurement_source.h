// =============================================================================
//  measurement_source.h  –  Abstrakte Schnittstelle für eine Messquelle
// -----------------------------------------------------------------------------
//  Jede Art von Messgerät (1-Wire-Temperaturen, Modbus-Zähler, ...) ist eine
//  eigene Klasse, die diese Schnittstelle implementiert. Der DaqLogger kennt
//  nur die Schnittstelle: Er meldet die Sensoren jeder Quelle beim Energielogger
//  an und ruft je Zyklus read() auf.
//
//  Eine neue Sensorart hinzufügen = eine neue Unterklasse schreiben und im
//  DaqLogger registrieren; der übrige Code bleibt unverändert.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdio>

// Beschreibung eines Sensors, Anhang A.3 der Energielogger-Schnittstelle.
// key, type, unit sind Pflicht; die übrigen Felder dürfen nullptr sein.
// Die Zeichenketten müssen so lange leben wie die Messquelle.
struct SensorInfo {
  const char *key;            // z. B. "temp_1", "modbus_1.energy" (1-32 Zeichen, [A-Za-z0-9_.:-])
  const char *type;           // z. B. "temperature", "energy", "power", "voltage"
  const char *unit;           // z. B. "degC", "kWh", "W", "V"
  const char *bus;
  const char *address;
  const char *manufacturer;
  const char *model;
};

// Nimmt die Messwerte einer Quelle entgegen.
class ReadingSink {
public:
  virtual ~ReadingSink() = default;
  // sensor: Index innerhalb der Quelle (wie bei sensorInfo()).
  // digits: gültige Stellen im JSON (float32-Quellen 7, Ganzzahlen und DS18B20 10).
  virtual void add(uint16_t sensor, double value, uint8_t digits) = 0;
};

// Zahl fürs JSON, out >= 24 Byte.
inline void formatReadingValue(double value, uint8_t digits, char *out, size_t len) {
  snprintf(out, len, "%.*g", digits, value);
}

class MeasurementSource {
public:
  virtual ~MeasurementSource() = default;

  // Einmalige Initialisierung (Bus absuchen, Verbindung vorbereiten, ...).
  virtual void begin() = 0;

  // Sensoren dieser Quelle; erst nach begin() gültig.
  virtual uint16_t   sensorCount() const = 0;
  virtual SensorInfo sensorInfo(uint16_t index) const = 0;

  // Alle Messwerte lesen und gültige Werte an `out` melden.
  // Rückgabe: Anzahl gemeldeter Werte.
  virtual uint16_t read(ReadingSink &out) = 0;

  // Fehlerhafte Einzelmessungen seit dem Start (für Logs).
  virtual uint32_t errorCount() const = 0;
};

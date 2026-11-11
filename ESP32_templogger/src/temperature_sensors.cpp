// =============================================================================
//  temperature_sensors.cpp  –  Implementierung von TemperatureSensors
// -----------------------------------------------------------------------------
//  Ablauf je Zyklus (read):
//    1. Wandlung auf allen Bussen starten und abwarten (750 ms bei 12 bit)
//    2. Jeden beim Start gefundenen Sensor auslesen, prüfen, gültige Werte melden
//
//  Prüfungen je Wert (rejectReason):
//    - DEVICE_DISCONNECTED_C (-127 °C): keine Antwort oder Scratchpad-CRC falsch
//      (getTempC() prüft die CRC intern)
//    - Plausibilitätsfenster minC .. maxC
//    - 85,0 °C ist der Einschaltwert des DS18B20. Da Heizungsvorläufe wirklich
//      85 °C erreichen können, wird 85,0 nur verworfen, wenn noch kein gültiger
//      Wert vorliegt oder der letzte weiter als plausibleDelta85C entfernt ist.
// =============================================================================
#include "temperature_sensors.h"

void TemperatureSensors::romToHex(const uint8_t *rom, char *out) {
  for (int i = 0; i < 8; i++) snprintf(out + 2 * i, 3, "%02X", rom[i]);
}

const char *TemperatureSensors::mappedKey(const uint8_t *rom) const {
  for (size_t i = 0; i < cfg_.sensorMapCount; i++) {
    if (memcmp(cfg_.sensorMap[i].rom, rom, 8) == 0) return cfg_.sensorMap[i].key;
  }
  return nullptr;
}

SensorInfo TemperatureSensors::sensorInfo(uint16_t index) const {
  const Sensor &s = sensors_[index];
  return {s.key, "temperature", "degC", "onewire", s.address, "Maxim", "DS18B20"};
}

const char *TemperatureSensors::rejectReason(float t, float lastValid) const {
  if (t == DEVICE_DISCONNECTED_C)          return "keine Antwort/CRC";
  if (t < cfg_.minC || t > cfg_.maxC)      return "unplausibel";
  if (t == 85.0f && (isnan(lastValid) || fabsf(t - lastValid) > cfg_.plausibleDelta85C)) {
    return "Einschaltwert 85 °C";
  }
  return nullptr;
}

// Bus b mehrmals absuchen und die ROM-IDs ohne Duplikate nach `out` sammeln
// (höchstens MAX_SENSORS_PER_BUS). Rückgabe: Anzahl gesammelter IDs.
// OneWire::search() prüft die CRC intern und liefert bei einem CRC- oder Busfehler
// nur false, genau wie am Ende der Suche. Ein gestörter Lauf ist also nur daran zu
// erkennen, dass er weniger IDs findet, und die Fühler dahinter fehlen ihm.
uint8_t TemperatureSensors::searchBus(uint8_t b, Sensor *out) {
  uint8_t  total   = 0;
  uint16_t fewest  = UINT16_MAX;   // IDs im schwächsten Suchlauf
  bool     tooMany = false;
  for (uint8_t pass = 0; pass < SEARCH_PASSES; pass++) {
    uint16_t found = 0;
    uint8_t  rom[8];
    wire_[b]->reset_search();
    while (wire_[b]->search(rom)) {
      found++;
      bool known = false;
      for (uint8_t i = 0; i < total && !known; i++) known = memcmp(out[i].rom, rom, 8) == 0;
      if (known) continue;
      if (total < MAX_SENSORS_PER_BUS) {
        memcpy(out[total++].rom, rom, 8);
      } else {
        tooMany = true;
      }
    }
    if (found < fewest) fewest = found;
  }
  if (fewest < total) {
    Serial.printf("[TEMP]   WARNUNG: Suchläufe uneinheitlich (schwächster Lauf %u von %u IDs) – "
                  "Leitung/Pull-up prüfen\n", (unsigned)fewest, (unsigned)total);
  }
  if (tooMany) {
    Serial.printf("[TEMP]   mehr als %u gefunden – nur die ersten %u werden verwendet\n",
                  MAX_SENSORS_PER_BUS, MAX_SENSORS_PER_BUS);
  }
  return total;
}

// In SENSOR_MAP eingetragene Fühler melden, die die Suche nicht gefunden hat.
void TemperatureSensors::warnMissingMappedSensors() const {
  for (size_t m = 0; m < cfg_.sensorMapCount; m++) {
    bool found = false;
    for (uint8_t i = 0; i < sensorCount_ && !found; i++) {
      found = memcmp(sensors_[i].rom, cfg_.sensorMap[m].rom, 8) == 0;
    }
    if (found) continue;
    char hex[17];
    romToHex(cfg_.sensorMap[m].rom, hex);
    Serial.printf("[TEMP] WARNUNG: %s (%s) nicht gefunden – bis zum Neustart ohne Messwert\n",
                  cfg_.sensorMap[m].key, hex);
  }
}

// Alle Busse absuchen (einmal beim Start; neue Sensoren erst nach Neustart).
void TemperatureSensors::begin() {
  const uint8_t buses = min(cfg_.busCount, MAX_BUSES);
  for (uint8_t b = 0; b < buses; b++) {
    wire_[b].reset(new OneWire(cfg_.pins[b]));
    dallas_[b].reset(new DallasTemperature(wire_[b].get()));
    dallas_[b]->begin();                       // sucht den Bus ab
    // Auflösung für ALLE Sensoren des Busses setzen. Das setzt auch die
    // Wartezeit, die requestTemperatures() blockiert (750 ms bei 12 bit).
    // Geschrieben wird nur bei Abweichung (kein unnötiger EEPROM-Zyklus).
    dallas_[b]->setResolution(cfg_.resolutionBits);

    // Sensoren selbst suchen statt getDeviceCount()/getAddress(i): Stört ein
    // Bitfehler die Suche in dallas.begin(), zählt die Bibliothek zu wenige
    // Sensoren, und die fehlenden blieben bis zum Neustart verloren.
    const uint8_t n = searchBus(b, &sensors_[sensorCount_]);
    Serial.printf("[TEMP] Bus %u (GPIO %u): %u Sensor(en)\n", b, cfg_.pins[b], n);
    for (uint8_t i = 0; i < n; i++) {
      Sensor &s = sensors_[sensorCount_];
      s.bus       = b;
      s.lastValid = NAN;
      // Auflösung auch für Sensoren setzen, die dallas.begin() wegen eines
      // Suchfehlers nicht gezählt hat (schreibt nur bei Abweichung).
      dallas_[b]->setResolution(s.rom, cfg_.resolutionBits, true);

      romToHex(s.rom, s.address);
      const char *key = mappedKey(s.rom);
      if (key != nullptr) {
        snprintf(s.key, sizeof(s.key), "%s", key);
      } else {
        snprintf(s.key, sizeof(s.key), "temp_%s", s.address);
      }
      Serial.printf("[TEMP]   %s  ->  %s%s\n", s.address, s.key, key ? "" : " (nicht in SENSOR_MAP)");
      sensorCount_++;
    }
  }
  if (sensorCount_ == 0) {
    Serial.println("[TEMP] WARNUNG: keine Sensoren gefunden – Verdrahtung/Pull-up prüfen");
  }
  warnMissingMappedSensors();
}

uint16_t TemperatureSensors::read(ReadingSink &out) {
  for (uint8_t b = 0; b < cfg_.busCount && b < MAX_BUSES; b++) {
    dallas_[b]->requestTemperatures();   // wartet die Wandlungszeit ab
  }

  uint16_t ok = 0;
  for (uint8_t i = 0; i < sensorCount_; i++) {
    Sensor &s = sensors_[i];
    const float t = dallas_[s.bus]->getTempC(s.rom);

    if (const char *reason = rejectReason(t, s.lastValid)) {
      errorCount_++;
      Serial.printf("[TEMP] %s verworfen (%s, %.2f °C)\n", s.key, reason, t);
      continue;
    }
    s.lastValid = t;
    out.add(i, t, 10);                           // Vielfache von 0,0625 -> exakt
    ok++;
  }
  return ok;
}

// =============================================================================
//  OneWire.h  –  Ersatz für OneWireNg/OneWire mit simulierten DS18B20 am Bus
// -----------------------------------------------------------------------------
//  Die ECHTE Bibliothek DallasTemperature spricht über diese Klasse mit den
//  simulierten Sensoren (sim::SimDS18B20 in sim.h). Nachgebildet werden die
//  1-Wire-Befehle auf Byte-Ebene: Reset/Presence, Search ROM, Match/Skip ROM,
//  Convert T (0x44, mit Wandlungszeit), Read/Write/Copy Scratchpad,
//  Read Power Supply. Nicht nachgebildet: Bit-Timing, Pegel, Leitungslänge.
// =============================================================================
#pragma once
#include "Arduino.h"

namespace sim { struct OneWireBus; }

class OneWire {
public:
  explicit OneWire(uint8_t pin);

  uint8_t reset();
  void    select(const uint8_t rom[8]);
  void    skip();
  void    write(uint8_t v, uint8_t power = 0);
  void    write_bytes(const uint8_t *buf, uint16_t count, bool power = 0);
  uint8_t read();
  void    read_bytes(uint8_t *buf, uint16_t count);
  void    write_bit(uint8_t v);
  uint8_t read_bit();
  void    depower() {}
  void    reset_search() { searchPos_ = 0; }
  void    target_search(uint8_t) {}
  bool    search(uint8_t *newAddr, bool search_mode = true);

  static uint8_t crc8(const uint8_t *addr, uint8_t len);

private:
  enum class Mode { IDLE, AWAIT_FUNCTION, READ_SCRATCH, WRITE_SCRATCH, READ_POWER, CONVERT };
  static constexpr int NONE = -1;
  static constexpr int ALL  = -2;

  sim::OneWireBus &bus();

  uint8_t pin_;
  Mode    mode_ = Mode::IDLE;
  int     target_ = NONE;       // Index des angesprochenen Sensors, NONE oder ALL
  uint8_t buf_[9] = {};
  uint8_t bufPos_ = 0;
  size_t  searchPos_ = 0;
};

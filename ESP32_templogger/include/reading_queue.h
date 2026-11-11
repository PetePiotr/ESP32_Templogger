// =============================================================================
//  reading_queue.h  –  Warteschlange der Messwerte bis zur Bestätigung des Servers
// -----------------------------------------------------------------------------
//  Ringpuffer fester Größe im RAM (auf dem P4 im PSRAM, da groß genug): Erst nach
//  HTTP 200 werden Werte gelöscht (Anhang A.6). Ist er voll, fällt der älteste Wert
//  weg und wird als "dropped" gezählt. Ein Neustart verliert den Inhalt.
// =============================================================================
#pragma once
#include <Arduino.h>

struct Reading {
  double   value;
  uint32_t measuredAt;   // Unix-Sekunden UTC
  uint16_t sensor;       // Index in der Sensorliste des DaqLogger
  uint8_t  digits;       // gültige Stellen im JSON
};

class ReadingQueue {
public:
  ReadingQueue() = default;
  ~ReadingQueue();
  ReadingQueue(const ReadingQueue &) = delete;
  ReadingQueue &operator=(const ReadingQueue &) = delete;

  // Speicher holen; bei zu wenig Speicher halbiert sich die Größe.
  // Rückgabe: tatsächliche Kapazität in Werten (0 = kein Speicher).
  size_t begin(size_t capacity);

  void           push(const Reading &r);
  const Reading &at(size_t index) const;     // 0 = ältester Wert
  void           popFront(size_t count);

  size_t   size() const { return size_; }
  size_t   capacity() const { return capacity_; }
  uint32_t dropped() const { return dropped_; }

private:
  Reading *buf_      = nullptr;
  size_t   capacity_ = 0;
  size_t   head_     = 0;
  size_t   size_     = 0;
  uint32_t dropped_  = 0;
};

// =============================================================================
//  Arduino.h  –  Ersatz des Arduino-Cores für die Host-Simulation (env:native)
// -----------------------------------------------------------------------------
//  Stellt nur bereit, was Firmware und DallasTemperature tatsächlich benutzen.
//  Die Zeit (millis, delay, time) ist SIMULIERT und wird über sim.h gesteuert:
//  ein 1-h-Dauertest läuft so in Sekundenbruchteilen.
//
//  ACHTUNG: time() wird per Makro auf die simulierte Uhr umgebogen. Deshalb
//  werden alle Standard-Header, die die Firmware braucht, HIER vor dem Makro
//  eingebunden (spätere #include sind dann wirkungslos).
// =============================================================================
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using std::isinf;
using std::isnan;

typedef uint8_t byte;
typedef bool    boolean;

#define OUTPUT 0x03
#define INPUT  0x01
#define LOW    0x0
#define HIGH   0x1

// --- simulierte Zeit (Implementierung in sim.cpp) -----------------------------
namespace sim {
uint32_t millisNow();
void     advance(uint32_t ms);
time_t   unixTime(time_t *out);
void     onYield();
void     configTz(const char *tz, const char *s1, const char *s2, const char *s3);
}  // namespace sim

inline uint32_t millis() { return sim::millisNow(); }
inline void     delay(uint32_t ms) { sim::advance(ms); }
inline void     yield() { sim::onYield(); }
inline void     pinMode(uint8_t, uint8_t) {}
inline void     digitalWrite(uint8_t, uint8_t) {}

inline void configTzTime(const char *tz, const char *s1, const char *s2 = nullptr,
                         const char *s3 = nullptr) {
  sim::configTz(tz, s1, s2, s3);
}

#define time(t) ::sim::unixTime(t)

template <typename A, typename B>
inline auto min(A a, B b) -> typename std::common_type<A, B>::type { return a < b ? a : b; }
template <typename A, typename B>
inline auto max(A a, B b) -> typename std::common_type<A, B>::type { return a > b ? a : b; }
template <typename T, typename L, typename H>
inline T constrain(T x, L lo, H hi) { return x < lo ? lo : (x > hi ? hi : x); }

typedef enum { ESP_RST_UNKNOWN = 0, ESP_RST_POWERON = 1 } esp_reset_reason_t;
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }

// --- String (Teilmenge der Arduino-Klasse, intern std::string) ---------------
class String {
public:
  String() {}
  String(const char *c) { if (c) s_ = c; }
  String(const std::string &s) : s_(s) {}
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T v) : s_(std::to_string(v)) {}

  String &operator=(const char *c) { s_ = c ? c : ""; return *this; }
  bool        concat(const char *c) { if (c) s_ += c; return true; }
  const char *c_str() const { return s_.c_str(); }
  unsigned    length() const { return static_cast<unsigned>(s_.size()); }
  const std::string &str() const { return s_; }

  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *c) { if (c) s_ += c; return *this; }
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String &operator+=(T v) { s_ += std::to_string(v); return *this; }

  bool operator==(const char *c) const { return s_ == (c ? c : ""); }
  bool operator==(const String &o) const { return s_ == o.s_; }

  friend String operator+(const String &a, const String &b) { return String(a.s_ + b.s_); }
  friend String operator+(const String &a, const char *b) { return String(a.s_ + (b ? b : "")); }
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  friend String operator+(const String &a, T v) { return String(a.s_ + std::to_string(v)); }

private:
  std::string s_;
};

// --- Serial: Ausgabe landet im Simulations-Log (sim::serialLog()) ------------
class SimSerial {
public:
  void begin(unsigned long) {}
  int  printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void print(const char *s);
  void print(const String &s) { print(s.c_str()); }
  void println(const char *s = "") { print(s); print("\n"); }
  void println(const String &s) { println(s.c_str()); }
};
extern SimSerial Serial;

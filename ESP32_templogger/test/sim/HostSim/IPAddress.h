// Ersatz für IPAddress (nur IPv4, wie in der Firmware verwendet).
#pragma once
#include "Arduino.h"

class IPAddress {
public:
  IPAddress() : b_{0, 0, 0, 0} {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : b_{a, b, c, d} {}
  uint8_t operator[](int i) const { return b_[i]; }
  bool operator==(const IPAddress &o) const { return memcmp(b_, o.b_, 4) == 0; }
  String toString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b_[0], b_[1], b_[2], b_[3]);
    return String(buf);
  }

private:
  uint8_t b_[4];
};

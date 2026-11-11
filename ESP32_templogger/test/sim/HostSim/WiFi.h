// Ersatz für WiFi.h: Verhalten über sim::wifi() einstellbar.
#pragma once
#include "Network.h"

typedef enum { WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 } wl_status_t;

class WiFiClass {
public:
  static bool setPins(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst);
  void        persistent(bool) {}
  static bool setHostname(const char *name);
  wl_status_t begin(const char *ssid, const char *password);
  bool        disconnect();
  IPAddress   localIP();
  int8_t      RSSI() { return -60; }
};
extern WiFiClass WiFi;

bool hostedHasUpdate();

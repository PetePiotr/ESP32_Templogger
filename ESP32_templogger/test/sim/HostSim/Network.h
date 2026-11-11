// Ersatz für Network.h (Arduino-ESP32-Core 3.x): nur die Ereignis-Schnittstelle.
#pragma once
#include "Arduino.h"
#include "IPAddress.h"

typedef enum {
  ARDUINO_EVENT_ETH_START,
  ARDUINO_EVENT_ETH_STOP,
  ARDUINO_EVENT_ETH_CONNECTED,
  ARDUINO_EVENT_ETH_DISCONNECTED,
  ARDUINO_EVENT_ETH_GOT_IP,
  ARDUINO_EVENT_ETH_LOST_IP,
  ARDUINO_EVENT_WIFI_STA_CONNECTED,
  ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
  ARDUINO_EVENT_WIFI_STA_GOT_IP,
  ARDUINO_EVENT_WIFI_STA_LOST_IP,
  ARDUINO_EVENT_MAX
} arduino_event_id_t;

typedef struct { int unused; } arduino_event_info_t;

typedef std::function<void(arduino_event_id_t, arduino_event_info_t)> NetworkEventFuncCb;

class NetworkManager {
public:
  int onEvent(NetworkEventFuncCb cb, arduino_event_id_t = ARDUINO_EVENT_MAX);
};
extern NetworkManager Network;

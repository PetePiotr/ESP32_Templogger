// Ersatz für ETH.h: Link-Ereignisse werden vom Test über sim::ethLinkUp() usw. ausgelöst.
#pragma once
#include "Network.h"

class ETHClass {
public:
  bool      begin();
  bool      setHostname(const char *name);
  IPAddress localIP();
};
extern ETHClass ETH;

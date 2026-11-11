// Ersatz für NetworkClient / Client (keine echte Verbindung; HTTP und Modbus
// werden in HTTPClient.h bzw. ModbusClientTCP.h simuliert).
#pragma once
#include "Arduino.h"

class Client {
public:
  virtual ~Client() = default;
};

class NetworkClient : public Client {};

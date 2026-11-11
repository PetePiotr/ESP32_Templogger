// =============================================================================
//  ModbusClientTCP.h  –  Ersatz für eModbus (nur der von der Firmware genutzte Teil)
// -----------------------------------------------------------------------------
//  Fehlercodes und Aufbau der Antwortnachricht entsprechen eModbus 1.7
//  (ModbusTypeDefs.h, ModbusMessage::getError): Fehlerantwort = [ID, FC|0x80, Code].
//  Hinter syncRequest() steht das simulierte Gateway aus sim.h.
// =============================================================================
#pragma once
#include "Arduino.h"
#include "IPAddress.h"
#include "NetworkClient.h"

enum FunctionCode : uint8_t {
  READ_HOLD_REGISTER  = 0x03,
  READ_INPUT_REGISTER = 0x04,
};

enum Error : uint8_t {
  SUCCESS                = 0x00,
  ILLEGAL_FUNCTION       = 0x01,
  ILLEGAL_DATA_ADDRESS   = 0x02,
  ILLEGAL_DATA_VALUE     = 0x03,
  SERVER_DEVICE_FAILURE  = 0x04,
  ACKNOWLEDGE            = 0x05,
  SERVER_DEVICE_BUSY     = 0x06,
  NEGATIVE_ACKNOWLEDGE   = 0x07,
  MEMORY_PARITY_ERROR    = 0x08,
  GATEWAY_PATH_UNAVAIL   = 0x0A,
  GATEWAY_TARGET_NO_RESP = 0x0B,
  TIMEOUT                = 0xE0,
  INVALID_SERVER         = 0xE1,
  CRC_ERROR              = 0xE2,
  FC_MISMATCH            = 0xE3,
  SERVER_ID_MISMATCH     = 0xE4,
  PACKET_LENGTH_ERROR    = 0xE5,
  PARAMETER_COUNT_ERROR  = 0xE6,
  PARAMETER_LIMIT_ERROR  = 0xE7,
  REQUEST_QUEUE_FULL     = 0xE8,
  ILLEGAL_IP_OR_PORT     = 0xE9,
  IP_CONNECTION_FAILED   = 0xEA,
  TCP_HEAD_MISMATCH      = 0xEB,
  EMPTY_MESSAGE          = 0xEC,
  ASCII_FRAME_ERR        = 0xED,
  ASCII_CRC_ERR          = 0xEE,
  ASCII_INVALID_CHAR     = 0xEF,
  BROADCAST_ERROR        = 0xF0,
  UNDEFINED_ERROR        = 0xFF
};

class ModbusMessage {
public:
  ModbusMessage() {}
  explicit ModbusMessage(std::vector<uint8_t> d) : data_(std::move(d)) {}
  Error getError() const {
    if (data_.size() > 2 && (data_[1] & 0x80)) return static_cast<Error>(data_[2]);
    return SUCCESS;
  }
  size_t size() const { return data_.size(); }
  // wie eModbus: Big-Endian lesen, Rückgabe = Index hinter dem gelesenen Wert
  uint16_t get(uint16_t index, uint16_t &v) const {
    if (index + 1u < data_.size()) v = static_cast<uint16_t>((data_[index] << 8) | data_[index + 1]);
    return index + 2;
  }

private:
  std::vector<uint8_t> data_;
};

class ModbusError {
public:
  explicit ModbusError(Error e) : err_(e) {}
  operator const char *() const;
  operator int() const { return static_cast<int>(err_); }

private:
  Error err_;
};

class ModbusClientTCP {
public:
  explicit ModbusClientTCP(Client &, uint16_t = 100) {}
  void setTimeout(uint32_t timeout = 2000, uint32_t = 200) { timeoutMs_ = timeout; }
  void begin(int = -1);
  uint8_t closeConnectionOnTimeouts(uint8_t = 3) { return 0; }
  bool setTarget(IPAddress host, uint16_t port, uint32_t = 0, uint32_t = 0);
  ModbusMessage syncRequest(uint32_t token, uint8_t serverID, FunctionCode fc,
                            uint16_t addr, uint16_t count);

private:
  uint32_t timeoutMs_ = 2000;
};

// Ersatz für HTTPClient: Jeder POST wird in sim::http() protokolliert; die
// Antwort (Statuscode, Body, Verbindungs- und Antwortzeit) liefert der Test.
#pragma once
#include "NetworkClient.h"
#include "NetworkClientSecure.h"

#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_READ_TIMEOUT       (-11)

class HTTPClient {
public:
  bool begin(NetworkClient &client, const char *url);
  bool begin(NetworkClientSecure &client, const char *url);
  void setTimeout(uint32_t ms) { timeoutMs_ = ms; }
  void setConnectTimeout(int32_t ms) { connectTimeoutMs_ = ms; }
  void addHeader(const String &name, const String &value);
  int  POST(const String &payload);
  String getString() { return String(respBody_); }
  void end() {}
  static String errorToString(int code);

private:
  std::string url_;
  bool        tls_ = false;
  bool        insecure_ = false;
  std::map<std::string, std::string> headers_;
  uint32_t    timeoutMs_ = 5000;
  int32_t     connectTimeoutMs_ = 5000;
  std::string respBody_;
};

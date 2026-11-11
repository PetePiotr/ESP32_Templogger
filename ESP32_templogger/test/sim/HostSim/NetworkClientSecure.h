// Ersatz für NetworkClientSecure: merkt sich nur, wie TLS konfiguriert wurde.
#pragma once
#include "NetworkClient.h"

class NetworkClientSecure : public NetworkClient {
public:
  void setCACert(const char *pem) { caCert = pem; insecure = false; }
  void setInsecure() { insecure = true; }
  const char *caCert   = nullptr;
  bool        insecure = false;
};

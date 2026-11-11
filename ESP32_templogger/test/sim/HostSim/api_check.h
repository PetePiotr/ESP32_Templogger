// =============================================================================
//  api_check.h  –  Prüft einen Request-Body gegen API_SCHNITTSTELLE.md (v0.3)
// -----------------------------------------------------------------------------
//  checkApiBody(body) liefert "" wenn alles stimmt, sonst eine Fehlerliste.
//  Geprüft werden Feldnamen (keine fehlenden, keine zusätzlichen), Typen,
//  Wertebereiche, Längen, Eindeutigkeit und das Zahlenformat im Rohtext
//  (Temperaturen mit genau 4 Nachkommastellen, Vielfache von 0,0625).
// =============================================================================
#pragma once
#include <ArduinoJson.h>
#include <regex>
#include <set>
#include "sim.h"

namespace apicheck {

inline bool onlyKeys(JsonObjectConst o, std::set<std::string> allowed, std::string &err,
                     const std::string &where) {
  bool ok = true;
  for (JsonPairConst kv : o) {
    if (!allowed.erase(kv.key().c_str())) { err += where + ": unerwartetes Feld " + kv.key().c_str() + "\n"; ok = false; }
  }
  for (auto &missing : allowed) { err += where + ": Feld fehlt: " + missing + "\n"; ok = false; }
  return ok;
}

inline bool isUInt(JsonVariantConst v) { return v.is<uint32_t>() && !v.is<bool>(); }

inline std::string checkApiBody(const std::string &body, bool expectMeters = true) {
  std::string err;
  JsonDocument doc;
  if (DeserializationError e = deserializeJson(doc, body)) return std::string("kein gültiges JSON: ") + e.c_str();
  if (!doc.is<JsonObjectConst>()) return "Wurzel ist kein Objekt";
  JsonObjectConst root = doc.as<JsonObjectConst>();

  std::set<std::string> keys = {"device_id", "firmware", "ts", "temperatures", "status"};
  if (expectMeters) keys.insert("meter_readings");
  onlyKeys(root, keys, err, "Wurzel");

  const char *dev = root["device_id"];
  if (!dev || !*dev || strlen(dev) > 32) err += "device_id ungültig\n";
  if (!root["firmware"].is<const char *>()) err += "firmware fehlt/kein String\n";
  if (!root["ts"].is<uint32_t>() || root["ts"].as<uint32_t>() < 1704067200u)
    err += "ts ungültig (keine Unix-Sekunden nach 2024)\n";

  // --- temperatures ---
  if (!root["temperatures"].is<JsonArrayConst>()) err += "temperatures ist keine Liste\n";
  std::set<std::string> roms;
  for (JsonObjectConst t : root["temperatures"].as<JsonArrayConst>()) {
    onlyKeys(t, {"sensor", "name", "c"}, err, "temperatures[]");
    const char *rom = t["sensor"];
    if (!rom || !std::regex_match(rom, std::regex("^[0-9A-F]{16}$"))) {
      err += "sensor keine 16 Hex-Großbuchstaben\n";
    } else {
      uint8_t b[8];
      for (int i = 0; i < 8; i++) b[i] = static_cast<uint8_t>(std::stoul(std::string(rom + 2 * i, 2), nullptr, 16));
      if (sim::crc8(b, 7) != b[7]) err += std::string("ROM-CRC falsch: ") + rom + "\n";
      if (b[0] != 0x28) err += std::string("keine DS18B20-Familie: ") + rom + "\n";
      if (!roms.insert(rom).second) err += std::string("sensor doppelt: ") + rom + "\n";
    }
    if (!t["name"].isNull() && (!t["name"].is<const char *>() || strlen(t["name"].as<const char *>()) == 0 ||
                                strlen(t["name"].as<const char *>()) > 31))
      err += "name weder null noch String 1..31\n";
    if (!t["c"].is<double>()) { err += "c keine Zahl\n"; continue; }
    const double c = t["c"];
    if (!std::isfinite(c) || c < -40.0 || c > 110.0) err += "c außerhalb -40..110\n";
    if (std::fabs(c * 16.0 - std::round(c * 16.0)) > 1e-9) err += "c kein Vielfaches von 0,0625\n";
  }
  // Rohtext: jede Temperatur mit genau 4 Nachkommastellen
  static const std::regex cRe("\"c\":(-?[0-9.eE+-]+)");
  for (auto it = std::sregex_iterator(body.begin(), body.end(), cRe); it != std::sregex_iterator(); ++it) {
    const std::string num = (*it)[1];
    if (!std::regex_match(num, std::regex("^-?[0-9]+\\.[0-9]{4}$"))) err += "c nicht im Format x.xxxx: " + num + "\n";
  }

  // --- meter_readings ---
  if (expectMeters) {
    if (!root["meter_readings"].is<JsonArrayConst>()) err += "meter_readings ist keine Liste\n";
    std::set<std::string> channels;
    for (JsonObjectConst m : root["meter_readings"].as<JsonArrayConst>()) {
      onlyKeys(m, {"meter", "quantity", "unit", "v"}, err, "meter_readings[]");
      if (!m["meter"].is<int>() || m["meter"].as<int>() < 1 || m["meter"].as<int>() > 247) err += "meter nicht 1..247\n";
      const char *q = m["quantity"], *u = m["unit"];
      if (!q || !*q || strlen(q) > 31) err += "quantity ungültig\n";
      if (!u || !*u || strlen(u) > 8) err += "unit ungültig\n";
      if (!m["v"].is<double>() || !std::isfinite(m["v"].as<double>())) err += "v keine endliche Zahl\n";
      if (q && !channels.insert(std::to_string(m["meter"].as<int>()) + "/" + q).second) err += "Zählerkanal doppelt\n";
    }
  }

  // --- status ---
  if (!root["status"].is<JsonObjectConst>()) {
    err += "status fehlt\n";
  } else {
    JsonObjectConst s = root["status"];
    onlyKeys(s, {"uptime_s", "upload_ok", "upload_err", "skipped", "read_err"}, err, "status");
    for (const char *k : {"uptime_s", "upload_ok", "upload_err", "skipped", "read_err"})
      if (!isUInt(s[k])) err += std::string("status.") + k + " keine nichtnegative Ganzzahl\n";
  }
  return err;
}

}  // namespace apicheck

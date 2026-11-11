# Host-Tests (Simulation auf dem PC)

Start: PlatformIO-Seitenleiste → **native → Advanced → Test**, oder

```
pio test -e native
python test/check_requests_with_api.py      # Gegenprobe mit test_api_server.py
```

## Aufbau

| Teil | echt | simuliert (`test/sim/HostSim`) |
| --- | --- | --- |
| Firmware-Klassen aus `src/` (ohne `main.cpp`) | ✔ | |
| DallasTemperature, ArduinoJson | ✔ | |
| DS18B20 am 1-Wire-Bus (Suche, Scratchpad, CRC, Wandlungszeit, Einschaltwert) | | ✔ Byte-Ebene |
| Modbus-TCP-Gateway + Zähler (Register, Ausnahmen, Timeouts) | | ✔ |
| Ethernet- und WLAN-Ereignisse, NTP, HTTP-POST, Task-Watchdog, `millis()` | | ✔ |

Die Uhr ist simuliert: der 1-h-Dauertest läuft in Sekundenbruchteilen.
Jeder gesendete Body wird gegen `API_SCHNITTSTELLE.md` geprüft (`api_check.h`);
die 360 Bodies des Dauertests landen in `sim_requests.jsonl`.

## Testfälle

- `test_temperature_sensors` (16): 12 Fühler an 2 Bussen, 9→12-bit-Umstellung,
  Wandlungszeit, Einschaltwert 85 °C (Original/Nachbau), echte 85 °C, Leitungsbruch,
  Bitfehler, Kurzschluss, keine Fühler, unplausible Werte, > 16 Fühler,
  Suchfehler beim Start (vorübergehend / in der Mitte / dauerhaft, wie bei OneWireNg als Abbruch)
- `test_modbus_meters` (12): SDM630 float32, Wortreihenfolge, Ganzzahlregister,
  Skalierung, NaN/Inf, Zähler stumm (0x0B / Timeout), Gateway aus, falsche Adresse,
  verstümmelte Antwort, Wiederkehr, Gateway-IP 0.0.0.0
- `test_esphome_core` (9): Fachlogik der ESPHome-Variante (`esphome/components/daq_push/daq_core`):
  Plausibilität/85-°C-Regel, Hex-Rohregister, alle Registertypen und Skalierung (double),
  NaN/Inf, Request gegen API-Spezifikation, leere Listen
- `test_system` (14): Start, Start ohne Netz/NTP, 1 h Dauerbetrieb, API aus, HTTP 401/500,
  Kabel gezogen, Gateway aus, langsame API, sehr langsames Netz (> 10 s), millis-Überlauf,
  alles ausgefallen, Fühlerausfall im Betrieb, Gateway-IP aus config.h, Mock-Modus

- `test_network` (10): WLAN-Fallback: aus, Ethernet rechtzeitig da, Start ohne Ethernet,
  Ausfall im Betrieb, Rückkehr von Ethernet, Access Point fehlt, WLAN-Abbruch, ESP32-C6
  startet nicht, leere SSID, Firmware-Warnung

## Grenzen

Nicht abgedeckt (nur am Board prüfbar): 1-Wire-Timing, Leitungslänge, Pull-up, OneWireNg
auf dem ESP32-P4; echte eModbus-Tasks/TCP und Registertabellen der echten Zähler;
echtes WLAN und ESP-Hosted mit dem ESP32-C6, Routing bei Netzwechsel;
TLS/HTTPS, echte Netzlatenz; Heap/Stack/Flash; FreeRTOS-Nebenläufigkeit (Ereignisse
kommen in der Simulation synchron); der Compilerlauf für den P4.

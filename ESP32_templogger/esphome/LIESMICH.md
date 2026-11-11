# DAQ-Logger auf ESPHome – Installation und Inbetriebnahme

Firmware 2.0.0-esphome · geprüft mit **ESPHome 2026.9.1** (`esphome config` und
Code-Erzeugung; der Compilerlauf für den ESP32-P4 steht noch aus)

Gleiche Aufgabe wie die Arduino-Variante im Projektordner (bleibt als Referenz erhalten):
alle 10 s Temperaturen und Zählerwerte messen und **sofort** als ein JSON-Request
(`API_SCHNITTSTELLE.md` v0.3, unverändert) an die Python-API senden. Kein Zwischenspeicher.

## Unterschiede zur Arduino-Variante

| | Arduino (src/) | ESPHome (esphome/) |
| --- | --- | --- |
| Stromzähler | Modbus TCP über LAN-Gateway | **Modbus RTU direkt** über Waveshare TTL-RS485 (C) |
| Fühler finden | automatisch beim Start | ROM-Adresse je Fühler in der YAML (steht nach dem ersten Start im Log) |
| Ablauf/JSON | Klassen in `src/` | eigene Komponente `components/daq_push` (C++) |
| 1-Wire, Modbus, Ethernet, NTP, HTTP | Bibliotheken | ESPHome-Bausteine `dallas_temp`, `modbus_controller`, `ethernet`, `sntp`, `http_request` |
| Updates | USB | USB, danach **OTA über Ethernet** |

## Aufbau

```
esphome/
  heizung01.yaml              Gerätekonfiguration (alle Einstellungen)
  secrets.yaml.example        Vorlage für API-URL, API-Key, OTA-Passwort
  components/daq_push/
    __init__.py               YAML-Schema + Prüfungen + Code-Erzeugung
    daq_push.h/.cpp           Takt, Sensoren anstoßen, Werte sammeln, POST
    daq_core.h/.cpp           Fachlogik ohne ESPHome (auf dem PC getestet)
```

Ablauf je Zyklus (`daq_push`, alle 10 s): ohne Netz/NTP auslassen (`skipped`) → alle
`dallas_temp` und `modbus_controller` gezielt anstoßen (deren `update_interval: never`) →
`collect_time` (2 s) warten → nur Werte aus **diesem** Zyklus verwenden, prüfen
(−40…110 °C, 85-°C-Regel, NaN), Modbus-Rohregister als `double` dekodieren → POST.

Die Konfigurationsprüfung leitet ab und kontrolliert, damit nichts doppelt gepflegt wird:
ROM-ID ← `address` des Fühlers, Zählernummer ← `address` des `modbus_controller`.
Sie lehnt ab: Fühler ohne `address`, `update_interval` ≠ `never`, Modbus-Textsensor ohne
`raw_encode: HEXBYTES` oder mit falscher `response_size` (4 Byte für 32-bit-Typen, 2 für 16-bit).

## 1. ESPHome installieren (PC)

```
python -m venv .venv
.venv\Scripts\pip install esphome==2026.9.1
```

## 2. Konfigurieren

1. `secrets.yaml.example` nach `secrets.yaml` kopieren und ausfüllen.
2. In `heizung01.yaml` alle Stellen mit `>>> ANPASSEN <<<` prüfen:
   - `board`: `esp32-p4` (Chip vor Rev. 3.00, 360 MHz) oder `esp32-p4_r3`
   - GPIOs für 1-Wire (20/21) und RS485 (22/23) **am Header-Pinout prüfen**
   - Baudrate/Parität und Registertabelle des echten Zählers (Beispiel: Eastron SDM630)
3. Fühleradressen: einmal flashen, im Log stehen die gefundenen Adressen
   (`Found devices: 0x...`). Je Fühler einen `dallas_temp`-Eintrag anlegen **und** ihn bei
   `daq_push: temperatures:` mit Namen eintragen.

## 3. Prüfen, bauen, flashen

```
cd esphome
esphome config heizung01.yaml       # nur Prüfung
esphome run heizung01.yaml          # bauen + flashen (erstes Mal per USB), danach OTA
esphome logs heizung01.yaml         # Log ansehen
```

Beim ersten `compile`/`run` lädt ESPHome ESP-IDF und die Toolchain herunter (mehrere GB).

## 4. Testen ohne Hardware

Die Fachlogik (`daq_core`) läuft in den PlatformIO-Host-Tests mit:
`pio test -e native -f test_esphome_core`. Den gesendeten JSON-Aufbau prüft dort dieselbe
API-Prüfung wie bei der Arduino-Variante.

## Grenzen / offen

- Compilerlauf und Hardwaretest für den ESP32-P4 stehen noch aus.
- ESPHome-Sensorwerte sind `float`; das betrifft nur die Temperaturen (DS18B20: 1/16 °C,
  exakt darstellbar). Zählerwerte laufen als Rohbytes und werden in `double` dekodiert.
- Neue Fühler erfordern einen YAML-Eintrag und ein OTA-Update (keine automatische Suche).
- Das Senden blockiert bis zu 2 × 5 s; `http_request.watchdog_timeout` ist daher 15 s.

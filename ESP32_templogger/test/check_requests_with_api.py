#!/usr/bin/env python3
"""
Gegenprobe: Die im Systemtest erzeugten Requests (sim_requests.jsonl, 1-h-Dauertest)
durch die Prüf- und Speicherlogik von test_api_server.py schicken.

So wird nicht nur gegen die Beschreibung in API_SCHNITTSTELLE.md geprüft, sondern
gegen den Code, der die Daten tatsächlich annimmt (parse() + Store, SQLite im RAM).

Aufruf (im Projektordner, nach "pio test -e native"):
    python test/check_requests_with_api.py
    python test/check_requests_with_api.py sim_requests.jsonl ../../claudes/test_api_server.py
"""
import importlib.util
import json
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
jsonl = Path(sys.argv[1]) if len(sys.argv) > 1 else PROJECT / "sim_requests.jsonl"
server = Path(sys.argv[2]) if len(sys.argv) > 2 else PROJECT.parent.parent / "claudes" / "test_api_server.py"

if not jsonl.is_file():
    sys.exit(f"{jsonl} fehlt – zuerst 'pio test -e native -f test_system' ausführen.")
if not server.is_file():
    sys.exit(f"{server} fehlt – Pfad zu test_api_server.py als 2. Argument angeben.")

spec = importlib.util.spec_from_file_location("test_api_server", server)
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)
store = api.Store(":memory:")

bodies = [line for line in jsonl.read_text(encoding="utf-8").splitlines() if line.strip()]
total_t = total_m = 0
for i, line in enumerate(bodies):
    data = json.loads(line)
    try:
        device, t_rows, m_rows = api.parse(data)            # wirft BadRequest -> HTTP 400
    except api.BadRequest as exc:
        sys.exit(f"Request {i}: API würde mit 400 ablehnen: {exc}\n{line}")
    if data["device_id"] not in api.API_KEYS:
        sys.exit(f"Request {i}: device_id {device!r} hat keinen API-Key in test_api_server.py")
    new_t, new_m = store.save(device, data.get("firmware", ""), t_rows, m_rows)
    if (new_t, new_m) != (len(t_rows), len(m_rows)):
        sys.exit(f"Request {i}: nur {new_t}/{len(t_rows)} Temperaturen, {new_m}/{len(m_rows)} Zählerwerte neu")
    total_t += new_t
    total_m += new_m

# Duplikat-Regel: denselben Request nochmals senden -> nichts Neues
device, t_rows, m_rows = api.parse(json.loads(bodies[-1]))
dup = store.save(device, "", t_rows, m_rows)
db = store.db
sensors = db.execute("SELECT COUNT(*) FROM temp_sensor").fetchone()[0]
channels = db.execute("SELECT COUNT(*) FROM meter_channel").fetchone()[0]
first, last = db.execute("SELECT MIN(ts), MAX(ts) FROM temp_measurement").fetchone()

print(f"{len(bodies)} Requests von der API angenommen")
print(f"  Temperaturwerte gespeichert: {total_t}  ({sensors} Sensoren)")
print(f"  Zählerwerte gespeichert:     {total_m}  ({channels} Kanäle)")
print(f"  Zeitraum (UTC): {first} .. {last}")
print(f"  Duplikat erneut gesendet -> neu: {dup}  (erwartet (0, 0))")
if dup != (0, 0):
    sys.exit("FEHLER: Duplikat wurde gespeichert")
print("OK")

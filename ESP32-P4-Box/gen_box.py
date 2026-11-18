"""Erzeugt das SolidCraft-Skript fuer eine Box um das Waveshare ESP32-P4-WIFI6-POE-ETH.

Aufruf:  python gen_box.py            -> schreibt esp32p4_box.json
Danach:  solidcraft-cli run esp32p4_box.json --save ESP32-P4-Box.sc

Koordinaten (mm): Innenraum x 0..IN_X, y 0..IN_Y, Boden z 0..FLOOR.
Vorderwand (y < 0) = Kante mit USB, RJ45 und TF-Karte, dazu 2x GX12 fuer die 1-Wire-Busse.
Rueckwand (y > IN_Y) = SMA-Antenne und GX16 fuer RS485.
Innen rechts: Traegerplatine (1-Wire-Verteiler + RS485-Wandler) auf 4 Domen, 2 Kabelbinder-Sockel.
"""
import json

# ---------------- Box ----------------
IN_X, IN_Y, IN_H = 100.0, 80.0, 35.0   # Innenmass (Breite, Tiefe, Hoehe ueber Boden)
WALL, FLOOR, LID = 2.0, 2.0, 2.0       # Wand-, Boden-, Deckelstaerke
R_OUT = 3.0                            # Radius der senkrechten Aussenkanten

# Deckelschrauben: M3 in Eckdome (Kernloch 2,5 fuer selbstschneidende Schraube)
POST_D, POST_OFF, POST_PILOT, POST_PILOT_DEPTH = 8.0, 3.5, 2.5, 14.0
LID_HOLE = 3.4                         # Durchgang M3
LIP_H, LIP_T, LIP_GAP = 3.0, 1.5, 0.3  # Zentrierrand unter dem Deckel

# ---------------- Platine (Waveshare-Masszeichnung) ----------------
PCB_X, PCB_Y, PCB_T = 55.0, 68.0, 1.6
HOLES_UV = [(3.5, 25.0), (51.5, 25.0), (3.5, 58.0), (51.5, 58.0)]  # u ab linker Kante, v ab Steckerkante
BOARD_X0 = 8.5          # linke Platinenkante im Innenraum
BOARD_Y0 = 0.5          # Abstand Steckerkante zur Vorderwand (innen)
STANDOFF_H, STANDOFF_D, STANDOFF_PILOT = 5.0, 6.0, 2.2   # M2.5 selbstschneidend

# Ausschnitte in der Vorderwand (u-Bereich auf der Platine, Hoehe relativ zur Platinenoberseite)
USB_U = (4.5, 19.5);   USB_TOP = 15.5      # USB-A + USB-C gestapelt
RJ45_U = (21.33, 38.53); RJ45_TOP = 14.5   # RJ45 / PoE, Mitte 29,93
TF_U = (38.0, 51.8)                        # TF-Karte, Mitte 45,26
TF_Z = (-1.0 - PCB_T, 3.5)                 # von 1 mm unter der Platine bis 3,5 mm darueber

# Antennenbuchse (SMA) in der Rueckwand fuer den IPEX-Anschluss des ESP32-C6
SMA_X, SMA_Z, SMA_D = 85.0, 22.0, 6.5

# 1-Wire-Buchsen: 2x GX12 3-polig (3V3, GND, DQ), Gewinde M12x1, Mutter innen.
# Vorderwand rechts neben RJ45/TF: zeigt im Schaltschrank nach unten, die Buskabel gehen wie LAN/USB nach unten weg.
# Ausschnitt laut Herstellerzeichnung 12,1 mm (+0,1); fuer FDM etwas groesser, damit das Gewinde ohne Nacharbeit passt.
OW_HOLE_D = 12.4
OW_X = {"Bus 1": 62.0, "Bus 2": 83.0}  # Buchsenmitte (x); 21 mm Abstand, Stecker ca. 15-16 mm Durchmesser
OW_Z = 23.0                            # ueber dem TF-Ausschnitt; Mutter innen bleibt unter dem Deckel-Zentrierrand (z 34)

# RS485-Buchse (A, B, GND): GX16 3-polig (M16x1), andere Groesse als die GX12 -> nicht verwechselbar.
# Rueckwand (im Schaltschrank oben) neben der SMA-Buchse; Vorderwand ist voll. Ausschnitt 16 mm, fuer FDM 16,4.
# Hinter der Buchse: Abstand zur Stiftleiste der Platine ca. 0,5 mm in x (Dupont-Stecker an den letzten Pins pruefen).
RS_HOLE_D, RS_X, RS_Z = 16.4, 68.0, 22.0

# Traegerplatine (Lochraster, auf ca. 34 x 44 mm zugeschnitten) im freien Bereich rechts neben der Platine:
# 1-Wire-Verteiler (Klemmen, 2x 4,7 kOhm Pull-up DQ->3V3) und RS485-Wandler Waveshare TTL TO RS485 (C)
# (42,8 x 15,2 x 4,75 mm, ohne Befestigungsloecher -> mit Klebepad oder Kabelbinder auf die Traegerplatine).
CARRIER_X, CARRIER_Y = (65.0, 99.0), (19.0, 63.0)
CARRIER_HOLES = [(68.0 + i * 11 * 2.54, 22.0 + j * 15 * 2.54) for j in (0, 1) for i in (0, 1)]  # Raster 27,94 x 38,1
CARRIER_H = 6.0                        # Dome; Loetseite bleibt 1,5 mm ueber den Clip-Schraubenkoepfen
RS485_MOD = (42.8, 15.2, 4.75)         # Laenge (y), Breite (x), Hoehe

# Kabelbinder-Sockel mit Tunnel (Binder bis 3 mm): hinter den 1-Wire-Buchsen und unter der RS485-Buchse
TIE_MOUNTS = [(72.5, 11.0, "x"), (68.0, 70.5, "y")]   # Mitte (x, y), Richtung des Tunnels
TIE_L, TIE_W, TIE_H = 7.0, 6.0, 4.5    # Laenge in Tunnelrichtung, Breite, Hoehe ueber dem Boden
TIE_SLOT_W, TIE_SLOT_H = 3.4, 1.8

# Beschriftung aussen, 0,5 mm vertieft (Innen: Etiketten "Bus 1 GPIO 20", "Bus 2 GPIO 21", "RS485 A/B/GND")
LABEL_H, LABEL_DEPTH = 3.5, 0.5
LABEL_FONT = "Arial"                   # fett, damit die Striche bei 3,5 mm Schrifthoehe druckbar breit bleiben

# Lueftungsschlitze im Deckel (ueber der Platine)
VENT_W, VENT_Y = 2.0, (22.0, 58.0)
VENT_X = [22.0 + 5.0 * i for i in range(8)]

# Hutschienen-Clip (TS35 nach EN 60715), eigenes Druckteil, von innen mit 4x M3x6 an den Boden geschraubt.
# Schiene laeuft entlang X, mittig unter der Box; Steckerseite (y < 0) zeigt im Schaltschrank nach unten.
DIN = True
RAIL_W, RAIL_T, RAIL_LIP_W = 35.0, 1.0, 5.0
RAIL_YC = IN_Y / 2                     # Schienenmitte (y)
CLIP_X = (2.0, 98.0)                   # Laenge des Clips
CLIP_T = 4.0                           # Dicke der Grundplatte (Abstand Boxboden -> Schiene)
HOOK_GAP, HOOK_T, HOOK_LIP_T, HOOK_GRIP = 0.3, 2.5, 2.0, 1.8   # Spiel, Wand, Lippe, Untergriff
SPRING_T, SPRING_X = 1.6, (10.0, 90.0) # Federbalken (biegt sich in -y)
SPRING_GAP = 0.5                       # Spalt Federbalken/Grundplatte
LATCH_X = (45.0, 55.0)                 # Rastnase mit Zuglasche
TAB_Y0, TAB_T = 0.0, 3.0               # Zuglasche bis an die Unterkante, Schlitz fuer Schraubendreher
CLIP_SCREWS = [(6.0, 30.0), (6.0, 50.0), (94.0, 30.0), (94.0, 50.0)]
CLIP_PILOT, FLOOR_HOLE = 2.5, 3.4

# ---------------------------------------------------------------
TOP = FLOOR + IN_H                     # Oberkante Wand = Unterseite Deckel
PCB_Z0 = FLOOR + STANDOFF_H            # Platinenunterseite
PCB_Z1 = PCB_Z0 + PCB_T                # Platinenoberseite
OX0, OY0 = -WALL, -WALL
OX, OY = IN_X + 2 * WALL, IN_Y + 2 * WALL

cmds = []
def c(command, **params):
    cmds.append({"command": command, "params": params})

def bx(name_or_none, x0, y0, z0, x1, y1, z1, **kw):
    p = dict(corner=[x0, y0, z0], length=round(x1 - x0, 4), width=round(y1 - y0, 4), height=round(z1 - z0, 4), **kw)
    if name_or_none:
        p["body_name"] = name_or_none
    c("solid.box", **p)

posts = [(POST_OFF, POST_OFF), (IN_X - POST_OFF, POST_OFF),
         (IN_X - POST_OFF, IN_Y - POST_OFF), (POST_OFF, IN_Y - POST_OFF)]
outer_edges = lambda z: [[OX0, OY0, z], [OX0 + OX, OY0, z], [OX0 + OX, OY0 + OY, z], [OX0, OY0 + OY, z]]

# ---- Unterteil ----
bx("Unterteil", OX0, OY0, 0, OX0 + OX, OY0 + OY, TOP)
c("solid.fillet", edges=outer_edges(TOP / 2), radius=R_OUT, body="Unterteil")
c("solid.shell", faces=[[IN_X / 2, IN_Y / 2, TOP]], thickness=WALL, direction="inside", body="Unterteil")
for (x, y) in posts:
    c("plastic.boss", position=[x, y, FLOOR], diameter=POST_D, height=IN_H,
      hole_diameter=POST_PILOT, hole_depth=POST_PILOT_DEPTH, body="Unterteil")
for (u, v) in HOLES_UV:
    c("plastic.boss", position=[BOARD_X0 + u, BOARD_Y0 + v, FLOOR], diameter=STANDOFF_D, height=STANDOFF_H,
      hole_diameter=STANDOFF_PILOT, fillet=0.5, body="Unterteil")
for (x, y) in CARRIER_HOLES:
    c("plastic.boss", position=[x, y, FLOOR], diameter=STANDOFF_D, height=CARRIER_H,
      hole_diameter=STANDOFF_PILOT, fillet=0.5, body="Unterteil")

cut = dict(operation="cut", targets=["Unterteil"])
yf0, yf1 = OY0 - 1, 1.0   # durch die Vorderwand
bx(None, BOARD_X0 + USB_U[0], yf0, PCB_Z1 - 0.5, BOARD_X0 + USB_U[1], yf1, PCB_Z1 + USB_TOP, name="Ausschnitt USB", **cut)
bx(None, BOARD_X0 + RJ45_U[0], yf0, PCB_Z1 - 0.5, BOARD_X0 + RJ45_U[1], yf1, PCB_Z1 + RJ45_TOP, name="Ausschnitt RJ45", **cut)
bx(None, BOARD_X0 + TF_U[0], yf0, PCB_Z1 + TF_Z[0], BOARD_X0 + TF_U[1], yf1, PCB_Z1 + TF_Z[1], name="Ausschnitt TF", **cut)
c("solid.hole", position=[SMA_X, IN_Y + WALL, SMA_Z], diameter=SMA_D, depth=WALL + 1, targets=["Unterteil"], name="Loch SMA-Antenne")
for bus, x in OW_X.items():
    c("solid.hole", position=[x, OY0, OW_Z], diameter=OW_HOLE_D, depth=WALL + 1, targets=["Unterteil"], name=f"Loch 1-Wire {bus}")
c("solid.hole", position=[RS_X, IN_Y + WALL, RS_Z], diameter=RS_HOLE_D, depth=WALL + 1, targets=["Unterteil"], name="Loch RS485")

# Kabelbinder-Sockel: Block auf dem Boden, Tunnel unten am Boden entlang
for i, (x, y, d) in enumerate(TIE_MOUNTS):
    hx, hy = (TIE_L / 2, TIE_W / 2) if d == "x" else (TIE_W / 2, TIE_L / 2)
    bx(None, x - hx, y - hy, FLOOR, x + hx, y + hy, FLOOR + TIE_H, name=f"Kabelbinder-Sockel {i + 1}",
       operation="join", targets=["Unterteil"])
    sx, sy = (hx + 1, TIE_SLOT_W / 2) if d == "x" else (TIE_SLOT_W / 2, hy + 1)
    bx(None, x - sx, y - sy, FLOOR, x + sx, y + sy, FLOOR + TIE_SLOT_H, name=f"Kabelbinder-Tunnel {i + 1}", **cut)

# Beschriftung: Skizze auf der Aussenflaeche der Wand (Normale nach aussen), dann vertieft einpraegen
def labels(sketch, origin, x_dir, face_pt, texts):
    c("sketch.create", plane={"origin": origin, "x_dir": x_dir, "y_dir": [0, 0, 1]}, name=sketch)
    for (txt, u, v) in texts:
        c("sketch.text", text=txt, at=[u, v], height=LABEL_H, align="center", font=LABEL_FONT, bold=True)
    c("sketch.finish")
    c("solid.emboss", sketch=sketch, depth=LABEL_DEPTH, mode="deboss", targets=["Unterteil"], face=face_pt)

labels("Beschriftung vorn", [0, OY0, 0], [1, 0, 0], [95.0, OY0, 5.0], [
    ("BUS 1", OW_X["Bus 1"], OW_Z + 9.5),
    ("BUS 2", OW_X["Bus 2"], OW_Z + 9.5),
    ("PoE", BOARD_X0 + sum(RJ45_U) / 2, PCB_Z1 + RJ45_TOP + 2.5),
])
# Rueckwand von aussen gesehen: x laeuft nach links, daher x_dir = -X und u = -x
labels("Beschriftung hinten", [0, IN_Y + WALL, 0], [-1, 0, 0], [30.0, IN_Y + WALL, 5.0], [
    ("RS485", -(RS_X - 20.0), RS_Z - LABEL_H / 2),
])

# ---- Deckel ----
bx("Deckel", OX0, OY0, TOP, OX0 + OX, OY0 + OY, TOP + LID)
c("solid.fillet", edges=outer_edges(TOP + LID / 2), radius=R_OUT, body="Deckel")
g = LIP_GAP
bx(None, g, g, TOP - LIP_H, IN_X - g, IN_Y - g, TOP, name="Zentrierrand", operation="join", targets=["Deckel"])
bx(None, g + LIP_T, g + LIP_T, TOP - LIP_H - 1, IN_X - g - LIP_T, IN_Y - g - LIP_T, TOP,
   name="Zentrierrand innen", operation="cut", targets=["Deckel"])
for i, (x, y) in enumerate(posts):
    c("solid.cylinder", base=[x, y, TOP - LIP_H - 1], diameter=POST_D + 1.0, height=LIP_H + 1,
      operation="cut", targets=["Deckel"], name=f"Freischnitt Dom {i + 1}")
for (x, y) in posts:
    c("solid.hole", position=[x, y, TOP + LID], diameter=LID_HOLE, depth=LID + 0.5, targets=["Deckel"])
for x in VENT_X:
    bx(None, x - VENT_W / 2, VENT_Y[0], TOP - 1, x + VENT_W / 2, VENT_Y[1], TOP + LID + 1,
       operation="cut", targets=["Deckel"])

# ---- Hutschienen-Clip ----
if DIN:
    ye0 = RAIL_YC - RAIL_W / 2               # Schienenkante unten (Rastnase)
    ye1 = RAIL_YC + RAIL_W / 2               # Schienenkante oben (fester Haken)
    zr = -CLIP_T                             # Auflage auf der Schiene
    zl0 = zr - RAIL_T - HOOK_GAP             # Oberkante der Lippen
    zl1 = zl0 - HOOK_LIP_T                   # Unterkante der Haken
    wi0 = ye0 - HOOK_GAP                     # Innenflaeche Rastnase
    wi1 = ye1 + HOOK_GAP                     # Innenflaeche fester Haken
    sb0 = wi0 - (HOOK_T - 0.4) - SPRING_T    # Aussenkante Federbalken
    sb1 = sb0 + SPRING_T
    J = dict(operation="join", targets=["Hutschienenclip"])
    K = dict(operation="cut", targets=["Hutschienenclip"])
    bx("Hutschienenclip", CLIP_X[0], sb0, zr, CLIP_X[1], wi1 + HOOK_T, 0)
    # Schlitz hinter dem Federbalken: Balken haengt nur an den Enden
    bx(None, SPRING_X[0], sb1, zr - 1, SPRING_X[1], wi0 + SPRING_GAP, 1, name="Schlitz Federbalken", **K)
    # Rastnase am Federbalken, mit Untergriff und Einfuehrschraege
    bx(None, LATCH_X[0], sb0, zl1, LATCH_X[1], wi0, 0, name="Rastnase", **J)
    bx(None, LATCH_X[0], wi0 - 0.01, zl1, LATCH_X[1], wi0 + HOOK_GRIP, zl0, name="Rastnase Lippe", **J)
    c("solid.chamfer", edges=[[sum(LATCH_X) / 2, wi0 + HOOK_GRIP, zl1]], distance=1.7, body="Hutschienenclip", tangent_chain=False)
    # Zuglasche zur Unterkante, zum Loesen mit dem Schraubendreher nach unten ziehen
    bx(None, LATCH_X[0], TAB_Y0, -TAB_T, LATCH_X[1], sb0 + 0.01, 0, name="Zuglasche", **J)
    bx(None, LATCH_X[0] + 2, TAB_Y0 + 2, -TAB_T - 1, LATCH_X[1] - 2, TAB_Y0 + 6, 1, name="Schlitz Schraubendreher", **K)
    # fester Haken oben
    bx(None, CLIP_X[0], wi1, zl1, CLIP_X[1], wi1 + HOOK_T, zr + 0.01, name="Haken", **J)
    bx(None, CLIP_X[0], wi1 - HOOK_GRIP, zl1, CLIP_X[1], wi1 + 0.01, zl0, name="Haken Lippe", **J)
    c("solid.chamfer", edges=[[50.0, wi1 - HOOK_GRIP, zl1]], distance=0.8, body="Hutschienenclip", tangent_chain=False)
    # Schraubloecher: Kernloch im Clip, Durchgang im Boden
    for (x, y) in CLIP_SCREWS:
        c("solid.hole", position=[x, y, zr], diameter=CLIP_PILOT, depth=CLIP_T + 0.5, targets=["Hutschienenclip"])
        c("solid.hole", position=[x, y, FLOOR], diameter=FLOOR_HOLE, depth=FLOOR + 0.5, targets=["Unterteil"])
    # Schiene als Platzhalter (TS35 x 7,5)
    bx("Hutschiene Platzhalter", -20, ye0, zr - RAIL_T, IN_X + 20, ye0 + RAIL_LIP_W, zr)
    bx("HS2", -20, ye1 - RAIL_LIP_W, zr - RAIL_T, IN_X + 20, ye1, zr)
    bx("HS3", -20, ye0 + RAIL_LIP_W - 1, zr - 7.5, IN_X + 20, ye0 + RAIL_LIP_W, zr - RAIL_T)
    bx("HS4", -20, ye1 - RAIL_LIP_W, zr - 7.5, IN_X + 20, ye1 - RAIL_LIP_W + 1, zr - RAIL_T)
    bx("HS5", -20, ye0 + RAIL_LIP_W - 1, zr - 7.5, IN_X + 20, ye1 - RAIL_LIP_W + 1, zr - 6.5)
    c("solid.combine", target="Hutschiene Platzhalter", tools=["HS2", "HS3", "HS4", "HS5"], operation="join", touching="one_body")

# ---- Platine als Platzhalter (nur zur Kontrolle, Masse der Bauteile geschaetzt) ----
X = lambda u: BOARD_X0 + u
Y = lambda v: BOARD_Y0 + v
bx("ESP32-P4 Platzhalter", X(0), Y(0), PCB_Z0, X(PCB_X), Y(PCB_Y), PCB_Z1)
for nm, (u0, v0, u1, v1, h) in {
    "RJ45": (21.6, -1.4, 38.2, 19.9, 13.5),
    "USB": (5.0, -1.4, 18.9, 16.4, 14.0),
    "TF": (38.5, 1.2, 52.1, 12.8, 1.8),
    "Stiftleiste": (3.0, 63.5, 52.0, 66.5, 8.5),
}.items():
    bx(f"P {nm}", X(u0), Y(v0), PCB_Z1, X(u1), Y(v1), PCB_Z1 + h)
c("solid.combine", target="ESP32-P4 Platzhalter", tools=["P RJ45", "P USB", "P TF", "P Stiftleiste"], operation="join")
for (u, v) in HOLES_UV:
    c("solid.cylinder", base=[X(u), Y(v), PCB_Z0 - 0.5], diameter=2.7, height=PCB_T + 1,
      operation="cut", targets=["ESP32-P4 Platzhalter"])

# ---- Rundbuchsen als Platzhalter (nur zur Kontrolle, Masse geschaetzt) ----
def socket(nm, x, z, segs):
    """segs: (y0, y1, Durchmesser) entlang y; der erste Abschnitt legt den Koerper an."""
    for i, (y0, y1, d) in enumerate(segs):
        kw = dict(body_name=nm) if i == 0 else dict(operation="join", targets=[nm])
        c("solid.cylinder", base=[x, y0, z], axis="Y", diameter=d, height=y1 - y0, **kw)

for bus, x in OW_X.items():   # GX12: Flansch/Mutter 16 mm, Koerper 11 mm
    socket(f"GX12 {bus} Platzhalter", x, OW_Z, [
        (OY0 - 10.0, OY0 - 2.0, 12.0),   # Steckgewinde aussen
        (OY0 - 2.0, OY0, 16.0),          # Flansch aussen
        (OY0, 0.0, 12.0),                # Gewinde in der Wand
        (0.0, 2.5, 16.0),                # Mutter innen
        (2.5, 16.0, 11.0)])              # Koerper mit Loetkelchen
socket("GX16 RS485 Platzhalter", RS_X, RS_Z, [   # GX16: Flansch/Mutter 20 mm, Koerper 14 mm
    (IN_Y - 14.0, IN_Y - 3.0, 14.0),             # Koerper mit Loetkelchen
    (IN_Y - 3.0, IN_Y, 20.0),                    # Mutter innen
    (IN_Y, IN_Y + WALL, 16.0),                   # Gewinde in der Wand
    (IN_Y + WALL, IN_Y + WALL + 2.0, 20.0),      # Flansch aussen
    (IN_Y + WALL + 2.0, IN_Y + WALL + 10.0, 16.0)])  # Steckgewinde aussen

# ---- Traegerplatine mit RS485-Wandler als Platzhalter ----
cz = FLOOR + CARRIER_H
bx("Traegerplatine Platzhalter", CARRIER_X[0], CARRIER_Y[0], cz, CARRIER_X[1], CARRIER_Y[1], cz + PCB_T)
ml, mw, mh = RS485_MOD
mx1, my0 = CARRIER_X[1] - 0.5, (CARRIER_Y[0] + CARRIER_Y[1] - ml) / 2
bx(None, mx1 - mw, my0, cz + PCB_T, mx1, my0 + ml, cz + PCB_T + mh, name="RS485-Wandler",
   operation="join", targets=["Traegerplatine Platzhalter"])

json.dump({"commands": cmds}, open("esp32p4_box.json", "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print(f"{len(cmds)} Befehle -> esp32p4_box.json  (aussen {OX} x {OY} x {TOP + LID} mm)")

# ---- Export-Skript: STEP (zusammengebaut) + STLs in Druckorientierung ----
import os
here = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
parts = ["Unterteil", "Deckel"] + (["Hutschienenclip"] if DIN else [])
ex = [{"command": "file.export", "params": {"path": f"{here}/ESP32-P4-Box.step", "bodies": parts}},
      {"command": "file.export", "params": {"path": f"{here}/Unterteil.stl", "bodies": ["Unterteil"], "quality": "fine"}}]
# Deckel und Clip umdrehen, damit die flache Seite auf dem Druckbett liegt
flip = ["Deckel"] + (["Hutschienenclip"] if DIN else [])
ex.append({"command": "solid.move", "params": {"bodies": flip, "axis": [1, 0, 0], "angle": 180}})
ex.append({"command": "solid.move", "params": {"bodies": ["Deckel"], "translate": [0, IN_Y, TOP + LID]}})
for p in flip:
    ex.append({"command": "file.export", "params": {"path": f"{here}/{p}.stl", "bodies": [p], "quality": "fine"}})
json.dump({"commands": ex}, open("export_druck.json", "w", encoding="utf-8"), indent=1, ensure_ascii=False)

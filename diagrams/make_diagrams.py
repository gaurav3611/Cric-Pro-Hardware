#!/usr/bin/env python3
"""
Draws every diagram of this kit as an SVG file, next to this script.

    python3 diagrams/make_diagrams.py

Change a pin, a part or a position here and run it again. The drawings are plain SVG, so
they open in any browser and can also be edited by hand in Inkscape.

Before drawing, the script checks the zero PCB layout against the list of connections
(NETS below): every connection must be made by the layout, and nothing else may be joined.
It stops with a message if the two disagree, so a layout drawn here cannot contain a short.
"""
import os
import sys

OUT = os.path.dirname(os.path.abspath(__file__))

# The pins this kit uses on the ESP32-C3. tools/check.mjs compares this line with the sketches.
PINS = {"SDA": 3, "SCL": 4, "LED": 8, "BUTTON": 9, "BATTERY": 0}

# ---------------------------------------------------------------------------------------------
# The circuit, as a list of nets. A net is a set of points that are joined together.
# ---------------------------------------------------------------------------------------------
NETS = {
    "BAT+":  ["BAT.+", "TP.B+"],
    "BAT-":  ["BAT.-", "TP.B-"],
    "VOUT":  ["TP.OUT+", "SW.a"],
    "VSW":   ["SW.common", "D1.anode", "R1.1"],
    "5V":    ["D1.cathode", "ESP.5V"],
    "GND":   ["TP.OUT-", "ESP.GND", "IMU.GND", "IMU.SAO", "R2.2", "C1.2"],
    "3V3":   ["ESP.3V3", "IMU.VIN"],
    "SCL":   [f"ESP.GPIO{PINS['SCL']}", "IMU.SCL"],
    "SDA":   [f"ESP.GPIO{PINS['SDA']}", "IMU.SDA"],
    "SENSE": ["R1.2", "R2.1", "C1.1", f"ESP.GPIO{PINS['BATTERY']}"],
}

# ---------------------------------------------------------------------------------------------
# Colours and small drawing helpers
# ---------------------------------------------------------------------------------------------
INK, MUTED, PAPER, PANEL, RULE = "#17221c", "#58675e", "#ffffff", "#f3f5f1", "#dde4dc"
RED, BLACK, ORANGE, BLUE, GREEN, GREY = "#c62828", "#212121", "#e68a00", "#1565c0", "#2e7d32", "#8a959b"
ESP_BOARD, IMU_BOARD, TP_BOARD, GOLD, GOLD_EDGE = "#1f2a44", "#2f5d50", "#3b4a5a", "#d4af37", "#8d6e00"
WARN = "#b0271f"
FONT = 'font-family="Helvetica, Arial, sans-serif"'


class Drawing:
    def __init__(self, width, height, title, subtitle=""):
        self.w, self.h = width, height
        self.parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">',
                      f'<rect width="{width}" height="{height}" fill="{PAPER}"/>']
        self.text(30, 38, title, 20, weight="bold")
        if subtitle:
            self.text(30, 60, subtitle, 13, fill=MUTED)

    def add(self, markup):
        self.parts.append(markup)

    def text(self, x, y, s, size=13, anchor="start", weight="normal", fill=INK, mono=False):
        family = 'font-family="Menlo, Consolas, monospace"' if mono else FONT
        s = str(s).replace("&", "&amp;").replace("<", "&lt;")
        self.add(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}" {family}>{s}</text>')

    def rect(self, x, y, w, h, fill, stroke="none", rx=8, width=1, opacity=1):
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{width}" fill-opacity="{opacity}"/>')

    def circle(self, x, y, r, fill, stroke="none", width=1):
        self.add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>')

    def wire(self, points, colour, width=4, dash=None):
        d = "M" + " L".join(f"{x},{y}" for x, y in points)
        extra = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"{extra}/>')

    def dot(self, x, y, colour):
        """A joint: wires that meet here are connected."""
        self.circle(x, y, 6, colour)

    def hop(self, x, y, colour, vertical=True):
        """Marks a place where the wire of this colour passes over another without touching it."""
        self.rect(x - 7, y - 7, 14, 14, PAPER, rx=7)
        if vertical:
            self.add(f'<path d="M{x},{y-9} A9,9 0 0 1 {x},{y+9}" fill="none" stroke="{colour}" stroke-width="4" stroke-linecap="round"/>')
        else:
            self.add(f'<path d="M{x-9},{y} A9,9 0 0 1 {x+9},{y}" fill="none" stroke="{colour}" stroke-width="4" stroke-linecap="round"/>')

    def pin(self, x, y, label, side, used=True, light="#ffffff", dim="#90a4ae", size=12):
        self.circle(x, y, 7, GOLD, GOLD_EDGE)
        if side == "right":      # the label sits to the right of the pin
            self.text(x + 14, y + 4, label, size, weight="bold" if used else "normal", fill=light if used else dim)
        else:
            self.text(x - 14, y + 4, label, size, "end", "bold" if used else "normal", light if used else dim)

    def note(self, x, y, lines, size=12, fill=MUTED, gap=17, anchor="start", weight="normal"):
        for i, line in enumerate(lines):
            self.text(x, y + i * gap, line, size, anchor, weight, fill)

    def panel(self, x, y, w, h):
        self.rect(x, y, w, h, PANEL, RULE, 8)

    def legend(self, x, y, colour, label, sub="", dash=None):
        self.wire([(x, y), (x + 34, y)], colour, 5 if not dash else 3, dash)
        self.text(x + 44, y + 5, label, 13, weight="bold")
        if sub:
            self.text(x + 44, y + 23, sub, 12, fill=MUTED)

    # ---- symbols of single parts, all drawn along a horizontal line through (x, y) ----
    def resistor(self, x, y, label, value, length=60, vertical=False):
        if vertical:
            self.rect(x - 9, y, 18, length, PAPER, INK, 2, 2)
            self.text(x + 18, y + length / 2 - 2, label, 12, weight="bold")
            self.text(x + 18, y + length / 2 + 14, value, 11, fill=MUTED)
        else:
            self.rect(x, y - 9, length, 18, PAPER, INK, 2, 2)
            self.text(x + length / 2, y - 16, label, 12, "middle", "bold")
            self.text(x + length / 2, y + 27, value, 11, "middle", fill=MUTED)

    def capacitor(self, x, y, label, value):
        """Vertical, top lead at (x, y), 22 tall."""
        self.wire([(x - 13, y), (x + 13, y)], INK, 3)
        self.wire([(x - 13, y + 8), (x + 13, y + 8)], INK, 3)
        self.text(x + 20, y + 2, label, 12, weight="bold")
        self.text(x + 20, y + 17, value, 11, fill=MUTED)

    def ground(self, x, y):
        """The ground sign, hanging from (x, y)."""
        self.wire([(x, y), (x, y + 10)], BLACK, 4)
        for i, half in enumerate((13, 8, 3)):
            self.wire([(x - half, y + 10 + i * 6), (x + half, y + 10 + i * 6)], BLACK, 3)

    def diode(self, x, y, label="D1"):
        """Points to the right: current flows from x to x + 44. The bar is the striped end."""
        self.add(f'<path d="M{x},{y-13} L{x},{y+13} L{x+26},{y} Z" fill="{INK}"/>')
        self.wire([(x + 28, y - 14), (x + 28, y + 14)], INK, 4)
        self.text(x + 14, y - 22, label, 12, "middle", "bold")

    def switch(self, x, y, label="SW1"):
        """From (x, y) to (x + 60, y), drawn closed-ish so the path is easy to follow."""
        self.circle(x + 8, y, 5, PAPER, INK, 2)
        self.circle(x + 52, y, 5, PAPER, INK, 2)
        self.wire([(x + 12, y - 3), (x + 50, y - 16)], INK, 3)
        self.text(x + 30, y - 24, label, 12, "middle", "bold")

    def save(self, name):
        self.add("</svg>")
        with open(os.path.join(OUT, name), "w") as f:
            f.write("\n".join(self.parts) + "\n")
        return name


# ---------------------------------------------------------------------------------------------
# The three boards
# ---------------------------------------------------------------------------------------------
ESP_RIGHT = ["5V", "GND", "3V3", "GPIO4", "GPIO3", "GPIO2", "GPIO1", "GPIO0"]       # the power side, top to bottom, USB at the top
ESP_LEFT = ["GPIO5", "GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10", "GPIO20", "GPIO21"]
IMU_PINS = ["VIN", "3V3", "GND", "SCL", "SDA", "CS", "SAO"]


def esp32(d, x, y, used, notes=None, w=230, h=360):
    """Draws the Super Mini seen from the top, USB up. Returns {pin name: (x, y)}."""
    notes = notes or {}
    d.rect(x, y, w, h, ESP_BOARD, rx=14)
    d.rect(x + w / 2 - 38, y - 16, 76, 34, "#b0b7bd", "#6b7479", 6)
    d.text(x + w / 2, y + 6, "USB-C", 11, "middle", fill="#263238")
    d.text(x + w / 2, y + h / 2 - 8, "ESP32-C3", 17, "middle", "bold", "#ffffff")
    d.text(x + w / 2, y + h / 2 + 14, "Super Mini", 14, "middle", fill="#cfd8dc")
    d.rect(x + w / 2 - 34, y + h - 52, 68, 40, "#37474f", rx=4)
    d.text(x + w / 2, y + h - 27, "antenna", 11, "middle", fill="#cfd8dc")
    at = {}
    for i, name in enumerate(ESP_LEFT):
        py = y + 52 + i * 36
        at[name] = (x + 16, py)
        d.pin(x + 16, py, name, "right", name in used)
    for i, name in enumerate(ESP_RIGHT):
        py = y + 52 + i * 36
        at[name] = (x + w - 16, py)
        d.pin(x + w - 16, py, name, "left", name in used)
    return at


def imu(d, x, y, used, w=200, h=300, first=52):
    """Draws the GY-BMI160 with its seven-pin row on the left. Returns {pin name: (x, y)}."""
    d.rect(x, y, w, h, IMU_BOARD, rx=12)
    d.text(x + w / 2 + 22, y + h / 2 - 20, "GY-BMI160", 16, "middle", "bold", "#ffffff")
    d.rect(x + w / 2 + 4, y + h / 2, 36, 36, "#1c2b26", rx=2)
    d.text(x + w / 2 + 22, y + h / 2 + 54, "the sensor chip", 10, "middle", fill="#b7d0c6")
    at = {}
    for i, name in enumerate(IMU_PINS):
        py = y + first + i * 36
        at[name] = (x + 16, py)
        d.pin(x + 16, py, name, "right", name in used, dim="#8fb3a6")
    return at


# ---------------------------------------------------------------------------------------------
# 1. Pin map
# ---------------------------------------------------------------------------------------------
def pin_map():
    d = Drawing(1000, 640, "The ESP32-C3 Super Mini and what this kit uses each pin for",
                "Seen from the top, USB socket up. The labels are printed on the underside of the real board.")
    at = esp32(d, 385, 120, ["5V", "GND", "3V3", "GPIO4", "GPIO3", "GPIO0", "GPIO8", "GPIO9"])
    right = {
        "5V": ("power in", "from the battery, through the switch and the diode", RED),
        "GND": ("ground", "the common return of every part", BLACK),
        "3V3": ("power out, 3.3 volts", "made by the board's regulator. Feeds the sensor.", RED),
        "GPIO4": ("I2C clock, SCL", "to the sensor", ORANGE),
        "GPIO3": ("I2C data, SDA", "to the sensor", BLUE),
        "GPIO2": ("leave free", "the chip reads it at power-on to decide how to start", GREY),
        "GPIO1": ("free", "can measure a voltage, like GPIO0", GREY),
        "GPIO0": ("battery voltage, stage 7", "optional. Through two resistors, never directly.", GREEN),
    }
    for name, (head, sub, colour) in right.items():
        x, y = at[name]
        d.wire([(x + 12, y), (x + 60, y)], colour, 4)
        d.text(x + 72, y - 1, head, 13, weight="bold")
        d.text(x + 72, y + 14, sub, 11, fill=MUTED)
    left = {
        "GPIO5": ("free", GREY), "GPIO6": ("free", GREY), "GPIO7": ("free", GREY),
        "GPIO8": ("the blue light on the board. LOW = on", BLUE),
        "GPIO9": ("the BOOT button. Pressed = LOW", ORANGE),
        "GPIO10": ("free", GREY), "GPIO20": ("free (serial RX)", GREY), "GPIO21": ("free (serial TX)", GREY),
    }
    for name, (head, colour) in left.items():
        x, y = at[name]
        d.wire([(x - 12, y), (x - 60, y)], colour, 4)
        d.text(x - 72, y + 4, head, 13, "end", "bold" if colour != GREY else "normal", INK if colour != GREY else MUTED)
    d.panel(30, 520, 940, 96)
    d.note(48, 546, [
        "GPIO8 and GPIO9 are already used on the board itself, by the light and the button. Do not wire anything else to them.",
        "GPIO2, GPIO8 and GPIO9 are read by the chip at the moment it gets power. A part that pulls one of them the wrong way stops it from starting.",
        "Every pin works at 3.3 volts. Never connect 5 volts or the battery directly to a GPIO pin.",
    ], 12, INK, 22)
    return d.save("pin-map.svg")


# ---------------------------------------------------------------------------------------------
# 2. Stage 2: the sensor on five wires
# ---------------------------------------------------------------------------------------------
def sensor_wiring():
    d = Drawing(1000, 640, "Stages 2 to 5: the sensor on five wires",
                "Powered from the USB cable. Wire by the labels printed on your boards, not by their position in this drawing.")
    e = esp32(d, 150, 110, ["GND", "3V3", "GPIO4", "GPIO3"])
    m = imu(d, 680, 110, ["VIN", "GND", "SCL", "SDA", "SAO"])
    d.wire([e["GND"], (560, e["GND"][1]), (560, m["GND"][1]), m["GND"]], BLACK)
    d.wire([e["3V3"], (500, e["3V3"][1]), (500, m["VIN"][1]), m["VIN"]], RED)
    d.hop(500, e["GND"][1], RED)
    d.wire([e["GPIO4"], m["SCL"]], ORANGE)
    d.wire([e["GPIO3"], m["SDA"]], BLUE)
    sx, sy = m["SAO"]
    gx, gy = m["GND"]
    d.wire([(sx, sy), (620, sy), (620, gy)], BLACK, 3, "7 5")
    d.hop(620, m["SCL"][1], BLACK)
    d.hop(620, m["SDA"][1], BLACK)
    d.dot(620, gy, BLACK)
    d.text(265, 494, "The light is on GPIO8 and the BOOT button on GPIO9.", 11, "middle", fill=MUTED)
    d.text(780, 434, "Leave 3V3, CS, INT1, INT2 and OCS unconnected.", 11, "middle", fill=MUTED)
    d.panel(30, 510, 940, 106)
    d.legend(50, 536, RED, "3V3  to  VIN", "power, 3.3 volts")
    d.legend(280, 536, BLACK, "GND  to  GND", "ground")
    d.legend(510, 536, ORANGE, f"GPIO{PINS['SCL']}  to  SCL", "I2C clock")
    d.legend(740, 536, BLUE, f"GPIO{PINS['SDA']}  to  SDA", "I2C data")
    d.wire([(50, 590), (84, 590)], BLACK, 3, "7 5")
    d.text(94, 595, "SAO  to  GND", 13, weight="bold")
    d.text(196, 595, "fixes the sensor's address at 0x68. Left open it is usually 0x69, which also works.", 12, fill=MUTED)
    d.text(950, 595, "Power the sensor from 3V3, never from 5V.", 12, "end", fill=WARN)
    return d.save("stage-2-sensor-wiring.svg")


# ---------------------------------------------------------------------------------------------
# Pieces shared by the power drawings
# ---------------------------------------------------------------------------------------------
def battery_box(d, x, y, w, h, plus_y, minus_y):
    d.rect(x, y, w, h, "#eceff1", "#90a4ae", 10, 2)
    d.text(x + w / 2 - 6, y + h / 2 - 8, "LiPo cell", 14, "middle", "bold")
    d.text(x + w / 2 - 6, y + h / 2 + 10, "3.7 V", 12, "middle", fill=MUTED)
    d.text(x + w / 2 - 6, y + h / 2 + 26, "1500 mAh", 12, "middle", fill=MUTED)
    for py, sign, colour in ((plus_y, "+", RED), (minus_y, "-", BLACK)):
        d.circle(x + w, py, 8, colour)
        d.text(x + w - 16, py + 6, sign, 18, "end", "bold", colour)
    return {"+": (x + w, plus_y), "-": (x + w, minus_y)}


def charger_box(d, x, y, w, h, rows, usb_note="Charge here"):
    """rows: {pad name: y}. B pads sit on the left edge, OUT pads on the right edge."""
    d.rect(x, y, w, h, TP_BOARD, rx=10)
    d.rect(x + w / 2 - 38, y - 16, 76, 34, "#b0b7bd", "#6b7479", 6)
    d.text(x + w / 2, y + 6, "USB", 11, "middle", fill="#263238")
    d.text(x + w / 2, y - 24, usb_note, 12, "middle", "bold", GREEN)
    d.text(x + w / 2, y + h / 2 - 4, "TP4056", 16, "middle", "bold", "#ffffff")
    d.text(x + w / 2, y + h / 2 + 14, "charger with", 11, "middle", fill="#cfd8dc")
    d.text(x + w / 2, y + h / 2 + 28, "protection", 11, "middle", fill="#cfd8dc")
    at = {}
    for name, py in rows.items():
        left = name.startswith("B")
        px = x + 16 if left else x + w - 16
        at[name] = (px, py)
        d.pin(px, py, name, "right" if left else "left")
    return at


# ---------------------------------------------------------------------------------------------
# 3. Stage 6: where the power flows
# ---------------------------------------------------------------------------------------------
def power_path():
    d = Drawing(1120, 760, "Stage 6: where the power flows",
                "The cell is charged by the TP4056 and feeds the ESP32 through the switch and the diode.")
    top, bottom = 280, 410
    b = battery_box(d, 40, 240, 140, 210, top, bottom)
    t = charger_box(d, 250, 230, 220, 230, {"B+": top, "B-": bottom, "OUT+": top, "OUT-": bottom})
    d.wire([b["+"], t["B+"]], RED)
    d.wire([b["-"], t["B-"]], BLACK)

    # The ESP32, shown only as far as power is concerned.
    ex, ey, ew, eh = 800, 150, 290, 310
    d.rect(ex, ey, ew, eh, ESP_BOARD, rx=12)
    ux = ex + 90                                   # the USB socket sits left of centre, above the 5V line
    d.rect(ux - 38, ey - 16, 76, 34, "#b0b7bd", "#6b7479", 6)
    d.text(ux, ey + 6, "USB-C", 11, "middle", fill="#263238")
    d.text(ux, ey - 24, "Program here", 12, "middle", "bold", BLUE)
    d.pin(ex + 16, top, "5V", "right")
    d.pin(ex + 16, bottom, "GND", "right")
    d.text(ex + ew - 16, ey + eh - 16, "ESP32-C3 Super Mini", 12, "end", "bold", "#ffffff")
    # inside the board: USB socket -> the board's own diode -> the 5V line -> regulator -> 3.3 V
    soft = "#ef9a9a"
    d.wire([(ux, ey + 18), (ux, ey + 56)], soft, 3)
    d.add(f'<path d="M{ux - 10},{ey + 56} L{ux + 10},{ey + 56} L{ux},{ey + 74} Z" fill="{soft}"/>')
    d.wire([(ux - 11, ey + 76), (ux + 11, ey + 76)], soft, 3)
    d.wire([(ux, ey + 76), (ux, top)], soft, 3)
    d.text(ux + 20, ey + 62, "the board's own diode:", 11, fill="#cfd8dc")
    d.text(ux + 20, ey + 77, "USB power can flow in,", 11, fill="#cfd8dc")
    d.text(ux + 20, ey + 92, "nothing flows back out", 11, fill="#cfd8dc")
    d.wire([(ex + 56, top), (ex + 170, top)], soft, 3)
    d.dot(ux, top, soft)
    d.rect(ex + 170, top - 19, 100, 38, "#37474f", "#90a4ae", 4)
    d.text(ex + 220, top - 3, "regulator", 12, "middle", fill="#ffffff")
    d.text(ex + 220, top + 12, "to 3.3 V", 11, "middle", fill="#cfd8dc")
    d.wire([(ex + 220, top + 19), (ex + 220, top + 62)], "#ffcc80", 3)
    d.text(ex + 220, top + 80, "3.3 V to the chip", 11, "middle", fill="#ffcc80")
    d.text(ex + 220, top + 95, "and to the sensor", 11, "middle", fill="#ffcc80")

    # switch, the diode
    d.wire([t["OUT+"], (540, top)], RED)
    d.switch(540, top)
    d.wire([(600, top), (650, top)], RED)
    d.diode(650, top)
    d.wire([(680, top), (ex + 16, top)], RED)
    d.wire([t["OUT-"], (ex + 16, bottom)], BLACK)

    d.note(110, 478, ["3.3 to 4.2 V", "Red lead is +"], 12, MUTED, 16, "middle")
    d.note(360, 488, ["Charges the cell to 4.2 V.", "Cuts off below 2.4 V", "and above 3 A."], 12, MUTED, 16, "middle")
    d.note(570, top + 30, ["SW1 slide switch", "off: nothing flows"], 12, MUTED, 16, "middle")
    d.note(700, top + 30, ["D1 diode 1N5817", "striped end", "towards the ESP32"], 12, MUTED, 16, "middle")
    d.note(740, top - 14, ["3.0 to 3.9 V"], 12, RED, 16, "middle", "bold")
    d.note(640, bottom + 22, ["ground: from OUT-, never from B-"], 12, MUTED, 16, "middle")

    d.panel(30, 550, 1060, 186)
    d.note(50, 578, [
        "Why the diode. The ESP32 board joins its 5V pin to its USB socket through a diode of its own. Without D1, plugging in that USB cable",
        "would push 5 volts straight into the cell, with nothing to limit the current. D1 lets current flow out of the battery only.",
        "With D1 fitted, the battery and the ESP32's USB cable can be connected at the same time. Whichever gives the higher voltage feeds the board.",
    ], 12, INK, 19)
    d.note(50, 650, [
        "Charging. Plug a phone charger into the TP4056's own socket, with the switch off. One light shows while it charges, the other when it is full.",
        "Ground. B- is the cell's own minus. OUT- is the same point after the protection switch. Everything else connects to OUT-, so the protection can act.",
        "Voltage. The diode costs about 0.3 V and the regulator needs about 0.1 V more than it gives. The board runs properly down to a cell voltage of about 3.5 V.",
    ], 12, INK, 19)
    d.text(50, 722, "Without D1: never have the battery switched on and the ESP32's USB cable plugged in at the same time.", 12, weight="bold", fill=WARN)
    return d.save("stage-6-power.svg")


# ---------------------------------------------------------------------------------------------
# 4. Stage 7: measuring the battery
# ---------------------------------------------------------------------------------------------
def battery_sense():
    d = Drawing(1000, 560, "Stage 7 (optional): measuring the battery",
                "Two equal resistors halve the battery voltage, so the pin never sees more than it can take.")
    y = 220
    d.circle(110, y, 7, RED)
    d.note(110, y - 44, ["switch output", "battery +"], 12, INK, 16, "middle", "bold")
    d.text(110, y + 30, "3.3 to 4.2 V", 12, "middle", fill=RED)
    d.wire([(110, y), (210, y)], RED)
    d.resistor(210, y, "R1", "100 k")
    d.wire([(270, y), (380, y)], GREEN)
    d.dot(380, y, GREEN)
    d.wire([(380, y), (470, y)], GREEN)
    d.resistor(470, y, "R2", "100 k")
    d.wire([(530, y), (610, y)], BLACK)
    d.ground(610, y)
    d.text(610, y + 52, "GND", 12, "middle", weight="bold")
    # up to the pin
    d.wire([(380, y), (380, 130)], GREEN)
    d.rect(318, 96, 124, 34, ESP_BOARD, rx=6)
    d.text(380, 118, f"ESP32 GPIO{PINS['BATTERY']}", 13, "middle", "bold", "#ffffff")
    d.text(452, 118, "1.65 to 2.1 V", 12, fill=GREEN)
    # down to the capacitor
    d.wire([(380, y), (380, 300)], GREEN)
    d.capacitor(380, 300, "C1", "100 nF")
    d.wire([(380, 308), (380, 340)], BLACK)
    d.ground(380, 340)

    d.panel(660, 96, 310, 290)
    d.note(678, 124, ["How it works"], 14, INK, 20, weight="bold")
    d.note(678, 150, [
        "The same small current flows through",
        "R1 and then R2. Because they are equal,",
        "each drops half of the battery voltage.",
        "The point between them is at half.",
        "",
        "pin = battery x R2 / (R1 + R2)",
        "",
        "4.2 V at the battery is 2.1 V at the pin.",
        "The pin measures up to about 2.5 V.",
        "",
        "The firmware multiplies by 2 again.",
        "C1 holds the voltage steady for the",
        "moment the chip takes its measurement.",
    ], 12, INK, 17)
    d.panel(30, 420, 940, 116)
    d.note(48, 446, [
        "Take the battery voltage after the switch and before the diode. Then the resistors draw nothing while the bat is switched off.",
        "With 200 kilohm in total they draw 21 millionths of an amp while it is on, which the cell does not notice.",
        "Resistors are never exactly equal. Stage 7 shows how to compare with a multimeter once and correct the difference.",
        "Change the resistors and the firmware has to know: BATTERY_DIVIDER_RATIO in config.h is (R1 + R2) / R2.",
    ], 12, INK, 22)
    return d.save("stage-7-battery-sense.svg")


# ---------------------------------------------------------------------------------------------
# 5. The whole circuit
# ---------------------------------------------------------------------------------------------
def full_circuit():
    d = Drawing(1280, 860, "The whole circuit",
                "Everything stage 8 needs. The part inside the dashed frame is optional and belongs to stage 7.")
    gnd_y, plus_y = 150, 210
    b = battery_box(d, 30, 124, 120, 112, plus_y, gnd_y)
    t = charger_box(d, 210, 110, 190, 140, {"B-": gnd_y, "B+": plus_y, "OUT-": gnd_y, "OUT+": plus_y})
    e = esp32(d, 560, 230, ["5V", "GND", "3V3", "GPIO4", "GPIO3", "GPIO0"])
    d.text(675, 222 - 24, "Program here", 12, "middle", "bold", BLUE)
    m = imu(d, 1040, 270, ["VIN", "GND", "SCL", "SDA", "SAO"], h=310, first=66)

    d.wire([b["-"], t["B-"]], BLACK)
    d.wire([b["+"], t["B+"]], RED)

    # ground: charger OUT- -> over the top of the ESP32 -> its GND pin -> on to the sensor
    gx, gy = e["GND"]
    d.wire([t["OUT-"], (865, gnd_y), (865, gy), (gx, gy)], BLACK)
    d.wire([(865, gy), (950, gy), (950, m["GND"][1]), m["GND"]], BLACK)
    d.dot(865, gy, BLACK)

    # plus: charger OUT+ -> switch -> diode -> over the top -> 5V pin
    d.wire([t["OUT+"], (425, plus_y)], RED)
    d.switch(425, plus_y)
    d.wire([(485, plus_y), (525, plus_y)], RED)
    d.diode(525, plus_y)
    vx, vy = e["5V"]
    d.wire([(555, plus_y), (590, plus_y), (590, 178), (840, 178), (840, vy), (vx, vy)], RED)

    # the sensor
    x3, y3 = e["3V3"]
    d.wire([(x3, y3), (900, y3), (900, m["VIN"][1]), m["VIN"]], RED)
    d.hop(950, m["VIN"][1], RED, vertical=False)
    c = e[f"GPIO{PINS['SCL']}"]
    d.wire([c, (925, c[1]), (925, m["SCL"][1]), m["SCL"]], ORANGE)
    a = e[f"GPIO{PINS['SDA']}"]
    d.wire([a, (905, a[1]), (905, m["SDA"][1]), m["SDA"]], BLUE)
    sx, sy = m["SAO"]
    d.wire([(sx, sy), (1000, sy), (1000, m["GND"][1])], BLACK, 3, "7 5")
    d.hop(1000, m["SDA"][1], BLACK)
    d.hop(1000, m["SCL"][1], BLACK)
    d.dot(1000, m["GND"][1], BLACK)

    # optional: the battery measurement
    d.add(f'<rect x="470" y="640" width="360" height="150" rx="10" fill="none" stroke="{GREEN}" stroke-width="2" stroke-dasharray="8 6"/>')
    d.text(520, 660, "Optional, stage 7", 12, weight="bold", fill=GREEN)
    d.dot(505, plus_y, RED)
    d.wire([(505, plus_y), (505, 700), (540, 700)], RED, 3)
    d.resistor(540, 700, "R1", "100 k")
    d.wire([(600, 700), (690, 700)], GREEN)
    d.dot(650, 700, GREEN)
    d.resistor(690, 700, "R2", "100 k")
    d.wire([(750, 700), (800, 700)], BLACK)
    d.ground(800, 700)
    d.wire([(650, 700), (650, 740)], GREEN)
    d.capacitor(650, 740, "C1", "100 nF")
    d.wire([(650, 748), (650, 760)], BLACK)
    d.ground(650, 760)
    px, py = e[f"GPIO{PINS['BATTERY']}"]
    d.wire([(px, py), (840, py), (840, 620), (650, 620), (650, 700)], GREEN)

    d.note(90, 262, ["red lead +", "black lead -"], 11, MUTED, 14, "middle")

    d.panel(30, 660, 410, 176)
    d.legend(48, 684, RED, "Power", "battery plus, 5V pin, 3.3 volts")
    d.legend(48, 728, BLACK, "Ground", "the common return. Three bars mean: connect to ground.")
    d.legend(48, 772, ORANGE, "I2C clock")
    d.legend(200, 772, BLUE, "I2C data")
    d.legend(48, 806, GREEN, "Battery measurement")
    notes = [
        "SW1: slide switch.  R1, R2: 100 kilohm.  C1: 100 nanofarad.",
        "D1: diode 1N5817, striped end towards the ESP32.",
        "",
        "Wires that cross are joined only where there is a dot.",
        "A small arc means one wire passes over the other.",
        "",
        "On the real charger board the four pads B+, B-, OUT+ and OUT-",
        "sit side by side at the end away from its USB socket.",
        "",
        "Dashed black line: SAO to GND, which fixes the sensor's address.",
    ]
    for i, line in enumerate(notes):
        d.text(1250, 668 + i * 18, line, 12, "end", fill=MUTED)
    return d.save("full-circuit.svg")


# ---------------------------------------------------------------------------------------------
# 6. Zero PCB layout. First as data, so it can be checked, then drawn.
# ---------------------------------------------------------------------------------------------
COLS = "ABCDEFGHIJKLMNOPQRST"
ROWS = 16

# Which hole each pin of each part sits in.
PLACED = {}
for i, name in enumerate(ESP_LEFT):
    PLACED[f"ESP.{name}"] = f"C{2 + i}"
for i, name in enumerate(ESP_RIGHT):
    PLACED[f"ESP.{name}"] = f"I{2 + i}"
for i, name in enumerate(IMU_PINS):
    PLACED[f"IMU.{name}"] = f"M{2 + i}"
PLACED.update({
    "SW.b": "K14", "SW.common": "L14", "SW.a": "M14",
    "D1.anode": "L12", "D1.cathode": "H12",
    "R1.1": "M12", "R1.2": "Q12",
    "R2.1": "R12", "R2.2": "R16",
    "C1.1": "S12", "C1.2": "S14",
    "TP.OUT+": "N14",   # the lead from the charger's OUT+ is soldered into this hole
    "TP.OUT-": "G14",   # and the lead from OUT- into this one
})

# Insulated wires on the solder side: (from hole, to hole, colour, what it is, only needed for stage 7?)
LINKS = [
    ("I4", "M2", RED, "3V3 to VIN", False),
    ("I3", "M4", BLACK, "GND to GND", False),
    ("I5", "M5", ORANGE, "GPIO4 to SCL", False),
    ("I6", "M6", BLUE, "GPIO3 to SDA", False),
    ("M8", "M4", BLACK, "SAO to GND", False),
    ("N14", "M14", RED, "charger OUT+ to the switch", False),
    ("L14", "L12", RED, "switch to the diode", False),
    ("H12", "I2", RED, "diode to 5V", False),
    ("G14", "I3", BLACK, "charger OUT- to GND", False),
    ("L12", "M12", RED, "switch output to R1", True),
    ("Q12", "R12", GREEN, "R1 to R2", True),
    ("R12", "S12", GREEN, "R2 to C1", True),
    ("Q12", "I9", GREEN, "R1 and R2 to GPIO0", True),
    ("S14", "R16", BLACK, "C1 to the ground end of R2", True),
    ("R16", "G14", BLACK, "that ground end to OUT-", True),
]

# How each wire is drawn, corner by corner. A wire not listed here runs straight.
ROUTES = {
    ("I4", "M2"): ["I4", "J4", "J2", "M2"],
    ("I3", "M4"): ["I3", "L3", "L4", "M4"],
    ("M8", "M4"): ["M8", "K8", "K4", "M4"],
    ("H12", "I2"): ["H12", "H1", "I1", "I2"],
    ("G14", "I3"): ["G14", "G3", "I3"],
    ("Q12", "I9"): ["Q12", "Q10", "I10", "I9"],
    ("S14", "R16"): ["S14", "S16", "R16"],
    ("R16", "G14"): ["R16", "G16", "G14"],
}


def holes_between(a, b):
    """Every hole on the straight line from a to b, both ends included."""
    (c1, r1), (c2, r2) = (COLS.index(a[0]), int(a[1:])), (COLS.index(b[0]), int(b[1:]))
    if c1 != c2 and r1 != r2:
        sys.exit(f"Layout error: the wire segment {a} to {b} is not straight")
    steps = max(abs(c2 - c1), abs(r2 - r1))
    return [f"{COLS[c1 + (c2 - c1) * i // max(steps, 1)]}{r1 + (r2 - r1) * i // max(steps, 1)}" for i in range(steps + 1)]


def check_layout():
    """Every net of NETS must be made by the layout, and no two nets may be joined."""
    holes = {}
    for pin, hole in PLACED.items():
        if hole in holes:
            sys.exit(f"Layout error: {pin} and {holes[hole]} are both placed in hole {hole}")
        holes[hole] = pin
    group = {hole: hole for hole in holes}

    def find(h):
        group.setdefault(h, h)
        while group[h] != h:
            group[h] = group[group[h]]
            h = group[h]
        return h

    for a, b, *_ in LINKS:
        group[find(a)] = find(b)

    made = {}
    for pin, hole in PLACED.items():
        made.setdefault(find(hole), set()).add(pin)
    net_of_hole = {hole: find(hole) for hole in holes}
    # A drawn wire may cross other wires, but it must not run through a pad that belongs to
    # another connection: in the drawing that would look like a joint, and on the board it
    # invites a solder bridge.
    for a, b, *_ in LINKS:
        path = ROUTES.get((a, b), [a, b])
        if path[0] != a or path[-1] != b:
            sys.exit(f"Layout error: the drawn route of {a} to {b} does not start and end there")
        for start, stop in zip(path, path[1:]):
            for hole in holes_between(start, stop):
                if hole in net_of_hole and net_of_hole[hole] != find(a):
                    sys.exit(f"Layout error: the wire {a} to {b} runs through hole {hole}, the pad of {holes[hole]}")
    made = {frozenset(pins) for pins in made.values() if len(pins) > 1}
    on_board = lambda pin: pin in PLACED
    wanted = {frozenset(p for p in pins if on_board(p)) for pins in NETS.values()}
    wanted = {pins for pins in wanted if len(pins) > 1}
    if made != wanted:
        missing = [sorted(n) for n in wanted - made]
        extra = [sorted(n) for n in made - wanted]
        sys.exit(f"Layout error. Connections the circuit needs but the layout does not make: {missing}. "
                 f"Connections the layout makes but the circuit does not have: {extra}")
    return len(wanted)


def zero_pcb():
    nets = check_layout()
    P, ox, oy = 34, 90, 160
    hx = lambda hole: ox + COLS.index(hole[0]) * P
    hy = lambda hole: oy + (int(hole[1:]) - 1) * P
    width, height = ox + len(COLS) * P + 350, oy + ROWS * P + 260
    d = Drawing(width, height, "Zero PCB layout, seen from the component side",
                "One arrangement that works. Each dot is a hole, 2.54 mm from the next. The wires run on the solder side and must be insulated.")
    d.rect(ox - P * 0.8, oy - P * 0.8, (len(COLS) - 1) * P + P * 1.6, (ROWS - 1) * P + P * 1.6, "#c8a165", "#8d6e3a", 8, 2)
    for c in COLS:
        d.text(hx(c + "1"), oy - P * 1.5, c, 11, "middle", fill=MUTED)
        for r in range(1, ROWS + 1):
            d.circle(hx(f"{c}{r}"), hy(f"{c}{r}"), 4.2, "#f7e9c8", "#8d6e3a")
    for r in range(1, ROWS + 1):
        d.text(ox - P * 1.05, hy(f"A{r}") + 4, str(r), 11, "end", fill=MUTED)

    # the two boards, drawn see-through so the wires underneath stay visible
    d.rect(hx("C1") - P * 0.55, hy("C1") - P * 0.75, 6 * P + P * 1.1, 8 * P + P * 1.0, ESP_BOARD, rx=10, opacity=0.88)
    d.rect(hx("F1") - 34, hy("F1") - P * 1.2, 68, 28, "#b0b7bd", "#6b7479", 5)
    d.text(hx("F1"), hy("F1") - P * 0.66, "USB-C", 10, "middle", fill="#263238")
    d.text(hx("E1") + 4, hy("F5") + 2, "ESP32-C3", 15, "middle", "bold", "#ffffff")
    d.text(hx("E1") + 4, hy("F5") + 22, "Super Mini", 12, "middle", fill="#cfd8dc")
    d.rect(hx("M1") - P * 0.55, hy("M2") - P * 0.6, 4 * P + P * 0.6, 6 * P + P * 1.2, IMU_BOARD, rx=9, opacity=0.9)
    d.text(hx("O1") + 14, hy("O4") + 4, "GY-BMI160", 13, "middle", "bold", "#ffffff")
    d.rect(hx("O1") - 2, hy("O5") + 4, 30, 30, "#1c2b26", rx=2)

    # ---- wires first, so the parts and pins sit on top of them ----
    for a, b, colour, _, optional in LINKS:
        points = [(hx(h), hy(h)) for h in ROUTES.get((a, b), [a, b])]
        if (a, b) == ("M8", "M4"):
            d.wire(points, colour, 3, "7 5")
        else:
            d.wire(points, colour, 4.5)

    used_esp = {"5V", "GND", "3V3", f"GPIO{PINS['SCL']}", f"GPIO{PINS['SDA']}", f"GPIO{PINS['BATTERY']}"}
    for pin, hole in PLACED.items():
        part, name = pin.split(".")
        x, y = hx(hole), hy(hole)
        if part == "ESP":
            d.circle(x, y, 6.5, GOLD, GOLD_EDGE)
            short = name.replace("GPIO", "")
            if hole[0] == "C":
                d.text(x + 12, y + 4, short, 10, fill="#cfd8dc")
            else:
                d.text(x - 12, y + 4, short, 10, "end", "bold" if name in used_esp else "normal", "#ffffff" if name in used_esp else "#90a4ae")
        elif part == "IMU":
            d.circle(x, y, 6.5, GOLD, GOLD_EDGE)
            wired = name in ("VIN", "GND", "SCL", "SDA", "SAO")
            d.text(x + 12, y + 4, name, 10, weight="bold" if wired else "normal", fill="#ffffff" if wired else "#8fb3a6")

    # ---- the small parts ----
    def leads(a, b):
        d.wire([(hx(a), hy(a)), (hx(b), hy(b))], "#9e9e9e", 2.5)

    def pads(*holes):
        for hole in holes:
            d.circle(hx(hole), hy(hole), 5.5, "#cfd8dc", "#546e7a")

    leads("M12", "Q12")                                             # R1
    d.rect(hx("O12") - 32, hy("O12") - 8, 64, 16, "#e8dcc0", INK, 4, 1.5)
    d.text(hx("O12"), hy("O12") + 4, "R1 100k", 9, "middle")
    pads("M12", "Q12")
    leads("R12", "R16")                                             # R2
    d.rect(hx("R14") - 8, hy("R14") - 32, 16, 64, "#e8dcc0", INK, 4, 1.5)
    d.add(f'<text x="{hx("R14")}" y="{hy("R14")}" font-size="9" text-anchor="middle" fill="{INK}" {FONT} transform="rotate(-90 {hx("R14")} {hy("R14")})" dy="3">R2 100k</text>')
    pads("R12", "R16")
    leads("S12", "S14")                                             # C1
    d.rect(hx("S13") - 9, hy("S13") - 13, 18, 26, "#e6a23c", INK, 6, 1.5)
    d.text(hx("S13") + 16, hy("S13") + 4, "C1", 10, weight="bold")
    pads("S12", "S14")
    leads("H12", "L12")                                             # D1, cathode (the stripe) at H12
    d.rect(hx("J12") - 32, hy("J12") - 9, 64, 18, "#424242", INK, 4, 1.5)
    d.rect(hx("J12") - 27, hy("J12") - 9, 9, 18, "#eceff1", rx=1)
    d.text(hx("J12") + 6, hy("J12") + 4, "D1", 10, "middle", "bold", "#ffffff")
    d.text(hx("J12"), hy("J12") + 24, "stripe on the left", 9, "middle", fill=INK)
    pads("H12", "L12")
    d.rect(hx("K14") - 16, hy("K14") - 13, 2 * P + 32, 26, "#263238", rx=4)   # SW1
    d.rect(hx("L14") + 2, hy("L14") + 13, 26, 12, "#b0bec5", "#546e7a", 2)
    d.text(hx("K14") - 24, hy("K14") + 4, "SW1", 10, "end", "bold")
    d.text(hx("L14"), hy("L14") + 40, "on = lever to the right", 9, "middle", fill=INK)
    pads("K14", "L14", "M14")
    d.circle(hx("N14"), hy("N14"), 7, RED)
    d.circle(hx("G14"), hy("G14"), 7, BLACK)

    # ---- the charger and the cell, which are not on the board ----
    ty = hy("A16") + 90
    left, right = hx("F1") - 26, hx("O1") + 60
    d.rect(left, ty, right - left, 84, TP_BOARD, rx=8)
    d.text((left + right) / 2 - 10, ty + 54, "TP4056 charger, taped beside the board or to the bat", 12, "middle", "bold", "#ffffff")
    d.rect(left - 28, ty + 26, 32, 34, "#b0b7bd", "#6b7479", 4)
    d.text(left - 36, ty + 47, "USB", 10, "end", fill=MUTED)
    spots = {"OUT-": hx("G1"), "OUT+": hx("N1"), "B+": right - 58, "B-": right - 22}
    for name, x in spots.items():
        d.circle(x, ty + 14, 6, GOLD, GOLD_EDGE)
        d.text(x, ty + 33, name, 9, "middle", fill="#ffffff")
    d.wire([(hx("G14"), hy("G14")), (hx("G14"), ty + 14)], BLACK, 4)
    d.wire([(hx("N14"), hy("N14")), (hx("N14"), ty + 14)], RED, 4)
    d.text(hx("G14") - 10, ty - 14, "lead from OUT-", 10, "end", fill=MUTED)
    d.text(hx("N14") - 10, ty - 14, "lead from OUT+", 10, "end", fill=MUTED)
    bx = right + 60
    d.rect(bx, ty, 150, 84, "#eceff1", "#90a4ae", 8, 2)
    d.text(bx + 75, ty + 40, "LiPo cell", 13, "middle", "bold")
    d.text(bx + 75, ty + 58, "3.7 V 1500 mAh", 10, "middle", fill=MUTED)
    d.wire([(spots["B+"], ty + 14), (spots["B+"], ty - 16), (bx + 40, ty - 16), (bx + 40, ty)], RED, 4)
    d.wire([(spots["B-"], ty + 14), (spots["B-"], ty - 32), (bx + 80, ty - 32), (bx + 80, ty)], BLACK, 4)

    # ---- the lists on the right ----
    lx, yy = ox + len(COLS) * P + 6, oy - 14
    d.text(lx, yy, "Wires, pad to pad", 14, weight="bold")
    yy += 26
    said_optional = False
    for a, b, colour, what, optional in LINKS:
        if optional and not said_optional:
            said_optional = True
            yy += 8
            d.text(lx, yy, "Only for stage 7", 12, weight="bold", fill=GREEN)
            yy += 22
        dash = "7 5" if (a, b) == ("M8", "M4") else None
        d.wire([(lx, yy - 4), (lx + 26, yy - 4)], colour, 3 if dash else 5, dash)
        d.text(lx + 34, yy, f"{a} to {b}", 12, weight="bold", mono=True)
        d.text(lx + 130, yy, what, 11, fill=MUTED)
        yy += 23
    yy += 12
    d.text(lx, yy, "Parts", 14, weight="bold")
    yy += 22
    for line in ("D1   H12 (stripe) and L12", "SW1  K14, L14, M14", "R1   M12 and Q12", "R2   R12 and R16", "C1   S12 and S14",
                 "OUT+ lead  N14", "OUT- lead  G14"):
        d.text(lx, yy, line, 12, mono=True)
        yy += 19

    by = ty + 122
    d.note(30, by, [
        "The hole positions assume this pin order: 5V, GND, 3V3, 4, 3, 2, 1, 0 down the right side of the ESP32, and VIN, 3V3, GND, SCL, SDA, CS, SAO on the sensor.",
        "If your boards are printed in another order, keep the label-to-label connections of the table in the README and move the wires to suit.",
        f"The script that drew this checked it: the layout makes all {nets} on-board connections of the circuit, joins nothing else, and no wire runs through another connection's pad.",
        "Some wires cross each other, which is why they must be insulated. The two leads from the charger are soldered at N14 and G14 and leave the board there.",
    ], 12, MUTED, 19)
    return d.save("zero-pcb-layout.svg")


# ---------------------------------------------------------------------------------------------
# 7. to 10. How the parts talk to each other
# ---------------------------------------------------------------------------------------------
def arrow(d, x1, y, x2, colour=INK):
    d.wire([(x1, y), (x2 - 8, y)], colour, 3)
    d.add(f'<path d="M{x2},{y} L{x2-12},{y-7} L{x2-12},{y+7} Z" fill="{colour}"/>')


def data_flow():
    d = Drawing(1240, 560, "From a swing to a number on the screen",
                "What each part does, and what travels between them.")
    boxes = [
        (30, "Bat", ["The bat turns", "and speeds up."], "#eceff1", INK),
        (250, "BMI160 sensor", ["Measures acceleration", "and rotation 100 times", "a second. Keeps the", "newest six numbers", "in its registers."], IMU_BOARD, "#ffffff"),
        (520, "ESP32-C3", ["Asks for the numbers", "50 times a second,", "takes off the still", "value and packs them", "into 14 bytes."], ESP_BOARD, "#ffffff"),
        (790, "Chrome or Edge", ["The browser's Web", "Bluetooth receives", "each packet and", "hands it to the page."], "#34495e", "#ffffff"),
        (1030, "SwingLab dashboard", ["Unpacks the numbers", "and works out bat", "speed, impact and", "shot direction."], "#0c1420", "#ffffff"),
    ]
    top, bh = 110, 170
    for x, title, lines, fill, ink in boxes:
        d.rect(x, top, 180, bh, fill, "#90a4ae" if fill == "#eceff1" else "none", 10, 2)
        d.text(x + 90, top + 30, title, 15, "middle", "bold", ink)
        for i, line in enumerate(lines):
            d.text(x + 90, top + 58 + i * 18, line, 12, "middle", fill=ink if fill == "#eceff1" else "#d5dde2")
    links = [
        (210, 250, "movement", ["nothing electrical:", "the sensor is fixed", "to the bat"]),
        (430, 520, "I2C", ["2 wires, SDA and SCL", "12 bytes per reading", "ESP32 asks, sensor answers"]),
        (700, 790, "Bluetooth LE", ["radio, no wires", "14 bytes, 50 a second", "the bat pushes (notify)"]),
        (970, 1030, "in the computer", ["a JavaScript event", "per packet"]),
    ]
    for x1, x2, head, lines in links:
        arrow(d, x1 + 4, top + bh / 2, x2 - 4)
        mid = (x1 + x2) / 2
        d.text(mid, top + bh + 30, head, 13, "middle", "bold")
        for i, line in enumerate(lines):
            d.text(mid, top + bh + 50 + i * 16, line, 11, "middle", fill=MUTED)
    d.text(mid, top + bh + 30, "", 1)

    # the other road, stage 9
    y2 = 440
    d.rect(520, y2, 180, 60, PANEL, RULE, 10)
    d.text(610, y2 + 26, "ESP32-C3", 13, "middle", "bold")
    d.text(610, y2 + 44, "stage 9, optional", 11, "middle", fill=MUTED)
    d.rect(1030, y2, 180, 60, PANEL, RULE, 10)
    d.text(1120, y2 + 26, "The API", 13, "middle", "bold")
    d.text(1120, y2 + 44, "java-cricket-backend", 11, "middle", fill=MUTED)
    arrow(d, 704, y2 + 30, 1026, MUTED)
    d.text(865, y2 + 20, "WiFi, then HTTP", 13, "middle", "bold")
    d.text(865, y2 + 48, "one POST request per reading, as JSON text, 10 a second", 11, "middle", fill=MUTED)
    d.text(30, y2 + 20, "The other road in:", 13, weight="bold")
    d.text(30, y2 + 40, "the same readings can go straight to the backend,", 12, fill=MUTED)
    d.text(30, y2 + 57, "without a browser. The dashboard does not need this.", 12, fill=MUTED)
    return d.save("how-data-flows.svg")


def i2c_read():
    d = Drawing(1240, 520, "One reading on the I2C wires",
                "What passes along the SDA wire when the ESP32 fetches the 12 data bytes. Time runs from left to right.")
    MASTER, SLAVE = ESP_BOARD, IMU_BOARD
    cells = [
        ("START", MASTER, 62, ["ESP32 takes", "the bus"]),
        ("0x68 + write", MASTER, 104, ["the sensor's", "address, and", "\"I will write\""]),
        ("ACK", SLAVE, 46, ["sensor:", "\"here\""]),
        ("0x0C", MASTER, 60, ["the register", "the data", "starts at"]),
        ("ACK", SLAVE, 46, ["\"got it\""]),
        ("START", MASTER, 62, ["again, without", "letting go"]),
        ("0x68 + read", MASTER, 100, ["the address", "again, and", "\"now I read\""]),
        ("ACK", SLAVE, 46, ["\"here\""]),
        ("byte 1", SLAVE, 62, ["rotation x,", "low byte"]),
        ("ACK", MASTER, 46, ["ESP32:", "\"more\""]),
        ("byte 2", SLAVE, 62, ["rotation x,", "high byte"]),
        ("ACK", MASTER, 46, ["\"more\""]),
        ("...", "#b0bec5", 44, ["bytes", "3 to 11"]),
        ("byte 12", SLAVE, 66, ["acceleration", "z, high byte"]),
        ("NACK", MASTER, 54, ["\"that is", "enough\""]),
        ("STOP", MASTER, 56, ["bus is", "free"]),
    ]
    x, y, h = 30, 130, 54
    for label, fill, w, below in cells:
        d.rect(x, y, w - 4, h, fill, rx=5)
        d.text(x + (w - 4) / 2, y + h / 2 + 5, label, 12, "middle", "bold", "#ffffff" if fill != "#b0bec5" else INK)
        for i, line in enumerate(below):
            d.text(x + (w - 4) / 2, y + h + 20 + i * 15, line, 11, "middle", fill=MUTED)
        x += w
    d.rect(30, 250, 22, 16, MASTER, rx=3)
    d.text(60, 263, "the ESP32 is talking", 12)
    d.rect(230, 250, 22, 16, SLAVE, rx=3)
    d.text(260, 263, "the sensor is talking", 12)
    d.panel(30, 300, 1180, 196)
    d.note(50, 328, [
        "Two wires, shared by every device. SCL is the clock: the ESP32 ticks it, and one bit of data moves on SDA with every tick. Nobody talks unless the ESP32 asks.",
        "Address. Every device has a number. This sensor is 0x68 when its SAO pin is joined to GND and 0x69 when SAO is joined to 3.3 volts. Only the device called answers.",
        "Register. Inside the sensor the values sit in numbered boxes. 0x0C is the first of twelve boxes holding the newest reading. After each byte the sensor moves on to",
        "the next box by itself, which is why one request brings all twelve.",
        "ACK. After every byte the receiver pulls SDA low for one tick to say \"got it\". No ACK after the address means nobody is there: that is what stage 2 looks for.",
        "Speed. Stage 2 and 3 use 100 000 ticks a second. Later stages use 400 000, and the whole exchange above then takes about a third of a millisecond.",
        "Pull-up resistors hold both wires at 3.3 volts when nobody pulls them low. The GY-BMI160 board has them built in, so none are added in this kit.",
    ], 12, INK, 22)
    return d.save("i2c-one-reading.svg")


def packet():
    # A packet taken from the simulator in the middle of a swing.
    data = bytes.fromhex("35057203121040fd060b4c010000")
    counts = [int.from_bytes(data[i:i + 2], "little", signed=True) for i in range(0, 14, 2)]
    names = ["ax", "ay", "az", "gx", "gy", "gz", "temperature"]
    d = Drawing(1240, 600, "The 14-byte packet",
                "What the bat sends 50 times a second, and how the dashboard reads it. The example was taken during a swing.")
    x0, y0, cw = 60, 110, 80
    colours = [RED, RED, RED, BLUE, BLUE, BLUE, GREY]
    for i in range(14):
        x = x0 + i * cw
        d.rect(x, y0, cw - 6, 52, PANEL, RULE, 6)
        d.text(x + (cw - 6) / 2, y0 + 33, f"{data[i]:02X}", 20, "middle", "bold", mono=True)
        d.text(x + (cw - 6) / 2, y0 - 10, f"byte {i}", 11, "middle", fill=MUTED)
    for f in range(7):
        x = x0 + f * 2 * cw
        d.wire([(x + 4, y0 + 66), (x + 2 * cw - 10, y0 + 66)], colours[f], 4)
        cx = x + cw - 3
        d.text(cx, y0 + 90, names[f], 15, "middle", "bold", colours[f] if f < 6 else MUTED)
        d.text(cx, y0 + 112, f"0x{data[2*f+1]:02X}{data[2*f]:02X}", 13, "middle", mono=True)
        d.text(cx, y0 + 132, f"= {counts[f]} counts", 12, "middle")
        if f < 3:
            d.text(cx, y0 + 152, "/ 4096", 12, "middle", fill=MUTED)
            d.text(cx, y0 + 174, f"{counts[f] / 4096:+.3f} g", 14, "middle", "bold")
        elif f < 6:
            d.text(cx, y0 + 152, "/ 16.4", 12, "middle", fill=MUTED)
            d.text(cx, y0 + 174, f"{counts[f] / 16.4:+.1f} deg/s", 14, "middle", "bold")
        else:
            d.text(cx, y0 + 152, "not used by", 12, "middle", fill=MUTED)
            d.text(cx, y0 + 170, "the dashboard", 12, "middle", fill=MUTED)
    import math
    gx, gy, gz = (c / 16.4 for c in counts[3:6])
    ax, ay, az = (c / 4096 for c in counts[0:3])
    rotation = math.sqrt(gx * gx + gy * gy + gz * gz)
    speed = min(rotation * math.pi / 180 * 0.85 * 3.6, 180) if rotation >= 5 else 0
    impact = math.sqrt(ax * ax + ay * ay + az * az)
    d.panel(30, 320, 1180, 256)
    d.note(50, 348, ["What the dashboard does with these six numbers"], 14, INK, 20, weight="bold")
    d.note(50, 376, [
        f"Total rotation   = square root of gx squared + gy squared + gz squared                    = {rotation:.1f} degrees per second",
        f"Bat speed        = total rotation x pi / 180 x 0.85 m x 3.6                               = {speed:.1f} km/h",
        f"Impact           = square root of ax squared + ay squared + az squared                    = {impact:.2f} g",
        f"Shot direction   = leg side if gz is above +1, off side if below -1, otherwise straight   = {'leg side' if gz > 1 else 'off side' if gz < -1 else 'straight'}",
    ], 12, INK, 22, )
    d.note(50, 476, [
        "Low byte first. The number 0x0B06 travels as 06 then 0B. Both ends must agree on this, and on the order of the seven numbers.",
        "Signed. Each number runs from -32768 to +32767. A value with its top bit set is negative: 0xFD40 is -704.",
        "Why counts and not units. Seven whole numbers fit in 14 bytes. The same values as text, as stage 9 sends them over WiFi, need about 110 bytes.",
        "The scale, 4096 and 16.4, comes from the sensor's ranges of 8 g and 2000 degrees per second. Change a range and both ends have to change.",
    ], 12, INK, 22)
    return d.save("packet.svg")


def bluetooth():
    d = Drawing(1240, 640, "How the bat and the dashboard find each other over Bluetooth",
                "Bluetooth Low Energy. The bat is the peripheral: it holds the data. The browser is the central: it connects and listens.")
    # what the bat offers, as a tree
    d.panel(30, 90, 520, 526)
    d.text(50, 120, "What the bat offers", 14, weight="bold")
    d.rect(50, 140, 480, 46, ESP_BOARD, rx=8)
    d.text(66, 169, "Device", 12, fill="#cfd8dc")
    d.text(130, 169, "SwingLab-Bat", 15, weight="bold", fill="#ffffff")
    d.wire([(80, 186), (80, 240), (100, 240)], MUTED, 2)
    d.rect(100, 212, 430, 56, "#34495e", rx=8)
    d.text(116, 234, "Service: a folder of values", 12, fill="#cfd8dc")
    d.text(116, 256, "4fafc201-1fb5-459e-8fcc-c5c9c331914b", 13, weight="bold", fill="#ffffff", mono=True)
    d.wire([(130, 268), (130, 322), (150, 322)], MUTED, 2)
    d.rect(150, 286, 380, 92, IMU_BOARD, rx=8)
    d.text(166, 308, "Characteristic: one value", 12, fill="#d5e5de")
    d.text(166, 330, "beb5483e-36e1-4688-b7f5-ea07361b26a8", 12, weight="bold", fill="#ffffff", mono=True)
    d.text(166, 351, "14 bytes: the newest reading", 12, fill="#ffffff")
    d.text(166, 369, "can be read, and is pushed (notify)", 12, fill="#d5e5de")
    d.wire([(80, 240), (80, 438), (100, 438)], MUTED, 2)
    d.rect(100, 410, 430, 56, "#34495e", rx=8)
    d.text(116, 432, "Service 0x180F: battery, the standard one", 12, fill="#cfd8dc")
    d.text(116, 454, "only with the stage 7 parts", 12, fill="#ffffff")
    d.wire([(130, 466), (130, 514), (150, 514)], MUTED, 2)
    d.rect(150, 484, 380, 60, IMU_BOARD, rx=8)
    d.text(166, 506, "Characteristic 0x2A19", 12, weight="bold", fill="#ffffff")
    d.text(166, 528, "1 byte: charge in percent, 0 to 100", 12, fill="#d5e5de")
    d.note(50, 572, ["The long ids were picked at random, once. They only have to be the same", "in the firmware (config.h) and in the web app (sensor-packet.ts)."], 12, MUTED, 18)

    # the conversation, step by step
    steps = [
        ("1", "The bat advertises", "A few times a second it broadcasts: my name is SwingLab-Bat, and I offer service 4fafc201. Its light blinks slowly."),
        ("2", "You press Connect Sensor", "The browser lists nearby devices whose name starts with SwingLab. A web page may only see what you pick yourself."),
        ("3", "The browser connects", "From now on the two take turns many times a second. The bat stops advertising and its light stays on."),
        ("4", "The page finds the value", "It asks for the service by its id, then for the characteristic by its id."),
        ("5", "The page asks for notifications", "It flips a small switch on the characteristic that means: push me every new value."),
        ("6", "The bat sends 14 bytes every 20 ms", "Nobody has to ask again. The dashboard uses a packet only while a shot is being recorded."),
        ("7", "The connection ends", "The page closes or goes out of range. The bat starts advertising again by itself."),
    ]
    x, y = 590, 100
    for number, head, text in steps:
        d.circle(x + 16, y + 16, 15, ESP_BOARD)
        d.text(x + 16, y + 21, number, 14, "middle", "bold", "#ffffff")
        d.text(x + 44, y + 14, head, 14, weight="bold")
        words, line, lines = text.split(), "", []
        for word in words:
            if len(line) + len(word) + 1 > 92:
                lines.append(line)
                line = word
            else:
                line = (line + " " + word).strip()
        lines.append(line)
        for i, l in enumerate(lines):
            d.text(x + 44, y + 34 + i * 17, l, 12, fill=MUTED)
        y += 74
    return d.save("bluetooth.svg")


if __name__ == "__main__":
    made = [pin_map(), sensor_wiring(), power_path(), battery_sense(), full_circuit(), zero_pcb(),
            data_flow(), i2c_read(), packet(), bluetooth()]
    print("drew " + ", ".join(made))

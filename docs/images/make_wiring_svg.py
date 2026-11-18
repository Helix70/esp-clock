"""Generate wiring-esp32c5.svg (HU-058 socket U4 -> Waveshare ESP32-C5 kit).

Run from this folder:  python make_wiring_svg.py
"""

LEFT = [  # socket pin, label
    ("1", "CLK_1 · driver 2 clock"),
    ("2", "DATA_1 · driver 2 data"),
    ("3", "R_T · NTC divider"),
    ("4", "R_P · LDR divider"),
    ("5", "DATA · driver 1 data"),
    ("6", "+5V"),
    ("8", "GND"),
    ("9", "S1 · button 1 (top)"),
    ("10", "S2 · button 2 (bottom)"),
    ("11", "R_B · buzzer (via Q1)"),
    ("12", "DS1302 SCLK"),
    ("13", "DS1302 I/O"),
    ("14", "DS1302 CE (RST)"),
    ("16", "CLK · driver 1 clock *"),
]

RIGHT = [  # header pin, label
    ("P1-1", "3V3"),
    ("P1-3", "GPIO2 · NTC supply"),
    ("P1-4", "GPIO3 · LDR ADC"),
    ("P1-5", "GPIO0 · RTC CE"),
    ("P1-6", "GPIO1 · NTC ADC"),
    ("P1-7", "GPIO6 · buzzer"),
    ("P1-9", "GPIO8 · minutes DIN"),
    ("P1-10", "GPIO9 · minutes CLK"),
    ("P1-11", "GPIO10 · button 1"),
    ("P1-12", "GPIO26 · button 2"),
    ("P1-14", "5V"),
    ("P1-15", "GND"),
    ("P2-4", "GPIO24 · hours DIN"),
    ("P2-5", "GPIO23 · hours CLK"),
    ("P2-8", "GPIO4 · RTC SCLK"),
    ("P2-9", "GPIO5 · RTC I/O"),
]

# (left socket pin, right header pin, colour, through the NPN buzzer driver)
WIRES = [
    ("1", "P1-10", "#6a3fc4", False),
    ("2", "P1-9", "#2f7de1", False),
    ("3", "P1-6", "#e07b00", False),
    ("4", "P1-4", "#c99a00", False),
    ("5", "P2-4", "#1aa0b8", False),
    ("6", "P1-14", "#d62828", False),
    ("8", "P1-15", "#222222", False),
    ("9", "P1-11", "#2a9d3a", False),
    ("10", "P1-12", "#7cb342", False),
    ("11", "P1-7", "#b5179e", True),
    ("12", "P2-8", "#5c6bc0", False),
    ("13", "P2-9", "#00897b", False),
    ("14", "P1-5", "#8d6e63", False),
    ("16", "P2-5", "#8e44ad", False),
]

ROW = 40
LY0 = 110          # first socket row
RY0 = LY0 + ROW // 2  # kit rows sit half a row lower so runs never overlap
LX, RX = 330, 670  # pin dot x positions
LANE0, LANE_STEP = 365, 16

ly = {p: LY0 + i * ROW for i, (p, _) in enumerate(LEFT)}
ry = {p: RY0 + j * ROW for j, (p, _) in enumerate(RIGHT)}

left_h = LY0 + (len(LEFT) - 1) * ROW + 30
right_h = RY0 + (len(RIGHT) - 1) * ROW + 30
notes_y = right_h + 40
NOTE_COUNT = 6
height = notes_y + NOTE_COUNT * 22 + 10

out = []
w = out.append
w(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 {height}" '
  f'width="1000" height="{height}" font-family="Segoe UI, Helvetica, Arial, sans-serif" font-size="14">')
w(f'  <rect width="1000" height="{height}" fill="#ffffff"/>')
w('  <text x="500" y="34" text-anchor="middle" font-size="20" font-weight="bold" fill="#111">'
  'HU-058 MCU socket U4 → Waveshare ESP32-C5-WIFI6-KIT</text>')
w('  <text x="500" y="56" text-anchor="middle" fill="#555">STC8G1K17 removed. '
  'HU-058 / HU-058SE (DS1302 RTC). Firmware defaults from main/Kconfig.projbuild.</text>')

w(f'  <rect x="30" y="72" width="300" height="{left_h - 72}" rx="8" fill="#f4f6fa" stroke="#334" stroke-width="1.5"/>')
w('  <text x="180" y="92" text-anchor="middle" font-weight="bold" fill="#111">HU-058 socket U4 (DIP-16)</text>')
w(f'  <rect x="670" y="72" width="300" height="{right_h - 72}" rx="8" fill="#f4f6fa" stroke="#334" stroke-width="1.5"/>')
w('  <text x="820" y="92" text-anchor="middle" font-weight="bold" fill="#111">ESP32-C5 kit headers</text>')

for p, label in LEFT:
    w(f'  <text x="44" y="{ly[p] + 5}" fill="#111"><tspan font-weight="bold">{p}</tspan>  {label}</text>')
for p, label in RIGHT:
    w(f'  <text x="684" y="{ry[p] + 5}" fill="#111"><tspan font-weight="bold">{p}</tspan>  {label}</text>')

w('  <g fill="none" stroke-width="3" stroke-linejoin="round">')
drivers = []
for k, (lp, rp, colour, npn) in enumerate(WIRES):
    x = LANE0 + k * LANE_STEP
    y1, y2 = ly[lp], ry[rp]
    w(f'    <polyline points="{LX},{y1} {x},{y1} {x},{y2} {RX},{y2}" stroke="{colour}"/>')
    if npn:
        drivers.append(y2)
w('  </g>')

# NPN driver boxes on the right-hand runs.
for y in drivers:
    w(f'  <rect x="602" y="{y - 11}" width="44" height="22" rx="3" fill="#fff7d6" stroke="#8a6d00" stroke-width="1.5"/>')
    w(f'  <text x="624" y="{y + 4}" text-anchor="middle" font-size="11" font-weight="bold" fill="#8a6d00">NPN</text>')

# 10K pull-ups to the two sensor nets: LDR (GPIO3) from 3V3, NTC (GPIO1)
# from GPIO2. The LDR run drops at x=600, left of the NTC run, so the two
# never cross; both join their socket wire where it passes.
y3v3, y_g2, y_ldr, y_ntc = ry["P1-1"], ry["P1-3"], ry["P1-4"], ry["P1-6"]
w('  <g stroke="#d62828" stroke-width="2.5" fill="none">')
w(f'    <polyline points="{RX},{y3v3} 600,{y3v3} 600,{y3v3 + 10}"/>')
w(f'    <line x1="600" y1="{y_ldr - 10}" x2="600" y2="{y_ldr}"/>')
w(f'    <polyline points="{RX},{y_g2} 625,{y_g2} 625,{y_ntc - 32}"/>')
w(f'    <line x1="625" y1="{y_ntc - 12}" x2="625" y2="{y_ntc}"/>')
w('  </g>')
w(f'  <rect x="593" y="{y3v3 + 10}" width="14" height="{y_ldr - y3v3 - 20}" fill="#fff" stroke="#111" stroke-width="1.5"/>')
# Sits between the GPIO0 and GPIO1 rows so it is clearly not on the CE line.
w(f'  <rect x="618" y="{y_ntc - 32}" width="14" height="20" fill="#fff" stroke="#111" stroke-width="1.5"/>')
w(f'  <text x="572" y="{y3v3 + 24}" font-size="10" fill="#111">10K</text>')
w(f'  <text x="636" y="{y_ntc - 18}" font-size="10" fill="#111">10K</text>')
w('  <g fill="#111">')
w(f'    <circle cx="600" cy="{y_ldr}" r="4"/><circle cx="625" cy="{y_ntc}" r="4"/>')
w('  </g>')

w('  <g fill="#111">')
w('    ' + ''.join(f'<circle cx="{LX}" cy="{y}" r="5"/>' for y in ly.values()))
w('    ' + ''.join(f'<circle cx="{RX}" cy="{y}" r="5"/>' for y in ry.values()))
w('  </g>')

notes = [
    "* Driver 1 clock: pin 16 on the HU-058 / HU-058SE. On the HU-058D (no RTC) it is pin 14 instead, and pins 12-14 carry no RTC.",
    "NPN = GPIO6 via 10K to NPN base, emitter GND, collector to socket 11. DS1302 VCC2 from 3V3.",
    "Do not fit R1 or R2 on the clock board: they pull the sensor nets to +5V. Fit the two 10K resistors shown instead.",
    "NTC: 100nF across it (socket pin 3 to GND). GPIO2 powers its divider only while reading. Never wire GPIO15 (PSRAM CS).",
    "Not connected: socket 7 (P5.5) and 15.",
    "Driver 1 = hour digits + AM + colon. Driver 2 = minute digits + dash + degree.",
]
assert len(notes) == NOTE_COUNT
w('  <g fill="#333" font-size="13">')
for i, n in enumerate(notes):
    w(f'    <text x="30" y="{notes_y + i * 22}">{n}</text>')
w('  </g>')
w('</svg>')

with open("wiring-esp32c5.svg", "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(out) + "\n")
print("wrote wiring-esp32c5.svg")

# ESP Clock

ESP32-C5 firmware for the HU-058 LED clock PCB. Drives two AiP33628 LED
drivers via GPIO bit-bang to display time and temperature on four
seven-segment digits with RGB LEDs and five annunciators.

## Features

- WiFi 6 (802.11ax) time synchronisation via NTP (hourly refresh)
- Captive portal for initial WiFi setup and configuration
- 12-hour or 24-hour display with AM indicator
- External NTC thermistor temperature readout (Celsius or Fahrenheit)
- Configurable LED colour (per-channel R/G/B enable)
- Five brightness levels with perceptual gamma mapping
- Blinking colon separator (optional)
- Button controls: mode switch, config portal, factory reset
- All settings persisted in NVS flash

## Hardware

| Component | Description |
| --- | --- |
| MCU | ESP32-C5-WROOM-1 (RISC-V 240 MHz, WiFi 6) |
| Display PCB | HU-058 with STC microcontroller removed |
| LED Drivers | 2x AiP33628 (30-bit shift register, two-wire) |
| Display | 4x seven-segment digits, 5 annunciators, all RGB |
| Temp sensor | NTC thermistor on HU-058 PCB (10K pull-up, NTC to GND) |

### Default Pin Assignments

| Function | GPIO | Notes |
| --- | --- | --- |
| Hours DIN | 24 | AiP33628 data line (hours half) |
| Hours CLK | 23 | AiP33628 clock line (hours half) |
| Minutes DIN | 8 | AiP33628 data line (minutes half) |
| Minutes CLK | 9 | AiP33628 clock line (minutes half) |
| Button 1 | 10 | Mode switch / hold for config |
| Button 2 | 26 | Used with Button 1 for factory reset |
| NTC ADC | 1 | NTC thermistor voltage divider input (ADC1) |
| LDR ADC | 3 | Light sensor voltage divider input (ADC1) |
| Status LED | 27 | On-board WS2812 (Waveshare ESP32-C5 kit) |
| USB D+/D- | 14/13 | Console output (USB Serial JTAG) |

All pin assignments are configurable via `idf.py menuconfig` under
"ESP Clock Configuration".

Defaults target the Waveshare ESP32-C5-WIFI6-KIT (ESP32-C5-WROOM-1 module).
Pins to avoid on that board: GPIO15 (not bonded out), GPIO27 (WS2812),
GPIO6 (battery divider), GPIO4/5 (on-board pull-ups), GPIO28 (BOOT key),
GPIO11/12 (UART0 to CH343P), GPIO13/14 (USB). ADC1 covers GPIO1-GPIO6 only.

### Status LED

| Colour | Pattern | Meaning |
| --- | --- | --- |
| White | Solid | Booting / lamp test |
| Yellow | Fast flash (50 ms on, 250 ms off) | Connecting to WiFi |
| Green | 1 Hz heartbeat (80 ms flash) | Running (main loop alive) |
| Red | Slow flash (100 ms on, 900 ms off) | WiFi connection failed |
| Blue | Slow flash (100 ms on, 900 ms off) | Config portal active |

WS2812 LEDs cannot be PWM dimmed; brightness is set by scaling the colour
values (`LED_MAX_LEVEL` in `components/status_led/src/status_led.c`).

### Wiring

Connect the ESP32-C5 dev board to the HU-058 PCB connector:

| HU-058 Pin | ESP32-C5 |
| --- | --- |
| Hours DIN | GPIO24 |
| Hours CLK | GPIO23 |
| Minutes DIN | GPIO8 |
| Minutes CLK | GPIO9 |
| NTC | GPIO1 |
| LDR | GPIO3 |
| VCC | 3V3 |
| GND | GND |

Buttons should be wired between the GPIO pin and GND (active low, internal
pull-ups are enabled).

## Build

Requires ESP-IDF v5.4 or later.

```bash
# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Configure for ESP32-C5 (--preview required, still in preview in v5.4)
idf.py --preview set-target esp32c5

# Build
idf.py --preview build

# Flash and monitor
idf.py -p PORT flash monitor
```

### Configuration

```bash
idf.py menuconfig
```

Under "ESP Clock Configuration":

- **Log output channel** — USB CDC (default) or UART0
- **GPIO pins** — hours DIN/CLK, minutes DIN/CLK, button 1/2, NTC ADC,
  LDR ADC, status LED

## Usage

### First Boot

1. Power on the clock — it displays "boot" and starts a WiFi access point
   named `ESP-Clock-XXXX`
2. Connect a phone or laptop to the AP
3. A captive portal page opens automatically (or navigate to `192.168.4.1`)
4. Pick your WiFi network from the scanned list (or type a hidden SSID),
   enter the password, choose a timezone, and set display preferences
5. Press "Save & Restart" — the clock connects to WiFi and syncs time via NTP

### Button Controls

| Action | Result |
| --- | --- |
| Button 1 press | Toggle between time and temperature display |
| Button 1 hold (2s) | Enter configuration portal |
| Both buttons held (2s) | Factory reset (erases all settings) |

### Settings

All settings are configurable through the web portal:

| Setting | Description | Default |
| --- | --- | --- |
| WiFi SSID/Password | Network credentials | (none) |
| NTP Server | Time sync server | pool.ntp.org |
| Timezone | City list; stored as a POSIX TZ string so DST is automatic | UTC |
| 24-hour format | 24h vs 12h display | On |
| Blink separator | Colon blinks each second | Off |
| Brightness | Display brightness 1-5 | 3 |
| LED R/G/B | Colour channel enables | All on (white) |
| Temperature unit | Celsius or Fahrenheit | Celsius |
| Temp calibration | Offset in tenths of a degree | 0 |

## Architecture

```text
main.c ──┬── hu058 ──── aip33628 ──── aip33628_hal (GPIO bit-bang)
          ├── buttons ── buttons_hal (GPIO + esp_timer)
          ├── clock_settings (NVS)
          ├── wifi_manager (esp_wifi STA)
          ├── clock_ntp (esp_sntp)
          └── captive_portal (soft AP + DNS + HTTP)
```

### Portable Components

The `aip33628`, `hu058`, and `buttons` components contain no ESP-IDF
dependencies. To port to a different platform, implement:

- `aip33628_hal_init()` and `aip33628_hal_send_frame()` for your GPIO API
- `buttons_hal_init()` with a 20ms periodic timer calling `buttons_poll()`

### Timer Architecture

| Timer | Period | Context | Purpose |
| --- | --- | --- | --- |
| Display scan | 625 us | GPTimer ISR | `hu058_scan_step()`, 4 phases = 400 Hz |
| Button poll | 20 ms | esp_timer task | Debounce + hold detection |
| Status LED | 50-900 ms | esp_timer task | Flash phases, heartbeat pulse |
| NTP sync | 1 hour | System | `esp_sntp` automatic refresh |

The display scan deliberately uses a dedicated GPTimer rather than an
ISR-dispatched `esp_timer`. When an ISR-dispatched `esp_timer` fires on the
same tick as a task-dispatched one, `esp_timer` skips waking its task, and
every task timer period here is a multiple of 625 us, so they would never run.

## License

MIT

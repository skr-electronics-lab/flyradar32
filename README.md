# FlyRadar32

### Autonomous Desktop ADS-B Flight Radar and Avionics Ground Station

FlyRadar32 is a standalone, dedicated desktop aviation instrument powered by an ESP32 dual-core microcontroller and a 1.8-inch ST7735 SPI display. It aggregates real-time ADS-B transponder telemetry from global flight tracking networks, computes spatial trajectories, and renders a 60 FPS sweeping PPI (Plan Position Indicator) radar display without requiring an external computer or cloud intermediary.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: ESP32](https://img.shields.io/badge/Hardware-ESP32--WROOM--32-red.svg)](https://www.espressif.com/)
[![Framework: PlatformIO](https://img.shields.io/badge/Build-PlatformIO-orange.svg)](https://platformio.org/)
[![Display: ST7735](https://img.shields.io/badge/Display-ST7735%201.8%22%20SPI-purple.svg)](include/radar_display.h)
[![Support: Ko-fi](https://img.shields.io/badge/Support-Ko--fi-ff5e5b.svg)](https://ko-fi.com/skrelectronicslab)

---

## Table of Contents

- [System Architecture](#system-architecture)
- [Key Features](#key-features)
- [Hardware Specification and BOM](#hardware-specification-and-bom)
- [Wiring and Pin Assignment](#wiring-and-pin-assignment)
- [Aviation Telemetry and Mathematics](#aviation-telemetry-and-mathematics)
- [Enclosure and 3D Fabrication](#enclosure-and-3d-fabrication)
- [Firmware Build and Deployment](#firmware-build-and-deployment)
- [First Boot and Captive Portal](#first-boot-and-captive-portal)
- [Embedded REST API Specification](#embedded-rest-api-specification)
- [GitHub Repository Topics](#github-repository-topics)
- [Author and Support](#author-and-support)
- [License](#license)

---

## System Architecture

FlyRadar32 segregates workloads across both symmetric Xtensa LX6 CPU cores using FreeRTOS tasks to guarantee deterministic screen refresh rates regardless of network latency:

```
                  CORE 0 (Networking & Ingestion)              CORE 1 (UI & Rendering Engine)
             +---------------------------------------+    +------------------------------------+
             | WiFi / TCP / TLS Network Stack        |    | LVGL / Canvas Graphic Pipeline     |
             |                                       |    |                                    |
ADS-B APIs --+-> API Ingestion Engine                |    | 60 Hz Polar Sweep Engine           |
(HTTPS REST) |   - airplanes.live                    |    | Real-Time Target Vector Rendering  |
             |   - adsb.lol                          |    | Dynamic Range Rings (5-100 NM)     |
             |   - OpenSky Network (OAuth2)          |    +-----------------+------------------+
             |                                       |                      ^
             | Circuit Breaker & Health Monitor      |                      |
             +-------------------+-------------------+                      |
                                 |                                          |
                                 +-----> Thread-Safe Target Ring Buffer ----+
                                 |       (FreeRTOS Mutex Protected)         |
                                 v                                          v
             +---------------------------------------+    +------------------------------------+
             | AsyncWebServer (Port 80)              |    | Physical 3-Button State Machine    |
             | - Gzip Static LittleFS Assets         |    | - Debounce & Long-Press Detection  |
             | - JSON Telemetry REST Endpoints       |    | - Cursor Target Selection          |
             +---------------------------------------+    +------------------------------------+
```

- **Core 0 (Networking & Telemetry Pipeline):**
  Executes HTTP/TLS REST requests, handles JSON deserialization, runs the automated failover circuit breaker across ADS-B data providers, and serves the embedded web dashboard.
- **Core 1 (Real-Time Graphics Pipeline):**
  Drives the ST7735 display over high-speed SPI (27 MHz). Performs double-buffered polar coordinate projection, renders heading vectors and history trails, and samples tactile push buttons with state debounce logic.

---

## Key Features

- **Live Multi-Target ADS-B Tracking:**
  Decodes ICAO 24-bit transponder addresses, callsigns, barometric/geometric altitude, ground speed (knots), track heading (degrees), and vertical rate (feet/min).
- **Tri-Tier Provider Redundancy:**
  Integrates `airplanes.live`, `adsb.lol`, and `OpenSky Network` (supporting anonymous queries and OAuth2 client credentials). A circuit breaker detects consecutive network failures (3 strikes) and applies a 5-cycle exponential backoff cooldown to prevent API starvation.
- **Precision Polar Radar Graphics:**
  Simulates a marine/air-traffic Plan Position Indicator (PPI) with a sweeping phosphorescence sweep line, concentric nautical mile range rings (5, 10, 25, 50, 100 NM), cardinal compass headings, and target history breadcrumb trails.
- **Self-Contained Embedded Web Dashboard:**
  Hosts a zero-dependency responsive control panel directly from ESP32 flash (Gzip-compressed LittleFS assets). Provides live radar maps, target lists, coordinate calibration, Wi-Fi management, and display preferences.
- **Captive Portal Field Deployment:**
  Launches an autonomous setup Access Point (`FlyRadar32-XXXX`) if configured Wi-Fi credentials fail, automatically redirecting connected mobile or desktop browsers to the provisioning console.
- **Non-Volatile Configuration Memory:**
  All user preferences (home latitude/longitude, range limits, display brightness, screen themes, and API credentials) persist across power cycles in ESP32 Non-Volatile Storage (NVS).

---

## Hardware Specification and BOM

| Component | Specification | Quantity | Reference Source |
|-----------|---------------|----------|------------------|
| Microcontroller | ESP32-WROOM-32 (30-Pin DevKit, 4MB Flash) | 1 | Espressif Systems |
| Display | 1.8" TFT ST7735 (128x160 RGB, 4-Wire SPI) | 1 | Black-Tab / Red-Tab |
| User Input | 6x6x3.8mm Momentary Tactile Push Buttons | 3 | SPST Through-Hole |
| Enclosure | 3-Piece Precision FDM Chassis (PETG / PLA+) | 1 Set | [enclosure/](enclosure/) |
| Power Source | 5V USB-C or Micro-USB (500mA minimum) | 1 | Standard USB Port |

### Electrical Characteristics

- **Logic Level:** 3.3V DC (All GPIOs and SPI lines)
- **Nominal Operating Current:** 115 mA (Display at 80% brightness, Wi-Fi connected)
- **Peak Current:** 220 mA (During 802.11b/g/n RF packet transmission)

---

## Wiring and Pin Assignment

The ST7735 display and push buttons connect directly to the ESP32 GPIO headers. Internal pull-up resistors are utilized for the button inputs, eliminating external discrete components.

### Pinout Mapping

| Function | ESP32 GPIO | Peripheral Pin | Notes |
|----------|------------|----------------|-------|
| Display SCLK | GPIO 18 | SCK / CLK | Hardware VSPI Clock (27 MHz) |
| Display MOSI | GPIO 23 | SDA / DIN | Hardware VSPI Master Out Slave In |
| Display CS | GPIO 5 | CS | Chip Select (Active Low) |
| Display DC | GPIO 2 | DC / A0 | Data / Command Selection |
| Display RST | GPIO 4 | RES / RESET | Hardware Display Reset |
| Display BL | GPIO 15 | BL / LED | Backlight Anode (PWM Brightness Control) |
| Display VCC | 3V3 Rail | VCC | 3.3V Power Rail |
| Display GND | GND Rail | GND | Common Ground |
| Button UP | GPIO 25 | Pin 1 to GPIO, Pin 2 to GND | Target cursor traverse (Clockwise) |
| Button DOWN | GPIO 26 | Pin 1 to GPIO, Pin 2 to GND | Target cursor traverse (Counter-clockwise) |
| Button SELECT | GPIO 27 | Pin 1 to GPIO, Pin 2 to GND | Short-press: Detail / Long-press: Menu |

> **Critical Strapping Pin Notice:**
> Never connect buttons or load resistors to GPIO 0, GPIO 12, or GPIO 15 that force unexpected logic levels during power-on. Pulling GPIO 0 low during boot triggers ROM download mode instead of firmware execution.

### Wiring Diagram

```
         ESP32-WROOM-32                             ST7735 1.8" SPI TFT
      +-------------------+                         +-----------------+
      |               3V3 |------------------------>| VCC             |
      |               GND |------------------------>| GND             |
      |            GPIO18 |------------------------>| SCK (Clock)     |
      |            GPIO23 |------------------------>| SDA (MOSI)      |
      |             GPIO5 |------------------------>| CS (Chip Select)|
      |             GPIO2 |------------------------>| DC (Data/Cmd)   |
      |             GPIO4 |------------------------>| RES (Reset)     |
      |            GPIO15 |------------------------>| BL (Backlight)  |
      |                   |                         +-----------------+
      |            GPIO25 |----+
      |            GPIO26 |--+ |                    TACTILE BUTTONS
      |            GPIO27 |+ | |                    +-----------------+
      +-------------------++ | +------------------->| [UP]     -> GND |
                           | +--------------------->| [DOWN]   -> GND |
                           +----------------------->| [SELECT] -> GND |
                                                    +-----------------+
```

---

## Aviation Telemetry and Mathematics

### Coordinate Projection (Equirectangular to Polar)

To project geodetic WGS-84 coordinates $(\text{lat}_{\text{target}}, \text{lon}_{\text{target}})$ onto a localized polar radar display centered at observer coordinates $(\text{lat}_0, \text{lon}_0)$, the firmware evaluates local planar offsets in nautical miles:

$$\Delta y = (\text{lat}_{\text{target}} - \text{lat}_0) \times 60.0$$

$$\Delta x = (\text{lon}_{\text{target}} - \text{lon}_0) \times 60.0 \times \cos\left(\frac{\text{lat}_0 \times \pi}{180.0}\right)$$

The slant range $\rho$ (nautical miles) and true azimuth $\theta$ (radians) are derived as:

$$\rho = \sqrt{\Delta x^2 + \Delta y^2}$$

$$\theta = \text{atan2}(\Delta x, \Delta y)$$

### Screen Rasterization Mapping

Given a radar display radius $R_{\text{screen}} = 56\text{ pixels}$ and an active range setting $R_{\text{max}}$ (e.g., 25 NM), screen coordinates $(X_s, Y_s)$ relative to screen center $(X_0, Y_0)$ are calculated as:

$$r = \left(\frac{\rho}{R_{\text{max}}}\right) \times R_{\text{screen}}$$

$$X_s = X_0 + r \cdot \sin(\theta)$$

$$Y_s = Y_0 - r \cdot \cos(\theta)$$

Targets exceeding $R_{\text{max}}$ are clipped from the primary PPI sweep and placed in the peripheral target queue.

---

## Enclosure and 3D Fabrication

The console chassis is specifically optimized for additive manufacturing (FDM), featuring an ergonomic 15-degree instrument slant, internally reinforced snap-latch joints, zero exterior screw fasteners, and anti-fallout button actuators.

All 3D models and print documentation are located in the [enclosure/](enclosure/) directory:

- `enclosure/flyradar32_shell.stl`: Base chassis with integrated PCB cradle and USB port collar.
- `enclosure/flyradar32_lid.stl`: 15-degree angled faceplate with flush display bezel and snap catches.
- `enclosure/flyradar32_button_caps.stl`: Set of 3 tactile switch keycaps with wide retention flanges.
- `enclosure/FlyRadar32_Enclosure.FCStd`: Master parametric CAD model (FreeCAD).

### Recommended Slicer Settings

- **Layer Height:** 0.20 mm (0.16 mm for keycaps)
- **Wall Perimeters:** 3 walls minimum (1.2 mm solid perimeter)
- **Infill:** 20% Gyroid or Grid
- **Material:** PETG (recommended for thermal and snap elasticity) or PLA+
- **Support Structures:** None required (All geometries engineered with self-supporting 45-degree overhangs)

Refer to [enclosure/README.md](enclosure/README.md) for full mechanical dimensions, tolerances, and assembly steps.

---

## Firmware Build and Deployment

### Development Prerequisites

- [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation/index.html) or [VS Code with PlatformIO IDE Extension](https://platformio.org/install/ide?install=vscode)
- Git

### Build Pipeline

The firmware build script ([scripts/compress_web.py](scripts/compress_web.py)) automatically compresses HTML, CSS, and JavaScript assets from [data/](data/) into Gzip byte arrays inside `include/web_assets_gz.h` prior to compilation.

```bash
# Clone the repository
git clone https://github.com/skrelectronicslab/flyradar32.git
cd flyradar32

# Compile firmware binary
pio run

# Flash firmware over USB
pio run --target upload

# Launch serial telemetry monitor (115200 baud)
pio run --target monitor
```

---

## First Boot and Captive Portal

1. **Initial Power-On:**
   Upon first startup without configured Wi-Fi credentials, the device initializes an Access Point:
   - **SSID:** `FlyRadar32-XXXX` (where `XXXX` is derived from the ESP32 MAC address)
   - **Default Passphrase:** `radar1234`
2. **Configuration Handshake:**
   Connect a computer or smartphone to the Access Point. A captive portal redirect automatically opens `http://192.168.4.1`.
3. **Provisioning Settings:**
   - **Wi-Fi Tab:** Scan nearby SSIDs, input your local network passphrase, and save.
   - **Location Tab:** Input your observer latitude and longitude (supports direct decimal coordinate paste or browser geolocation).
   - **Display & Provider Tab:** Configure radar refresh rate (5s to 60s), default range scale, and API provider order.
4. **Autonomous Operation:**
   The device transitions to station mode, displays its assigned local network IP address on the screen, and immediately begins live radar tracking.

---

## Embedded REST API Specification

FlyRadar32 exposes a lightweight HTTP REST API for integration into Home Assistant, Node-RED, or custom telemetry dashboards:

### Endpoints

| Method | Endpoint | Description | Sample Output / Payload |
|--------|----------|-------------|-------------------------|
| `GET` | `/api/status` | Current radar telemetry and aircraft list | `{"uptime":3600,"wifi":"OK","aircraft_count":12,"targets":[...]}` |
| `GET` | `/api/config` | Read active system parameters | `{"lat":37.7749,"lon":-122.4194,"range":25,"theme":0}` |
| `POST` | `/api/config` | Update system parameters | `{"lat":51.5074,"lon":-0.1278,"range":50}` |
| `GET` | `/api/wifi/scan`| Scan for 2.4GHz Wi-Fi networks | `[{"ssid":"HomeNet","rssi":-58,"secure":true}]` |
| `POST` | `/api/wifi/connect`| Save Wi-Fi credentials and connect | `{"ssid":"HomeNet","password":"secretpassword"}` |
| `POST` | `/api/restart` | Issue software reboot | `{"status":"restarting"}` |

---

## GitHub Repository Topics

When configuring this repository on GitHub, add the following tags under **Repository Details -> Topics** for optimal indexing across embedded systems, aviation, and open-source communities:

```
esp32, ads-b, flight-radar, avionics, radar-display, st7735, freertos, platformio, embedded-systems, cplusplus, 3d-printing, freecad, opensky-network, iot, open-source-hardware
```

---

## Author and Support

FlyRadar32 is designed and developed by **Raihan (SK Raihan)**.

- **Organization:** SKR Electronics Lab / SKR Projects Hub
- **YouTube:** [@skr_electronics_lab](https://youtube.com/@skr_electronics_lab)
- **Instagram:** [@skr_electronics_lab](https://instagram.com/skr_electronics_lab)
- **X (Twitter):** [@skrelectronics](https://twitter.com/skrelectronics)
- **Email:** `skrelectronicslab@gmail.com`

If this open-source hardware project assists your engineering workflows, telemetry setups, or aviation exploration, support ongoing development on Ko-fi:

<div align="center">

[![Support on Ko-fi](assets/kofi_button.svg)](https://ko-fi.com/skrelectronicslab)

</div>

---

## License

This project is licensed under the [MIT License](LICENSE). You are free to inspect, modify, fork, distribute, and manufacture hardware based on these specifications.

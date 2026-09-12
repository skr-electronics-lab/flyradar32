<div align="center">

# FlyRadar32

**A live ADS-B aircraft radar on your desk — ESP32 + 1.8" ST7735 TFT**

Live aircraft positions, callsigns, altitude & speed — rendered on a sweeping
radar display, no PC required.

[![PlatformIO](https://img.shields.io/badge/PlatformIO-FE7D37?style=for-the-badge&logo=platformio&logoColor=white)](https://platformio.org/)
[![ESP32](https://img.shields.io/badge/ESP32-E7352C?style=for-the-badge&logo=espressif&logoColor=white)](https://www.espressif.com/)
[![Arduino](https://img.shields.io/badge/Arduino-00979D?style=for-the-badge&logo=arduino&logoColor=white)](https://www.arduino.cc/)
[![License: MIT](https://img.shields.io/badge/License-MIT-5E6AD2?style=for-the-badge)](LICENSE)

[Features](#-features) · [Hardware](#-hardware) · [Build](#-build--flash) · [First Boot](#-first-boot) · [Web UI](#-web-ui) · [Roadmap](#-roadmap)

</div>

---

## Why

Airplanes fly over your head every day. Commercial sites show you a map on a
phone — FlyRadar32 puts the sky on a dedicated gadget instead: a physical
radar sweep on a 160×128 TFT, a 3-button interface, and a web dashboard
served from the device itself. It's a complete IoT build: firmware, captive
portal, REST API, and frontend, all on a $4 chip.

## ✨ Features

- **Live ADS-B tracking** — aircraft positions, callsign, altitude, speed,
  vertical rate, and distance from you, via free community APIs
- **Multi-provider with automatic failover** — airplanes.live and adsb.lol
  tried in your priority order; a provider that fails 3× gets a 5-cycle
  cooldown instead of hammering
- **Real radar display** — sweep animation, range rings, compass labels,
  altitude-based aircraft colors, and flight-history trails
- **On-device navigation** — 3 buttons (UP / DOWN / SELECT) walk the cursor
  between aircraft; SELECT opens full aircraft detail, long-press opens
  settings (range, brightness, theme, providers, factory reset…)
- **Self-hosted web UI** — the device serves a responsive dashboard
  (Wi-Fi setup, location, display & API config, live aircraft list) from
  LittleFS
- **Captive-portal Wi-Fi setup** — first boot starts an access point;
  phones auto-redirect to the setup page, zero code editing to deploy
- **Power-loss-safe settings** — everything persists to NVS (Preferences)
- **Customizable look** — themes, aircraft icons vs. dots, labels,
  sweep on/off, range rings, compass — live from either the device or the web

## 🧰 Hardware

| Part | Notes |
|------|-------|
| ESP32 DevKit (WROOM-32) | any classic ESP32 dev board |
| 1.8" ST7735 TFT (128×160) | black-tab variant, SPI |
| 3× momentary pushbuttons | to GND, internal pullups used |

**~$10 total** — no HATs, no soldering beyond 11 wires.

### Wiring

| Function | GPIO | | Function | GPIO |
|----------|------|-|----------|------|
| TFT SCLK | 18 | | Button UP | 25 |
| TFT MOSI (SDA) | 23 | | Button DOWN | 26 |
| TFT CS | 5 | | Button SELECT | 27 |
| TFT RST (RES) | 4 | | | |
| TFT DC | 2 | | | |
| TFT BL (LED) | 15 | | | |

> **GPIO0 warning:** never wire a button to GPIO0 — it's the boot strap pin;
> holding it low at power-on forces the download bootloader instead of your app.

```
                    ┌─────────────┐
        3V3 ────────┤VCC        BL├──────── 3V3 (backlight)
        GPIO18 ─────┤SCK       CS├──────── GPIO5
        GPIO23 ─────┤SDA      RES├──────── GPIO4
           GND ─────┤GND       DC├──────── GPIO2
                    │  ST7735    │
                    │  1.8" TFT  │   ┌─[BTN]─ GND   GPIO25 (UP)
                    └─────────────┘   ├─[BTN]─ GND   GPIO26 (DOWN)
                                      └─[BTN]─ GND   GPIO27 (SELECT)
```

## 🔨 Build & flash

This is a PlatformIO project.

```bash
pio run                 # compile
pio run -t upload       # flash firmware (web UI is embedded in the binary)
pio device monitor      # serial log @ 115200
```

The web dashboard is gzip-compressed into the firmware itself
(`scripts/compress_web.py` regenerates `include/web_assets_gz.h` on every
build), so a plain `pio run -t upload` ships everything at once.
`pio run -t uploadfs` (LittleFS) is only needed if you want to serve the
files from the filesystem instead.

## 🚀 First boot

1. **No stored Wi-Fi?** The device starts an access point: `FlyRadar32-XXXX`
   (password: `radar1234`, change `AP_PASSWORD` in `config.h` before
   deploying). If a saved network dies later, it re-hosts the AP and retries
   every 60 s.
2. **Connect** a phone or laptop to that AP — most phones auto-prompt
   "sign in to network" (captive portal implemented for Android/Apple/Windows
   connectivity-check URLs). Fallback: `http://192.168.4.1`.
3. **Wi-Fi tab** → Scan → tap a network → password → Connect. Status mirrors
   on the device screen and in the web UI.
4. The device now shows its IP on screen — **open it from any device**
   on the same network.
5. **Location tab** → paste `lat, lon` straight from Google Maps, or tap
   Auto-detect (IP).
6. **Display & API tab** → refresh interval, radar range, labels, theme,
   provider toggles.
7. **On-device:** UP/DOWN walks the radar cursor between aircraft;
   SELECT (short) = aircraft detail · SELECT (long) = settings menu.

## 🌐 Web UI

Served from the device itself (LittleFS, no cloud):

| Tab | What it does |
|-----|---------------|
| Status | live aircraft table: callsign, altitude, speed, distance |
| Wi-Fi | scan, join, signal strength, AP fallback |
| Location | `lat, lon` paste or IP auto-detect |
| Display & API | refresh interval, range, theme, sweep, provider priority |

The firmware exposes a small REST API (`ESPAsyncWebServer`) the frontend
talks to — the same endpoints will drive anything else you want to build
on top (Home Assistant, Node-RED, custom dashboards).

## 🗺️ How it works

```
 ┌─────────┐   HTTPS    ┌──────────────────┐   JSON    ┌─────────────┐
 │ ADS-B    │◄──────────┤ api_providers    │◄─────────│ airplanes.live
 │ web APIs │            │ (background task,│           │ adsb.lol
 └─────────┘            │  failover,       │          └─────────────┘
                        │  3-fail cooldown) │
                        └────────┬─────────┘
                                 │ aircraft[]
              ┌──────────────────┼──────────────────┐
              ▼                  ▼                   ▼
      ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
      │ radar_display │   │ main.cpp      │   │ webui         │
      │ double-buffer │   │ buttons,      │   │ REST API +    │
      │ canvas sweep  │   │ state machine │   │ static files  │
      └──────────────┘   └──────────────┘   └──────────────┘
```

Fetch runs on a background FreeRTOS task, rendering never blocks on the
network, and settings persist across power cycles via NVS.

## 📁 Project layout

```
platformio.ini        pins, flags, library pins (LVGL 8.3.x pinned —
                       8.4 changed dark-theme greys)
include/              headers for every module + config.h (all tunables)
scripts/
  compress_web.py     pre-build: gzips data/ into include/web_assets_gz.h
src/
  main.cpp            screen state machine, button routing, render loop
  storage.cpp         NVS-backed settings (Preferences)
  buttons.cpp         debounced short/long press for 3 buttons
  wifi_manager.cpp    AP fallback + captive portal + drop-detection reconnect
  webui.cpp           ESPAsyncWebServer REST API + embedded web UI
  api_providers.cpp   background fetch task, multi-provider failover, OAuth2
  radar_display.cpp   canvas-based double-buffered rendering + trails
data/                 web UI source (compiled into the firmware at build time)
  index.html, style.css, app.js
```

## ✈️ API providers

| Provider | Auth | Status |
|----------|------|--------|
| [airplanes.live](https://api.airplanes.live/v2/point/{lat}/{lon}/{radius_nm}) | none | ✅ working (HTTPS) |
| [adsb.lol](https://api.adsb.lol/v2/point/{lat}/{lon}/{radius_nm}) | none | ✅ working (HTTPS) |
| OpenSky Network | OAuth2 client-credentials (optional; works anonymously too) | ✅ working — paste credentials in the web UI |

Providers are tried in your configured priority order each cycle. A
provider failing 3× in a row is skipped for the next 5 cycles — the radar
never starves because one API is having a bad day.

## 🗺 Roadmap

- [ ] Sort radar cursor by bearing — UP/DOWN walks clockwise around the compass
- [ ] TLS certificate pinning (replace `setInsecure()`)
- [ ] Async Wi-Fi scan (`WiFi.scanNetworks(true)` + poll endpoint)
- [ ] NTP time sync (needed for real cert validation / timestamps)
- [ ] Weather overlay at your location
- [ ] Aircraft photos on the detail page

## ⚠️ Limitations (honest ones)

- **TLS validation is off** (`setInsecure()`) on all HTTPS clients — fine for
  a home gadget; pin real certificates before shipping to others.
- **No auth on the config API** — the web UI is trusted-LAN only; don't
  port-forward the device. (The old half-built PIN lock was removed.)
- **Default AP password** (`radar1234`) — change `AP_PASSWORD` in
  `config.h` before deploying.
- **Wi-Fi scan blocks** the request thread ~2-4 s (occasional settings
  action only; async scan is roadmap).
- Cursor walks aircraft in provider response order — bearing-sort is the
  cheap fix (roadmap #1).

## 📄 License

[MIT](LICENSE) — free to build, modify, and sell. If it lands you a job in
aviation, tell me the story.

---

<div align="center">

**Build video & full tutorial → [skrelectronicslab.com](https://www.skrelectronicslab.com)**

[![Website](https://img.shields.io/badge/Website-5E6AD2?style=for-the-badge)](https://www.skrelectronicslab.com)
[![YouTube](https://img.shields.io/badge/YouTube-FF0000?style=for-the-badge&logo=youtube&logoColor=white)](https://www.youtube.com/@skr_electronics_lab)

</div>

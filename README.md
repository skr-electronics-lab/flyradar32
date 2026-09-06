# ESP32 ADS-B Radar - Full Firmware

Multi-provider ADS-B/aircraft radar on an ESP32 + ST7735 display, with
3-button on-device navigation, a captive-portal Wi-Fi setup flow, and a
responsive web management UI served from the device itself.

## Hardware wiring

| Function        | GPIO |
|------------------|------|
| TFT SCLK         | 18   |
| TFT MOSI (SDA)   | 23   |
| TFT CS           | 5    |
| TFT RST (RES)    | 4    |
| TFT DC           | 2    |
| TFT BL (LED)     | 15   |
| Button UP        | 25   |
| Button DOWN      | 26   |
| Button SELECT    | 27   |

All buttons are wired to GND with `INPUT_PULLUP` used in firmware - no
external resistors needed. GPIO0 is deliberately **not** used for any
button: it's the ESP32's boot-mode strap pin, and holding it low at
power-on forces the download bootloader instead of your app.

## Project layout

```
platformio.ini
include/            headers for every module
src/
  main.cpp          screen state machine, button routing, render loop
  storage.cpp        NVS-backed settings (Preferences)
  buttons.cpp         debounced short/long press for 3 buttons
  wifi_manager.cpp    AP fallback + captive portal + non-blocking connect
  webui.cpp           ESPAsyncWebServer REST API + static file serving
  api_providers.cpp   background fetch task, multi-provider fallback, OAuth2
  radar_display.cpp   canvas-based double-buffered rendering
data/                files uploaded to LittleFS (the web UI)
  index.html, style.css, app.js
```

## Building

This is a PlatformIO project.

```
pio run                # compile
pio run -t upload      # flash firmware
pio run -t uploadfs    # flash the data/ folder to LittleFS (the web UI)
pio device monitor      # serial log
```

Both `upload` and `uploadfs` are required on first flash - the firmware
and the web UI are flashed separately.

## First boot flow

1. On first boot (no stored Wi-Fi credentials), the device starts an
   access point named `FlyRadar32-XXXX` (password `radar1234`, see
   `AP_PASSWORD` in `config.h` - change this before deploying).
   If a stored network becomes unreachable later, the device re-hosts
   the same AP and retries the stored credentials every 60 s
   (paused while a client is connected to the portal).
2. Connect a phone or laptop to that AP. Most phones will prompt to
   "sign in to network" automatically (captive portal redirect is
   implemented for the common Android/Apple/Windows connectivity-check
   URLs); otherwise open `http://192.168.4.1` manually.
3. On the **Wi-Fi** tab: tap **Scan**, tap a network to autofill the
   SSID, enter the password, tap **Connect**. The device shows
   connecting/success/failure both on its own screen and in the web UI's
   status card. On failure it stays in AP mode so you can retry.
4. Once connected, the device screen shows its new IP address. Open that
   IP from any device on the same network to keep managing it.
5. On the **Location** tab, paste coordinates copied directly from Google
   Maps (format `lat, lon`) or tap **Auto-detect (IP)** for a rough fix.
6. On the **Display & API** tab you can set the refresh interval, radar
   range, labels, icon, sweep animation, theme and customization toggles.
   The two data providers (`airplanes.live`, `adsb.lol`) can be toggled
   on-device via Settings -> Data Providers.
7. On-device: UP/DOWN move the radar cursor between tracked aircraft,
   SELECT (short) opens full detail for the highlighted plane, SELECT
   (long) opens the Settings menu (range, brightness, labels, icon, theme,
   compass, range rings, trails, data providers, aircraft list, force
   refresh, system info, factory reset).

## API providers implemented

- **airplanes.live** - `GET https://api.airplanes.live/v2/point/{lat}/{lon}/{radius_nm}`, no auth.
- **adsb.lol** - same schema/endpoint shape as airplanes.live (ADS-B Exchange v2-compatible), no auth: `https://api.adsb.lol/v2/point/{lat}/{lon}/{radius_nm}`.
- OpenSky Network - **not implemented yet** (roadmap: OAuth2 client-credentials flow, required since March 2026; the old basic-auth method is retired).

The background task tries enabled providers in your configured priority
order each cycle; a provider that fails 3 times in a row is skipped for
the next 5 cycles (simple backoff) rather than retried every time.

## Known limitations / good next steps

- **TLS certificate validation is disabled** (`setInsecure()`) on all
  HTTPS clients for simplicity. For a deployed device, pin the actual
  server certificates instead.
- **Wi-Fi scan is synchronous** (blocks ~2-4s while scanning). Fine for
  an occasional settings action; an async scan (`WiFi.scanNetworks(true)`)
  with a poll endpoint would avoid tying up a request thread.
- Cursor selection on the radar walks the aircraft array in whatever
  order the provider returned it. Sorting by bearing before display would
  make UP/DOWN feel like walking clockwise around the compass - a cheap,
  worthwhile follow-up.
- No NTP time sync is performed. Not required for anything currently
  implemented (OAuth2 expiry uses relative `millis()` timing), but would
  be needed if you later want real certificate validation or to log
  timestamps.
- The AP setup password (`radar1234`) and the absence of any transport
  encryption on the plain-HTTP config server are fine for a home/hobby
  deployment; harden both before shipping this to anyone else.

# claude-knock

Physical notification system: Claude Code hook -> MQTT -> ESP32-C3 -> solenoid.

## Project Structure

- `device/firmware/` — ESP-IDF v6.0 project targeting ESP32-C3. Created with `idf.py create-project`, target set with `idf.py set-target esp32c3`.
- `server/` — Docker/Podman compose for Mosquitto MQTT broker with ACL and optional Traefik.
- `scripts/` — Flash scripts, hook install/uninstall scripts.
  - `install.sh`, `install.ps1` — Interactive hook installers for `~/.claude/settings.json`.
  - `uninstall.sh`, `uninstall.ps1` — Hook removal scripts.
  - `flash.sh`, `flash.ps1`, `flash.bat` — Firmware flash scripts bundled in releases.

## Firmware Modules

All source is in `device/firmware/main/`:

- `main.c` — Entry point. Loads NVS config; branches to AP provisioning mode or normal WiFi+MQTT mode.
- `config.c/h` — NVS read/write/clear. Namespace `ck_config`. Auto-generates MQTT username from MAC.
- `wifi.c/h` — AP mode (open, DHCP option 114 captive portal) and STA mode (retry with fallback to AP).
- `portal.c/h` — HTTP server for captive portal. Routes: `GET /`, `GET /status`, `POST /save`. 404 redirects to `/`.
- `portal.html` — Embedded config page (EMBED_FILES). Self-contained HTML/CSS/JS, dark theme.
- `mqtt.c/h` — MQTT client. Parses `knock:N` payload, calls `solenoid_pulse(N)`. `__reset__` triggers factory reset.
- `solenoid.c/h` — GPIO 5 pulse, GPIO 8 LED status, GPIO 9 reset button (3s hold).

## Build

```sh
cd device/firmware
source /path/to/esp-idf/export.sh
idf.py build
```

## Key Design Decisions

- `dns_server` component is copied from ESP-IDF captive portal example (`examples/protocols/http_server/captive_portal/components/dns_server/`).
- MQTT uses managed component `espressif/mqtt ^1.0.0` declared in `idf_component.yml`.
- Install scripts resolve symlinks before writing to avoid breaking dotfile setups. They identify claude-knock hooks by matching `mosquitto_pub.*knock:` in the command string.
- Two hook events: `Stop` sends `knock:2`, `Notification` sends `knock:3`.

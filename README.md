# claude-knock

Physical notification when Claude Code finishes a response.

A Claude Code hook publishes an MQTT message, and an ESP32-C3 actuates a solenoid via MOSFET. Two events produce distinct knock patterns so you can tell them apart:

- **Stop** (turn complete) — 2 knocks
- **Notification** (desktop alert) — 3 knocks

## Usage

### Hook Installation

```sh
# macOS / Linux
bash <(curl -fsSL https://raw.githubusercontent.com/luftaquila/claude-knock/main/scripts/install.sh)

# Windows (PowerShell)
irm https://raw.githubusercontent.com/luftaquila/claude-knock/main/scripts/install.ps1 | iex
```

Prompts for MQTT host, port, topic, username, and password. Installs `mosquitto_pub` via the platform package manager if missing.

To remove:

```sh
bash <(curl -fsSL https://raw.githubusercontent.com/luftaquila/claude-knock/main/scripts/uninstall.sh)
irm https://raw.githubusercontent.com/luftaquila/claude-knock/main/scripts/uninstall.ps1 | iex
```

### MQTT Server

Any MQTT broker works. Point the hook and the device to the same broker and topic. The install script and the device captive portal each ask for the connection details.

<details>
<summary>Self Hosting</summary>

A Mosquitto compose file is included. Works with Docker and Podman.

```sh
cd server

# Create users
docker compose run --rm mosquitto mosquitto_passwd -b /mosquitto/config/password.txt claude-knock-hook <password>
docker compose run --rm mosquitto mosquitto_passwd -b /mosquitto/config/password.txt claude-knock-<mac6> <password>

docker compose up -d
```

The device MAC suffix is shown on the captive portal during setup.

**Traefik integration:**

- **Bundled** — `docker compose --profile traefik up -d`
- **External** — `MQTT_DOMAIN=mqtt.example.com TRAEFIK_NETWORK=traefik docker compose --profile external-traefik up -d`

</details>

### Firmware

Download the latest release from [Releases](https://github.com/luftaquila/claude-knock/releases), extract, and run the flash script:

```sh
# macOS / Linux
./flash.sh

# Windows
flash.bat
```

Requires Python 3. The script installs `esptool` automatically.

On first boot, the device starts AP **claude-knock**. Connect and configure WiFi + MQTT through the captive portal. The MQTT username is auto-generated from the device MAC address.

> [!NOTE]
> Hold the BOOT button (GPIO 9) for 3 seconds to factory reset.

<details>
<summary>Development</summary>

Requires [ESP-IDF v6.0](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32c3/get-started/).

```sh
cd device/firmware
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

</details>

## Hardware

| GPIO | Function |
|------|----------|
| 5 | MOSFET gate (solenoid) |
| 8 | Status LED |
| 9 | Factory reset button |

N-channel MOSFET gate to GPIO 5 with 10k pull-down. Flyback diode across the solenoid.

**LED:**
- Slow blink — AP mode
- Fast blink — connecting
- Solid — normal operation

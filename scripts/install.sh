#!/usr/bin/env bash
set -euo pipefail

SETTINGS_LINK="${HOME}/.claude/settings.json"

# Resolve symlink to actual file
if [ -L "$SETTINGS_LINK" ]; then
  SETTINGS_FILE="$(readlink -f "$SETTINGS_LINK")"
else
  SETTINGS_FILE="$SETTINGS_LINK"
fi

echo "=== claude-knock hook installer ==="
echo

# Detect package manager
detect_pm() {
  if command -v brew &>/dev/null; then echo "brew"
  elif command -v apt-get &>/dev/null; then echo "apt"
  elif command -v dnf &>/dev/null; then echo "dnf"
  elif command -v pacman &>/dev/null; then echo "pacman"
  else echo ""; fi
}

# Install a package if missing, asking the user first
ensure_cmd() {
  local cmd="$1" pkg_brew="$2" pkg_apt="$3" pkg_dnf="$4" pkg_pacman="$5"

  if command -v "$cmd" &>/dev/null; then return 0; fi

  local pm
  pm=$(detect_pm)
  if [ -z "$pm" ]; then
    echo "Error: $cmd not found and no supported package manager detected."
    exit 1
  fi

  local pkg
  case "$pm" in
    brew)   pkg="$pkg_brew" ;;
    apt)    pkg="$pkg_apt" ;;
    dnf)    pkg="$pkg_dnf" ;;
    pacman) pkg="$pkg_pacman" ;;
  esac

  read -rp "$cmd not found. Install $pkg via $pm? [Y/n] " ans
  if [[ "$ans" =~ ^[Nn]$ ]]; then
    echo "Aborted."
    exit 1
  fi

  case "$pm" in
    brew)   brew install "$pkg" ;;
    apt)    sudo apt-get install -y "$pkg" ;;
    dnf)    sudo dnf install -y "$pkg" ;;
    pacman) sudo pacman -S --noconfirm "$pkg" ;;
  esac
}

ensure_cmd mosquitto_pub mosquitto mosquitto-clients mosquitto mosquitto
ensure_cmd jq jq jq jq jq

# Check settings.json
if [ ! -f "$SETTINGS_FILE" ]; then
  echo "Error: $SETTINGS_FILE not found."
  echo "Is Claude Code installed?"
  exit 1
fi

echo "Configure MQTT connection for claude-knock hooks."
echo

read -rp "MQTT host [localhost]: " mqtt_host
mqtt_host="${mqtt_host:-localhost}"

read -rp "MQTT port [1883]: " mqtt_port
mqtt_port="${mqtt_port:-1883}"

read -rp "MQTT topic [claude-knock]: " mqtt_topic
mqtt_topic="${mqtt_topic:-claude-knock}"

read -rp "MQTT username [claude-knock-hook]: " mqtt_user
mqtt_user="${mqtt_user:-claude-knock-hook}"

read -rsp "MQTT password: " mqtt_pass
echo
if [ -z "$mqtt_pass" ]; then
  echo "Error: MQTT password is required."
  exit 1
fi

echo
echo "--- Configuration ---"
echo "  Host:     $mqtt_host"
echo "  Port:     $mqtt_port"
echo "  Topic:    $mqtt_topic"
echo "  Username: $mqtt_user"
echo "  Password: ****"
echo

read -rp "Apply to $SETTINGS_FILE? [Y/n] " confirm
if [[ "$confirm" =~ ^[Nn]$ ]]; then
  echo "Aborted."
  exit 0
fi

# Escape single quotes for safe shell embedding
shell_escape() {
  printf '%s' "$1" | sed "s/'/'\\\\''/g"
}

# Build mosquitto_pub commands
stop_cmd="mosquitto_pub -h '$(shell_escape "$mqtt_host")' -p '$(shell_escape "$mqtt_port")' -t '$(shell_escape "$mqtt_topic")' -u '$(shell_escape "$mqtt_user")' -P '$(shell_escape "$mqtt_pass")' -m knock:2"
notif_cmd="mosquitto_pub -h '$(shell_escape "$mqtt_host")' -p '$(shell_escape "$mqtt_port")' -t '$(shell_escape "$mqtt_topic")' -u '$(shell_escape "$mqtt_user")' -P '$(shell_escape "$mqtt_pass")' -m knock:3"

# Remove existing claude-knock entries, then append new ones
jq --arg stop_cmd "$stop_cmd" --arg notif_cmd "$notif_cmd" '
  # Filter out existing claude-knock entries (match mosquitto_pub + knock: to avoid false positives)
  ((.hooks.Stop // []) | map(select(.hooks | all(.command | test("mosquitto_pub.*knock:") | not)))) as $stop_rest |
  ((.hooks.Notification // []) | map(select(.hooks | all(.command | test("mosquitto_pub.*knock:") | not)))) as $notif_rest |
  # Append new entries
  .hooks.Stop = $stop_rest + [
    {
      "matcher": "",
      "hooks": [
        {
          "type": "command",
          "command": $stop_cmd,
          "timeout": 5
        }
      ]
    }
  ]
  | .hooks.Notification = $notif_rest + [
    {
      "matcher": "",
      "hooks": [
        {
          "type": "command",
          "command": $notif_cmd,
          "timeout": 5
        }
      ]
    }
  ]
' "$SETTINGS_FILE" > "${SETTINGS_FILE}.tmp" && mv "${SETTINGS_FILE}.tmp" "$SETTINGS_FILE"

echo
echo "Done! Hooks added to $SETTINGS_FILE"
echo
echo "  Stop         -> knock:2 (2 pulses)"
echo "  Notification -> knock:3 (3 pulses)"
echo
echo "To uninstall, run: $(dirname "$0")/uninstall.sh"

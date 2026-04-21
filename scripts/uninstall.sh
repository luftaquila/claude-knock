#!/usr/bin/env bash
set -euo pipefail

SETTINGS_LINK="${HOME}/.claude/settings.json"

# Resolve symlink to actual file
if [ -L "$SETTINGS_LINK" ]; then
  SETTINGS_FILE="$(readlink -f "$SETTINGS_LINK")"
else
  SETTINGS_FILE="$SETTINGS_LINK"
fi

echo "=== claude-knock hook uninstaller ==="
echo

if [ ! -f "$SETTINGS_FILE" ]; then
  echo "Error: $SETTINGS_FILE not found."
  exit 1
fi

if ! command -v jq &>/dev/null; then
  echo "Error: jq not found."
  exit 1
fi

# Check if claude-knock hooks exist
if ! jq -e '.hooks.Stop[]?.hooks[]? | select(.command | test("mosquitto_pub.*knock:"))' "$SETTINGS_FILE" &>/dev/null && \
   ! jq -e '.hooks.Notification[]?.hooks[]? | select(.command | test("mosquitto_pub.*knock:"))' "$SETTINGS_FILE" &>/dev/null; then
  echo "No claude-knock hooks found."
  exit 0
fi

read -rp "Remove claude-knock hooks from $SETTINGS_FILE? [Y/n] " confirm
if [[ "$confirm" =~ ^[Nn]$ ]]; then
  echo "Aborted."
  exit 0
fi

# Remove only claude-knock entries, keep other hooks intact
jq '
  (if .hooks.Stop then .hooks.Stop |= map(select(.hooks | all(.command | test("mosquitto_pub.*knock:") | not))) else . end) |
  (if .hooks.Notification then .hooks.Notification |= map(select(.hooks | all(.command | test("mosquitto_pub.*knock:") | not))) else . end) |
  if .hooks.Stop == [] then del(.hooks.Stop) else . end |
  if .hooks.Notification == [] then del(.hooks.Notification) else . end |
  if .hooks == {} then del(.hooks) else . end
' "$SETTINGS_FILE" > "${SETTINGS_FILE}.tmp" && mv "${SETTINGS_FILE}.tmp" "$SETTINGS_FILE"

echo "Done! claude-knock hooks removed."

#Requires -Version 5.1
$ErrorActionPreference = "Stop"

$SettingsLink = Join-Path $env:USERPROFILE ".claude\settings.json"

# Resolve symlink/junction to actual file
$item = Get-Item $SettingsLink -ErrorAction SilentlyContinue
if ($item -and $item.Target) {
    $SettingsFile = $item.Target
} else {
    $SettingsFile = $SettingsLink
}

Write-Host "=== claude-knock hook installer ===" -ForegroundColor Cyan
Write-Host

# Detect package manager
function Get-PackageManager {
    if (Get-Command winget -ErrorAction SilentlyContinue) { return "winget" }
    if (Get-Command choco -ErrorAction SilentlyContinue) { return "choco" }
    if (Get-Command scoop -ErrorAction SilentlyContinue) { return "scoop" }
    return ""
}

# Install a package if missing
function Ensure-Command {
    param($Cmd, $WingetPkg, $ChocoPkg, $ScoopPkg)

    if (Get-Command $Cmd -ErrorAction SilentlyContinue) { return }

    $pm = Get-PackageManager
    if (-not $pm) {
        Write-Host "Error: $Cmd not found and no supported package manager detected." -ForegroundColor Red
        exit 1
    }

    $pkg = switch ($pm) {
        "winget" { $WingetPkg }
        "choco"  { $ChocoPkg }
        "scoop"  { $ScoopPkg }
    }

    $ans = Read-Host "$Cmd not found. Install $pkg via $pm? [Y/n]"
    if ($ans -match '^[Nn]$') {
        Write-Host "Aborted."
        exit 1
    }

    switch ($pm) {
        "winget" { winget install --accept-package-agreements --accept-source-agreements $pkg }
        "choco"  { choco install -y $pkg }
        "scoop"  { scoop install $pkg }
    }

    # Refresh PATH
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("Path", "User")
}

Ensure-Command "mosquitto_pub" "EclipseFoundation.Mosquitto" "mosquitto" "mosquitto"

# Check settings.json
if (-not (Test-Path $SettingsFile)) {
    Write-Host "Error: $SettingsFile not found." -ForegroundColor Red
    Write-Host "Is Claude Code installed?"
    exit 1
}

# Read current settings
$settings = Get-Content -Raw $SettingsFile | ConvertFrom-Json

# Helper: check if a hook array contains a claude-knock entry
function Has-CkHook($hookArray) {
    if (-not $hookArray) { return $false }
    foreach ($entry in $hookArray) {
        foreach ($h in $entry.hooks) {
            if ($h.command -match "mosquitto_pub.*knock:") { return $true }
        }
    }
    return $false
}

# Helper: filter out claude-knock entries from a hook array
function Remove-CkEntries($hookArray) {
    if (-not $hookArray) { return @() }
    $result = @()
    foreach ($entry in $hookArray) {
        $isCk = $false
        foreach ($h in $entry.hooks) {
            if ($h.command -match "mosquitto_pub.*knock:") { $isCk = $true; break }
        }
        if (-not $isCk) { $result += $entry }
    }
    return $result
}

Write-Host "Configure MQTT connection for claude-knock hooks."
Write-Host

$mqtt_host = Read-Host "MQTT host"
if ([string]::IsNullOrWhiteSpace($mqtt_host)) {
    Write-Host "Error: MQTT host is required." -ForegroundColor Red
    exit 1
}

$mqtt_port = Read-Host "MQTT port [443]"
if ([string]::IsNullOrWhiteSpace($mqtt_port)) { $mqtt_port = "443" }

$mqtt_topic = Read-Host "MQTT topic [claude-knock]"
if ([string]::IsNullOrWhiteSpace($mqtt_topic)) { $mqtt_topic = "claude-knock" }

$mqtt_user = Read-Host "MQTT username [claude-knock-hook]"
if ([string]::IsNullOrWhiteSpace($mqtt_user)) { $mqtt_user = "claude-knock-hook" }

$mqtt_secure = Read-Host "MQTT password" -AsSecureString
$mqtt_pass = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto(
    [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($mqtt_secure))

if ([string]::IsNullOrWhiteSpace($mqtt_pass)) {
    Write-Host "Error: MQTT password is required." -ForegroundColor Red
    exit 1
}

# Use TLS for any non-1883 port
if ($mqtt_port -eq "1883") {
    $tls_desc = "no (plain MQTT)"
} else {
    $tls_desc = "yes (--tls-use-os-certs)"
}

Write-Host
Write-Host "--- Configuration ---"
Write-Host "  Host:     $mqtt_host"
Write-Host "  Port:     $mqtt_port"
Write-Host "  Topic:    $mqtt_topic"
Write-Host "  Username: $mqtt_user"
Write-Host "  Password: ****"
Write-Host "  TLS:      $tls_desc"
Write-Host

$confirm = Read-Host "Apply to $SettingsFile? [Y/n]"
if ($confirm -match '^[Nn]$') {
    Write-Host "Aborted."
    exit 0
}

# Escape single quotes for safe shell embedding
function Escape-ShellQuote($s) { $s -replace "'", "'\''"}

# Build mosquitto_pub commands (TLS for any non-1883 port)
$e_host  = Escape-ShellQuote $mqtt_host
$e_port  = Escape-ShellQuote $mqtt_port
$e_topic = Escape-ShellQuote $mqtt_topic
$e_user  = Escape-ShellQuote $mqtt_user
$e_pass  = Escape-ShellQuote $mqtt_pass
if ($mqtt_port -eq "1883") {
    $tls_args = ""
} else {
    $tls_args = " --tls-use-os-certs"
}
$stop_cmd = "mosquitto_pub -h '$e_host' -p '$e_port'$tls_args -t '$e_topic' -u '$e_user' -P '$e_pass' -m knock:2"
$notif_cmd = "mosquitto_pub -h '$e_host' -p '$e_port'$tls_args -t '$e_topic' -u '$e_user' -P '$e_pass' -m knock:3"

# Ensure hooks property exists
if (-not $settings.hooks) {
    $settings | Add-Member -NotePropertyName hooks -NotePropertyValue ([PSCustomObject]@{})
}

# Build new claude-knock entries
$stopEntry = [PSCustomObject]@{
    matcher = ""
    hooks = @(
        [PSCustomObject]@{
            type    = "command"
            command = $stop_cmd
            timeout = 5
        }
    )
}

$notifEntry = [PSCustomObject]@{
    matcher = ""
    hooks = @(
        [PSCustomObject]@{
            type    = "command"
            command = $notif_cmd
            timeout = 5
        }
    )
}

# Remove existing claude-knock entries, then append new ones
foreach ($event in @("Stop", "Notification")) {
    if ($settings.hooks.PSObject.Properties[$event]) {
        $settings.hooks.$event = @(Remove-CkEntries $settings.hooks.$event)
    }
}

# Append new entries (preserving other hooks)
if ($settings.hooks.PSObject.Properties["Stop"]) {
    $settings.hooks.Stop = @($settings.hooks.Stop) + $stopEntry
} else {
    $settings.hooks | Add-Member -NotePropertyName Stop -NotePropertyValue @($stopEntry)
}

if ($settings.hooks.PSObject.Properties["Notification"]) {
    $settings.hooks.Notification = @($settings.hooks.Notification) + $notifEntry
} else {
    $settings.hooks | Add-Member -NotePropertyName Notification -NotePropertyValue @($notifEntry)
}

# Write back (atomic: write to temp then rename)
$tmpFile = "$SettingsFile.tmp"
$settings | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 $tmpFile
Move-Item -Force $tmpFile $SettingsFile

Write-Host
Write-Host "Done! Hooks added to $SettingsFile" -ForegroundColor Green
Write-Host
Write-Host "  Stop         -> knock:2 (2 pulses)"
Write-Host "  Notification -> knock:3 (3 pulses)"
Write-Host
Write-Host "To uninstall, run: uninstall.ps1"

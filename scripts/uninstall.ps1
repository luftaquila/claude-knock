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

Write-Host "=== claude-knock hook uninstaller ===" -ForegroundColor Cyan
Write-Host

if (-not (Test-Path $SettingsFile)) {
    Write-Host "Error: $SettingsFile not found." -ForegroundColor Red
    exit 1
}

$settings = Get-Content -Raw $SettingsFile | ConvertFrom-Json

# Check if claude-knock hooks exist
function Has-CkHook($hookArray) {
    if (-not $hookArray) { return $false }
    foreach ($entry in $hookArray) {
        foreach ($h in $entry.hooks) {
            if ($h.command -match "mosquitto_pub.*knock:") { return $true }
        }
    }
    return $false
}

if (-not (Has-CkHook $settings.hooks.Stop) -and -not (Has-CkHook $settings.hooks.Notification)) {
    Write-Host "No claude-knock hooks found."
    exit 0
}

$confirm = Read-Host "Remove claude-knock hooks from $SettingsFile? [Y/n]"
if ($confirm -match '^[Nn]$') {
    Write-Host "Aborted."
    exit 0
}

# Remove only claude-knock entries, keep others
foreach ($event in @("Stop", "Notification")) {
    if (-not $settings.hooks.PSObject.Properties[$event]) { continue }

    $kept = @()
    foreach ($entry in $settings.hooks.$event) {
        $isCk = $false
        foreach ($h in $entry.hooks) {
            if ($h.command -match "mosquitto_pub.*knock:") { $isCk = $true; break }
        }
        if (-not $isCk) { $kept += $entry }
    }

    if ($kept.Count -eq 0) {
        $settings.hooks.PSObject.Properties.Remove($event)
    } else {
        $settings.hooks.$event = $kept
    }
}

# Remove hooks object if empty
if (@($settings.hooks.PSObject.Properties).Count -eq 0) {
    $settings.PSObject.Properties.Remove("hooks")
}

$tmpFile = "$SettingsFile.tmp"
$settings | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 $tmpFile
Move-Item -Force $tmpFile $SettingsFile

Write-Host "Done! claude-knock hooks removed." -ForegroundColor Green

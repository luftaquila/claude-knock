$ErrorActionPreference = "Stop"

Set-Location -Path "firmware"

Write-Host "Installing esptool..."
python -m pip install esptool

Write-Host "Flashing firmware..."
python -m esptool --chip esp32c3 --before default-reset --after hard-reset write-flash "@flash_args"

Write-Host "Done."

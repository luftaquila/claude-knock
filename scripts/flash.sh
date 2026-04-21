#!/bin/bash

set -e

cd "firmware" || exit 1

echo "Installing esptool..."
python3 -m pip install esptool

echo "Flashing firmware..."
esptool.py --chip esp32c3 --before default-reset --after hard-reset write-flash "@flash_args"

echo "Done."

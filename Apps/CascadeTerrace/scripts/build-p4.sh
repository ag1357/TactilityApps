#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Source ESP-IDF 6.1 export.sh first}"
: "${TACTILITY_SDK_PATH:?Set to the SDK directory whose basename is TactilitySDK}"
# Build only the real external ELF, not an unrelated standalone firmware image.
idf.py -B build-p4-native -DIDF_TARGET=esp32p4 reconfigure
idf.py -B build-p4-native elf
python3 scripts/package-p4.py

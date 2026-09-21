#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $# -ne 1 ]]; then echo 'Usage: scripts/deploy-cm5.sh P4_HOST' >&2; exit 2; fi
# Enable Tactility's development server on the device. Existing firmware is not flashed.
# Prefer the published native artifact; no compiler needed on the CM5 for a
# hardware qualification run. CASCADE_REBUILD=1 explicitly rebuilds from source.
if [[ "${CASCADE_REBUILD:-0}" == 1 ]]; then
    scripts/build-p4.sh
elif [[ ! -f build-p4-native/cascadeterrace.app.elf && -f research/releases/cascadeterrace.app.elf ]]; then
    mkdir -p build build-p4-native
    cp research/releases/cascadeterrace.app.elf build-p4-native/cascadeterrace.app.elf
elif [[ ! -f build-p4-native/cascadeterrace.app.elf ]]; then
    scripts/build-p4.sh
fi
package_args=(--mode "${CASCADE_MODE:-0}")
if [[ "${CASCADE_QUALIFY:-0}" == 1 ]]; then package_args+=(--qualification); fi
python3 scripts/package-p4.py "${package_args[@]}"
python3 ../../tactility.py install --path "$PWD" --host "$1"
python3 ../../tactility.py run --path "$PWD" --host "$1"

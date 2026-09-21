#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $# -ne 1 ]]; then echo 'Usage: scripts/deploy-cm5.sh P4_HOST' >&2; exit 2; fi
# Enable Tactility's development server on the device. Existing firmware is not flashed.
[[ -f build/ag1357.cascadeterrace.app ]] || scripts/build-p4.sh
if [[ "${CASCADE_QUALIFY:-0}" == 1 ]]; then python3 scripts/package-p4.py --qualification; fi
python3 ../../tactility.py install --path "$PWD" --host "$1"
python3 ../../tactility.py run --path "$PWD" --host "$1"

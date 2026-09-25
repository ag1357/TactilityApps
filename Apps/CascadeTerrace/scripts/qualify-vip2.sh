#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results/vip2 build/vip2-desktop
make vip2-test | tee results/vip2/platform-tests.txt
make build/cascade
./build/cascade --platform-selftest build/vip2-desktop | tee results/vip2/desktop.txt

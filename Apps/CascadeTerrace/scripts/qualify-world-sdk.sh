#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results/worldsdk
python3 tools/worldsdk/sdk.py compile content/worlds/cascade.json --output build/cascade.cws --c-include content/worlds/cascade.inc > results/worldsdk/cascade-manifest.json
python3 tools/worldsdk/sdk.py compile content/worlds/elek_grid.json --output build/elek_grid.cws > results/worldsdk/elek-manifest.json
make build/world_sdk_test build/world_state_test build/libsdk.so build/libworldview.so build/test
./build/world_sdk_test > results/worldsdk/traversal.json
./build/world_state_test > results/worldsdk/state.json
./build/test > results/worldsdk/legacy-final.txt
python3 tools/worldsdk/qualify.py
python3 tools/worldsdk/network_test.py
python3 tools/worldsdk/rendered_test.py > results/worldsdk/rendered-network.json
# This is a release gate, not an optional result that can be hidden by aggregation.
python3 tools/worldsdk/navigation_probe.py

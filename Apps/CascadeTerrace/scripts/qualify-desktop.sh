#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make all build/evaluate build/benchmark
mkdir -p results
./build/test > results/core-tests.json
./build/benchmark > results/desktop-performance.json
run_dir=$(mktemp -d)
trap 'rm -rf "$run_dir"' EXIT
./build/cascade --headless --save "$run_dir/world.save" --script tests/causal-before.ct > results/causal-before.log
# A distinct process; no globals or dialogue context survive.
./build/cascade --headless --load --save "$run_dir/world.save" --script tests/causal-after.ct > results/causal-after.log
for mode in 0 1; do
 for variant in 0 1 2; do
  ./build/evaluate tests/evaluation.tsv "$mode" "$variant" > "results/dialogue-$mode-$variant.jsonl"
  ./build/evaluate tests/heldout.tsv "$mode" "$variant" > "results/paraphrases-$mode-$variant.jsonl"
 done
done
python3 scripts/summarize.py

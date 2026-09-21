#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Original hashes refer to the exact decompressed bytes. Keep packed deliverables
# small; unpack before reproducing frozen evaluations or inspecting JSONL traces.
for file in research/data/*.jsonl.gz research/results/*traces.jsonl.gz; do
    gzip -dc "$file" > "${file%.gz}"
done

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash research/unpack.sh
# Runtime inference/build needs C11 only. Research training needs numpy/scipy.
make build/libsemantic.so build/semantic_test build/test build/evaluate build/librunga.so
./build/test > research/results/current-core.json
./build/semantic_test > research/results/semantic-tests.json
OPENBLAS_NUM_THREADS=1 python3 research/evaluate.py validation
OPENBLAS_NUM_THREADS=1 python3 research/evaluate.py test
python3 research/evaluate_a.py test
OPENBLAS_NUM_THREADS=1 python3 research/stress.py
OPENBLAS_NUM_THREADS=1 python3 research/retrieval_bench.py
# Final is intentionally NOT part of routine CI or tuning. To reproduce frozen
# final results without selection: python3 research/evaluate.py final

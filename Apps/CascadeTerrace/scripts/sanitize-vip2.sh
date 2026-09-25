#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build results/vip2
# LeakSanitizer is unavailable under workspace ptrace; ASan and UBSan stay enabled.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
flags=(-O1 -g -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -fno-sanitize-recover=all -Icore -DCG_TRAINED_MODEL)
cc "${flags[@]}" core/viewport.c tests/viewport.c -o build/san_vip2_viewport
cc "${flags[@]}" -DAI_TEST_THREADS -pthread core/action_input.c tests/action_input.c -o build/san_vip2_input
cc "${flags[@]}" -pthread core/platform_lifecycle.c tests/platform_lifecycle.c -o build/san_vip2_lifecycle
cc "${flags[@]}" core/action_input.c core/i2c_controls.c tests/i2c_controls.c -o build/san_vip2_i2c
cc "${flags[@]}" -Itests/i2c_stubs main/i2c_input.c core/i2c_controls.c tests/i2c_platform.c -o build/san_vip2_i2c_platform
read -r -a sources <<< "$(make --no-print-directory -s --eval='vip2-sources: ; @echo $(CORE)' vip2-sources)"
cc "${flags[@]}" "${sources[@]}" tests/gameplay_platform.c -lm -o build/san_vip2_gameplay
{
for suite in viewport input lifecycle i2c i2c_platform gameplay; do
    "./build/san_vip2_$suite"
done
} > results/vip2/sanitizer.txt 2> results/vip2/sanitizer.err
printf 'VI-P2: 6 ASan/UBSan suites passed\n'

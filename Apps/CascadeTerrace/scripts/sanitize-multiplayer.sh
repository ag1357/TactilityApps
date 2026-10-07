#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build results/multiplayer
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
read -r -a sources <<< "$(make --no-print-directory -s --eval='mp-sources: ; @echo $(CORE)' mp-sources)"
read -r -a sdl_cflags <<< "$(sdl2-config --cflags)"
read -r -a sdl_libs <<< "$(sdl2-config --libs)"
flags=(-O1 -g -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -fno-sanitize-recover=all -Icore -Ibuild -DCG_TRAINED_MODEL)
make build/content_fingerprint.h build/libanaphorum.so
cc "${flags[@]}" "${sources[@]}" content/online_authority.c tests/online_authority.c -lm -o build/san_online_authority
cc "${flags[@]}" "${sdl_cflags[@]}" "${sources[@]}" desktop/main.c multiplayer/client.c "${sdl_libs[@]}" -lm -pthread -o build/san_network_client
{
    ./build/san_online_authority
    ANAPHORUM_TEST_CLIENT="$PWD/build/san_network_client" python3 -m unittest discover -s tests -p 'test_network_client.py' -v
} > results/multiplayer/sanitizer.txt 2> results/multiplayer/sanitizer.err
printf 'Multiplayer authority/client ASan/UBSan: passed\n'

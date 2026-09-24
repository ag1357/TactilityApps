#!/usr/bin/env bash
# Address/undefined-behavior sanitizer refresh over every C suite in the
# world SDK qualification. Same sources as the Makefile targets, plus
# -fsanitize=address,undefined -fno-sanitize-recover=all; LeakSanitizer is
# disabled under the execution environment's tracing (see docs/STATUS.md).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build results/worldsdk
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
run() {
  local out="$1"; shift
  ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
    cc -O1 -g -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation \
       $SAN "$@" -lm -o "build/san_$(basename "$out" .json)"
}
{
run traversal world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c tests/world_sdk.c
run macro world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c tests/macro_sdk.c
run resource world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c tests/resource_sdk.c
run anomaly world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c tests/anomaly_sdk.c
run creature world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c tests/creature_sdk.c
run social world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c tests/social_sdk.c
run bridge -Icore -DCG_TRAINED_MODEL world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c content/cascade_adapter.c core/world.c core/state.c tests/bridge_sdk.c
run authority_merge world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c tests/world_state.c
run legacy -Icore -DCG_TRAINED_MODEL world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c content/cascade_adapter.c core/semantic.c core/decision.c core/general_dialogue.c research/rung_a/cognition.c core/world.c core/state.c core/dialogue.c core/persistence.c core/render.c tests/test.c
run presentation -Icore -DCG_TRAINED_MODEL core/presentation.c tests/presentation.c
for suite in traversal macro resource anomaly creature social bridge authority_merge legacy presentation; do
  "./build/san_$suite"
done
} > results/worldsdk/sanitizer.txt 2> results/worldsdk/sanitizer.err
echo "sanitizer refresh: $(wc -l < results/worldsdk/sanitizer.txt) suites clean"

CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation
CPPFLAGS += -Icore -DCG_TRAINED_MODEL
SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS ?= $(shell sdl2-config --libs)
SEMANTIC = core/semantic.c core/decision.c
WORLD_SDK = world/schema.c world/recipe.c world/geometry.c world/traversal.c world/state.c world/resource.c world/creature.c world/social.c world/persistence.c
CORE = core/action_input.c core/viewport.c core/interaction.c content/cascade_interaction.c $(WORLD_SDK) content/cascade_adapter.c $(SEMANTIC) core/general_dialogue.c research/rung_a/cognition.c core/world.c core/state.c core/dialogue.c core/persistence.c core/render.c
all: build/cascade build/test
build/world_sdk_test: $(WORLD_SDK) tests/world_sdk.c world/sdk.h | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
sdk-test: build/world_sdk_test
	./build/world_sdk_test
build/macro_sdk_test: $(WORLD_SDK) tests/macro_sdk.c world/sdk.h content/worlds/macro.inc | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
macro-test: build/macro_sdk_test
	./build/macro_sdk_test
build/resource_sdk_test: $(WORLD_SDK) tests/resource_sdk.c world/sdk.h content/worlds/macro.inc | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
resource-test: build/resource_sdk_test
	./build/resource_sdk_test
build/anomaly_sdk_test: $(WORLD_SDK) tests/anomaly_sdk.c world/sdk.h content/worlds/macro.inc | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
anomaly-test: build/anomaly_sdk_test
	./build/anomaly_sdk_test
build/creature_sdk_test: $(WORLD_SDK) tests/creature_sdk.c world/state.h content/worlds/macro.inc | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
creature-test: build/creature_sdk_test
	./build/creature_sdk_test
build/social_sdk_test: $(WORLD_SDK) tests/social_sdk.c world/state.h content/worlds/social.inc | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
social-test: build/social_sdk_test
	./build/social_sdk_test
build/bridge_sdk_test: $(WORLD_SDK) content/cascade_adapter.c core/world.c core/state.c tests/bridge_sdk.c content/cascade_adapter.h core/game.h content/worlds/cascade.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -o $@
bridge-test: build/bridge_sdk_test
	./build/bridge_sdk_test
build:
	mkdir -p build
build/cascade: $(CORE) desktop/main.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) $(filter %.c,$^) $(SDL_LIBS) -lm -o $@
build/test: $(CORE) tests/test.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -lm -o $@
test: build/test
	./build/test
clean:
	rm -rf build
.PHONY: all clean test
build/evaluate: $(CORE) tests/evaluate.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -lm -o $@
build/benchmark: $(CORE) tests/benchmark.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -lm -o $@

build/cascade build/test build/evaluate build/benchmark: $(wildcard core/*.h core/*.inc)

build/libsemantic.so: $(SEMANTIC) core/semantic.h research/models/decision_int8.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC $(SEMANTIC) -o $@

build/semantic_test: $(SEMANTIC) tests/semantic_test.c core/semantic.h research/models/decision_int8.inc | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -o $@

build/cascade build/test build/evaluate build/benchmark: research/models/decision_int8.inc

build/rung_a.o: research/rung_a/cognition.c core/game.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -Dnpc_view=research_npc_view -Dgame_apply=research_game_apply -c $< -o $@
build/librunga.so: research/rung_a/adapter.c build/rung_a.o $(SEMANTIC) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC $(filter %.c %.o,$^) -o $@
build/cognition_demo: $(CORE) research/rung_a/cognition_demo.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -lm -o $@
build/generality_probe: $(CORE) research/rung_a/generality_probe.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -lm -o $@

build/libsdk.so: $(WORLD_SDK) world/sdk.h | build
	$(CC) $(CFLAGS) -shared -fPIC $(filter %.c,$^) -o $@

build/cascade build/test build/evaluate build/benchmark: content/worlds/cascade.inc

build/world_viewer: $(CORE) desktop/world_viewer.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) $(filter %.c,$^) $(SDL_LIBS) -lm -o $@

build/world_state_test: $(WORLD_SDK) tests/world_state.c world/state.h | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

build/libworldview.so: $(CORE) core/render.h world/state.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC $(filter %.c,$^) -lm -o $@

build/world_sdk_test build/world_state_test build/libsdk.so build/libworldview.so build/world_viewer build/cascade build/test build/evaluate build/benchmark: $(wildcard world/*.h) content/worlds/cascade.inc content/worlds/macro.inc
.PHONY: sdk-test sdk-qualify
sdk-qualify:
	./scripts/qualify-world-sdk.sh

build/presentation_test: core/presentation.c tests/presentation.c core/presentation.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -o $@
.PHONY: presentation-test
presentation-test: build/presentation_test
	./build/presentation_test

build/viewport_test: core/viewport.c tests/viewport.c core/viewport.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -o $@
viewport-test: build/viewport_test
	./build/viewport_test

build/action_input_test: core/action_input.c tests/action_input.c core/action_input.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DAI_TEST_THREADS -pthread $(filter %.c,$^) -o $@
action-input-test: build/action_input_test
	./build/action_input_test
build/gameplay_platform_test: $(CORE) tests/gameplay_platform.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -lm -o $@
gameplay-platform-test: build/gameplay_platform_test
	./build/gameplay_platform_test
build/i2c_controls_test: core/action_input.c core/i2c_controls.c tests/i2c_controls.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c,$^) -o $@
i2c-controls-test: build/i2c_controls_test
	./build/i2c_controls_test

build/platform_lifecycle_test: core/platform_lifecycle.c tests/platform_lifecycle.c core/platform_lifecycle.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(filter %.c,$^) -o $@
platform-lifecycle-test: build/platform_lifecycle_test
	./build/platform_lifecycle_test

build/i2c_platform_test: main/i2c_input.c core/i2c_controls.c tests/i2c_platform.c | build
	$(CC) $(CFLAGS) -Itests/i2c_stubs -Icore $(filter %.c,$^) -o $@
i2c-platform-test: build/i2c_platform_test
	./build/i2c_platform_test
.PHONY: vip2-test
vip2-test: viewport-test action-input-test gameplay-platform-test platform-lifecycle-test i2c-controls-test i2c-platform-test

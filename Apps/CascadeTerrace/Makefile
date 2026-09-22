CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation
CPPFLAGS += -Icore -DCG_TRAINED_MODEL
SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS ?= $(shell sdl2-config --libs)
SEMANTIC = core/semantic.c core/decision.c
WORLD_SDK = world/schema.c world/recipe.c
CORE = $(SEMANTIC) core/general_dialogue.c research/rung_a/cognition.c core/world.c core/state.c core/dialogue.c core/persistence.c core/render.c
all: build/cascade build/test
build/world_sdk_test: $(WORLD_SDK) tests/world_sdk.c world/sdk.h | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
sdk-test: build/world_sdk_test
	./build/world_sdk_test
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

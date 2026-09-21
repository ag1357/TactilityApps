CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation
CPPFLAGS += -Icore
SDL_CFLAGS ?= $(shell sdl2-config --cflags)
SDL_LIBS ?= $(shell sdl2-config --libs)
CORE = core/world.c core/state.c core/dialogue.c core/persistence.c core/render.c
all: build/cascade build/test
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

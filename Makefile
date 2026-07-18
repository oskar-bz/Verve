# Verve — modular build. Static core library + tests + demo, all clang.
CC       := clang
CFLAGS   ?= -std=c11 -Iinclude -Wall -Wextra -Wshadow -Wconversion -g -O2 \
            -fno-omit-frame-pointer
DEPFLAGS := -MMD -MP
LDFLAGS  ?=
LDLIBS   ?= -lm

BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
LIB      := $(BUILD)/libverve.a

TEST_SRC := $(wildcard tests/*.c)
TEST_BIN := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRC))

# Headless examples build with the generic rule; the windowed GUI demo needs SDL3.
DEMO_SRC := $(filter-out examples/gui_demo.c,$(wildcard examples/*.c))
DEMO_BIN := $(patsubst examples/%.c,$(BUILD)/%,$(DEMO_SRC))

# SDL3 + OpenGL backend (libepoxy loader) + stb_truetype (vendored).
# Vendored stb is noisy under -Wconversion/-Wshadow, so this TU set stays at
# -Wall -Wextra rather than the core's stricter flags.
GUI_CFLAGS := -std=c11 -Iinclude -Ibackends -Ivendor -Wall -Wextra -g -O2 \
              $(shell pkg-config --cflags sdl3)
GUI_LIBS   := $(shell pkg-config --libs sdl3) -lepoxy -lGL

.PHONY: all lib test demo gui clean
all: lib test demo

lib: $(LIB)

$(LIB): $(OBJ)
	@mkdir -p $(BUILD)
	ar rcs $@ $^

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(OBJ:.o=.d)

$(BUILD)/%: tests/%.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/%: examples/%.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

demo: $(DEMO_BIN)

gui: $(BUILD)/gui_demo
$(BUILD)/gui_demo: examples/gui_demo.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@

test: $(TEST_BIN)
	@echo "== running tests =="
	@fail=0; for t in $(TEST_BIN); do \
		echo "-- $$t"; $$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; exit 1; fi

clean:
	rm -rf $(BUILD)

# Verve — modular build. Static core library + tests + demo, all clang.
CC       := clang
CFLAGS   ?= -std=c11 -Iinclude -Wall -Wextra -Wshadow -Wconversion -g -O2 \
            -fno-omit-frame-pointer
LDFLAGS  ?=
LDLIBS   ?= -lm

BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
LIB      := $(BUILD)/libverve.a

TEST_SRC := $(wildcard tests/*.c)
TEST_BIN := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRC))

DEMO_SRC := $(wildcard examples/*.c)
DEMO_BIN := $(patsubst examples/%.c,$(BUILD)/%,$(DEMO_SRC))

.PHONY: all lib test demo clean
all: lib test demo

lib: $(LIB)

$(LIB): $(OBJ)
	@mkdir -p $(BUILD)
	ar rcs $@ $^

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%: tests/%.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/%: examples/%.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

demo: $(DEMO_BIN)

test: $(TEST_BIN)
	@echo "== running tests =="
	@fail=0; for t in $(TEST_BIN); do \
		echo "-- $$t"; $$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; exit 1; fi

clean:
	rm -rf $(BUILD)

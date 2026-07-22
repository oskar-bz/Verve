# Verve — modular build. Static core library + tests + demo, all clang.
CC       := clang
CFLAGS   ?= -std=c11 -Iinclude -Wall -Wextra -Wshadow -Wconversion -g -O2 \
            -fno-omit-frame-pointer
DEPFLAGS := -MMD -MP
LDFLAGS  ?=
LDLIBS   ?= -lm -lpthread

# Performance instrumentation: make VV_PERF=1 enables granular phase timing.
PERF_CFLAGS := $(if $(VV_PERF),-DVV_PERF,)

BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
LIB      := $(BUILD)/libverve.a

TEST_SRC := $(wildcard tests/*.c)
TEST_BIN := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRC))

# Headless examples build with the generic rule; windowed demos need SDL3, and
# the craz vector demos (icons, svgview) additionally need ../craz — so those
# are excluded here and get their own rules below.
DEMO_SRC := $(filter-out examples/gui_demo.c examples/sevenguis.c \
                         examples/icons.c examples/svgview.c,$(wildcard examples/*.c))
DEMO_BIN := $(patsubst examples/%.c,$(BUILD)/%,$(DEMO_SRC))

# SDL3 + OpenGL backend (libepoxy loader) + stb_truetype (vendored).
# Vendored stb is noisy under -Wconversion/-Wshadow, so this TU set stays at
# -Wall -Wextra rather than the core's stricter flags.
GUI_CFLAGS := -std=c11 -Iinclude -Ibackends -Ivendor -Wall -Wextra -g -O2 \
              $(shell pkg-config --cflags sdl3)
GUI_LIBS   := $(shell pkg-config --libs sdl3) -lepoxy -lGL

# craz CPU vector rasterizer (sibling checkout) — a build-time dependency of the
# vector demos only (icons, svgview). Built on demand via its own Makefile. Note:
# these demos also need the craz-integrated GL backend + backends/vv_vector.{c,h}
# (the vv_app_vector path); if that module is absent the demos won't link.
CRAZ_DIR    := ../craz
CRAZ_LIB    := $(CRAZ_DIR)/libcraz.a
CRAZ_CFLAGS := -I$(CRAZ_DIR)/include
VV_VECTOR   := $(wildcard backends/vv_vector.c)

.PHONY: all lib test demo gui vector icons svgview clean
all: lib test demo

lib: $(LIB)

$(LIB): $(OBJ)
	@mkdir -p $(BUILD)
	ar rcs $@ $^

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(PERF_CFLAGS) -c $< -o $@

-include $(OBJ:.o=.d)

$(BUILD)/%: tests/%.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/%: examples/%.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

demo: $(DEMO_BIN)

# Windowed examples share the SDL3/GL backend build (explicit, not pattern, to
# avoid clashing with the headless examples rule).
gui: $(BUILD)/gui_demo $(BUILD)/sevenguis $(BUILD)/mycounter $(BUILD)/kanban \
     $(BUILD)/palette $(BUILD)/habit $(BUILD)/inspector $(BUILD)/transitions \
     $(BUILD)/table $(BUILD)/finder $(BUILD)/playground $(BUILD)/bindings \
     $(BUILD)/showcase $(BUILD)/theme_editor $(BUILD)/panels $(BUILD)/dates $(BUILD)/gallery $(BUILD)/i18n \
     $(BUILD)/chat $(BUILD)/visualize $(BUILD)/markdown $(BUILD)/async $(BUILD)/dragdrop \
     $(BUILD)/perf_demo
# perf_demo links the drop-in HUD (examples/inspect/vv_perf_hud.h). PERF_CFLAGS
# governs both the core objects and this TU, so `make VV_PERF=1 perf_demo`
# instruments everything; a plain build still runs (the panel says how to enable).
$(BUILD)/perf_demo: examples/perf_demo.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $(PERF_CFLAGS) -Iexamples/inspect $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
# craz vector demos. `make vector` (or icons/svgview) builds them. They link the
# GL backend + the vv_vector module + libcraz; $(CRAZ_LIB) is built on demand.
vector: icons svgview
icons:   $(BUILD)/icons
svgview: $(BUILD)/svgview
$(CRAZ_LIB):
	$(MAKE) -C $(CRAZ_DIR)
$(BUILD)/icons: examples/icons.c backends/vv_sdl_gl.c $(VV_VECTOR) $(LIB) $(CRAZ_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $(CRAZ_CFLAGS) $(filter %.c %.a,$^) $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/svgview: examples/svgview.c backends/vv_sdl_gl.c $(VV_VECTOR) $(LIB) $(CRAZ_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $(CRAZ_CFLAGS) $(filter %.c %.a,$^) $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@

$(BUILD)/gui_demo: examples/gui_demo.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/sevenguis: examples/sevenguis.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/mycounter: examples/mycounter.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/kanban: examples/kanban.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/palette: examples/palette.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/habit: examples/habit.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/inspector: examples/inspector.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) -Iexamples/inspect $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/transitions: examples/transitions.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/table: examples/table.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/finder: examples/finder.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/playground: examples/playground.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/bindings: examples/bindings.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/showcase: examples/showcase.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/theme_editor: examples/theme_editor.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/panels: examples/panels.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/dates: examples/dates.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/i18n: examples/i18n.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/gallery: examples/gallery.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/chat: examples/chat.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/visualize: examples/visualize.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/markdown: examples/markdown.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/async: examples/async.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/dragdrop: examples/dragdrop.c backends/vv_sdl_gl.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
# Hot-reload demo: the view is a .so the host dlopen's and swaps on change.
# Run ./build/hotdemo, then edit examples/hot/view.c and `make hot` to rebuild
# just the .so — the running host picks it up and keeps its state.
.PHONY: hot
hot: $(BUILD)/hotview.so
$(BUILD)/hotview.so: examples/hot/view.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) -Iexamples/hot -fPIC -shared $< $(LIB) $(LDLIBS) -o $@
$(BUILD)/hotdemo: examples/hot/host.c backends/vv_sdl_gl.c $(LIB) $(BUILD)/hotview.so
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) -Iexamples/hot examples/hot/host.c backends/vv_sdl_gl.c $(LIB) \
		$(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -ldl -o $@

test: $(TEST_BIN)
	@echo "== running tests =="
	@fail=0; for t in $(TEST_BIN); do \
		echo "-- $$t"; $$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; exit 1; fi

clean:
	rm -rf $(BUILD)

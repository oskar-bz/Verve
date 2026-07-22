# Verve — modular build. Static core library + tests + demo, all clang.
CC       := clang
# -fPIC so the static core can also be linked into shared objects — notably the
# hot-reload view .so, which pulls core objects (e.g. vv_theme) transitively.
CFLAGS   ?= -std=c11 -Iinclude -Wall -Wextra -Wshadow -Wconversion -g -O2 \
            -fno-omit-frame-pointer -fPIC
DEPFLAGS := -MMD -MP
LDFLAGS  ?=
LDLIBS   ?= -lm -lpthread

BUILD    := build
SRC      := $(wildcard src/*.c)
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
LIB      := $(BUILD)/libverve.a

TEST_SRC := $(wildcard tests/*.c)
TEST_BIN := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRC))

# Headless examples build with the generic rule; windowed demos need SDL3.
DEMO_SRC := $(filter-out examples/gui_demo.c examples/sevenguis.c,$(wildcard examples/*.c))
DEMO_BIN := $(patsubst examples/%.c,$(BUILD)/%,$(DEMO_SRC))

# The craz CPU vector rasterizer (../craz) powers the GL backend's text (analytic
# -AA subpixel glyph cache), icons, and dynamic SVG. It is a hard dependency of
# the SDL/GL backend; the core library itself stays craz-free.
CRAZ_DIR ?= ../craz
CRAZ_LIB := $(CRAZ_DIR)/libcraz.a

# SDL3 + OpenGL backend (libepoxy loader) + craz (vendored stb lives inside craz).
GUI_CFLAGS := -std=c11 -Iinclude -Ibackends -Ivendor -I$(CRAZ_DIR)/include \
              -Wall -Wextra -g -O2 $(shell pkg-config --cflags sdl3)
GUI_LIBS   := $(shell pkg-config --libs sdl3) -lepoxy -lGL $(CRAZ_LIB)

# The GL backend is two TUs: the SDL/GL renderer and the craz-backed vector
# services (icons / SVG / canvas). GUI targets compile both.
GUI_BACKEND := backends/vv_sdl_gl.c backends/vv_vector.c

# Build craz's static lib on demand (its own Makefile). GUI targets depend on it.
$(CRAZ_LIB):
	$(MAKE) -C $(CRAZ_DIR)

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

# Windowed examples share the SDL3/GL backend build (explicit, not pattern, to
# avoid clashing with the headless examples rule).
gui: $(CRAZ_LIB) $(BUILD)/gui_demo $(BUILD)/sevenguis $(BUILD)/mycounter $(BUILD)/kanban \
     $(BUILD)/palette $(BUILD)/habit $(BUILD)/inspector $(BUILD)/transitions \
     $(BUILD)/table $(BUILD)/finder $(BUILD)/playground $(BUILD)/bindings \
     $(BUILD)/showcase $(BUILD)/theme_editor $(BUILD)/panels $(BUILD)/dates $(BUILD)/gallery $(BUILD)/i18n \
     $(BUILD)/chat $(BUILD)/visualize $(BUILD)/markdown $(BUILD)/async $(BUILD)/dragdrop \
     $(BUILD)/icons
$(BUILD)/gui_demo: examples/gui_demo.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/sevenguis: examples/sevenguis.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/mycounter: examples/mycounter.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/kanban: examples/kanban.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/palette: examples/palette.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/habit: examples/habit.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/inspector: examples/inspector.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) -Iexamples/inspect $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/transitions: examples/transitions.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/table: examples/table.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/finder: examples/finder.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/playground: examples/playground.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/bindings: examples/bindings.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/showcase: examples/showcase.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/theme_editor: examples/theme_editor.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/panels: examples/panels.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/dates: examples/dates.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/i18n: examples/i18n.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/gallery: examples/gallery.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/chat: examples/chat.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/visualize: examples/visualize.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/markdown: examples/markdown.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/async: examples/async.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/dragdrop: examples/dragdrop.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@
$(BUILD)/icons: examples/icons.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@

# SVG viewer: Verve UI + the craz CPU vector rasterizer. craz is now part of the
# GUI flags/libs (see GUI_CFLAGS/GUI_LIBS above), so this is a plain GUI target.
svgview: $(BUILD)/svgview
$(BUILD)/svgview: examples/svgview.c $(GUI_BACKEND) $(LIB) $(CRAZ_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) examples/svgview.c $(GUI_BACKEND) \
		$(LIB) $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@

shadcn: $(BUILD)/shadcn

$(BUILD)/shadcn: components/shadcn.c
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) components/shadcn.c $(GUI_BACKEND) $(LIB) \
		$(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -ldl -o $@

fluent: $(BUILD)/fluent

$(BUILD)/fluent: components/fluent.c
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) components/fluent.c $(GUI_BACKEND) $(LIB) \
		$(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -ldl -o $@

components: shadcn fluent

 
# Hot-reload demo: the view is a .so the host dlopen's and swaps on change.
# Run ./build/hotdemo, then edit examples/hot/view.c and `make hot` to rebuild
# just the .so — the running host picks it up and keeps its state.
# .PHONY: hot
hot: $(BUILD)/hotview.so
$(BUILD)/hotview.so: examples/hot/view.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) -Iexamples/hot -fPIC -shared $< $(LIB) $(LDLIBS) -o $@
$(BUILD)/hotdemo: examples/hot/host.c $(GUI_BACKEND) $(LIB) $(BUILD)/hotview.so
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) -Iexamples/hot examples/hot/host.c $(GUI_BACKEND) $(LIB) \
		$(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -ldl -o $@

# ---- starter templates -----------------------------------------------------
# A normal minimal app (one vv_app_run call) and a hot-reloadable app (host +
# swappable view .so via vv_hot_run). Copy templates/ to start a new project.
.PHONY: templates template-minimal template-hot template-hot-view
templates: template-minimal template-hot

# Minimal single-file app.
template-minimal: $(BUILD)/tpl_minimal
$(BUILD)/tpl_minimal: templates/minimal.c $(GUI_BACKEND) $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) $^ $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@

# Hot template: build the host once, then rebuild just the view with
# `make template-hot-view` while ./build/tpl_hotdemo keeps running.
template-hot: $(BUILD)/tpl_hotdemo
template-hot-view: $(BUILD)/tpl_hotview.so
$(BUILD)/tpl_hotview.so: templates/hot/view.c $(LIB)
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) -Itemplates/hot -fPIC -shared $< $(LIB) $(LDLIBS) -o $@
$(BUILD)/tpl_hotdemo: templates/hot/host.c $(GUI_BACKEND) backends/vv_hot.c $(LIB) $(BUILD)/tpl_hotview.so
	@mkdir -p $(BUILD)
	$(CC) $(GUI_CFLAGS) -Itemplates/hot templates/hot/host.c $(GUI_BACKEND) backends/vv_hot.c $(LIB) \
		$(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -ldl -o $@

test: $(TEST_BIN)
	@echo "== running tests =="
	@fail=0; for t in $(TEST_BIN); do \
		echo "-- $$t"; $$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; exit 1; fi

clean:
	rm -rf $(BUILD)

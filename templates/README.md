# Verve starter templates

Two ways to start a Verve app. Both lean on the turn-key runners in the SDL/GL
backend, so you only write `update()` and `view()` — no window/loop boilerplate.

## Minimal app — `templates/minimal.c`

A single file. `main()` is one call to `vv_app_run()`.

```sh
make template-minimal
./build/tpl_minimal
```

To start your own: copy `minimal.c`, rename the messages/state, grow `view()`.
The shape to keep:

- **`view(ctx, state)`** is a *pure function of state* — it builds the UI and
  never mutates state. Widgets emit **messages** when acted on.
- **`update(state, ev)`** is the *only* place state changes — it handles messages.

## Hot-reloadable app — `templates/hot/`

For prototyping: keep the app running and edit the UI live. State lives in the
host process (so it survives reloads); the `update`/`view` functions live in a
`.so` that the host swaps in whenever you rebuild it.

```sh
make template-hot         # build the host + the initial view .so
./build/tpl_hotdemo       # leave this running

# in another terminal, edit templates/hot/view.c, then:
make template-hot-view    # rebuild just the .so -> the window updates live
```

Three files:

| File       | Role                                                              |
|------------|-------------------------------------------------------------------|
| `app.h`    | Shared contract: your `App` state struct + message ids.           |
| `host.c`   | Rarely edited. Owns the state; one call to `vv_hot_run()`.        |
| `view.c`   | **The file you iterate on** — `view_update` + `view_build`.        |

Auto-rebuild on save (optional), e.g. with `entr`:

```sh
ls templates/hot/view.c | entr make template-hot-view
```

## The runner API

- `vv_app_run(&(vv_AppDesc){...})` — normal app. Declared in `backends/vv_sdl_gl.h`.
- `vv_hot_run(&(vv_HotDesc){...})` — hot-reload host. Declared in `backends/vv_hot.h`
  (POSIX/`dlopen`; link with `-ldl`).

Both take zeroable descriptors: unset fields fall back to sensible defaults
(720–900px window, a dark clear colour, the default font search list). Set
`.clipboard = true` if your UI has text fields that need OS copy/paste.

> Note: the core library is built with `-fPIC` so it can be linked into the
> hot-reload `.so`.

# Verve

An animation-native, scriptable immediate-mode UI library in C for
graphics-heavy applications. See [verve-design.md](verve-design.md) for the full
design and rationale.

Immediate in its API, retained in its state, every-frame in its cadence — "React's
architecture written in C." User code declares the *target* style for the current
logical state; the retained node holds the *actual* value and its velocity;
springs close the gap.

## Build

```sh
make          # library + tests + demo (clang)
make test     # run the test suite
./build/headless_demo
```

The core has no backend dependency — it emits a flat `vv_CommandBuffer`. The
headless demo prints that buffer to prove the pipeline end to end.

## Module layout

Each module is a header in `include/verve/` and a source in `src/`, kept
independently testable (§ references are to the design doc):

| Module | Role |
|---|---|
| `vv_types`   | value types: vec/rect/color/edges/mat23 |
| `vv_arena`   | the only allocator — persistent + frame arenas (§13) |
| `vv_id`      | parent-scoped identity derivation (§3.1) |
| `vv_node`    | retained node + pool with freelist + ID→index map (§3.2) |
| `vv_anim`    | springs, the animation primitive (§6.1) |
| `vv_layout`  | layout declaration + engine (§5) |
| `vv_style`   | style declaration + interpolated actual state (§7) |
| `vv_command` | render command protocol — the core's entire output (§8) |
| `vv_backend` | backend interface + command dispatch (§15) |
| `vv_context` | frame lifecycle, build API, reconciliation (§4) |

## Status — Phase 0 (of the §17 build order)

Done: arenas, IDs, node pool, reconciliation with lifecycle (birth /
persistence / exit), the Build/Present phase split, command emission, backend
dispatch, headless backend. Identity is stable and keyed widgets survive sibling
shifts (the §18 top risk), verified by tests.

Placeholder for later phases: layout is a trivial stacker (real 4-pass solver =
Phase 1); `actual_rect` snaps to `layout_rect` (FLIP springs = Phase 3); style
interpolation, input, text, Lua, idle mode follow the design's build order.

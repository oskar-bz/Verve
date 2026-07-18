-- synth.lua — ergonomics sketch (NOT yet runnable; Phase 7).
--
-- Shows why Lua is "the workflow bet": value bindings with metadata, so preset
-- save/load, undo, and automation come for free (§12); hot reload (edit this
-- file, the running window updates without losing `params`); and a coroutine
-- driving a multi-step animation without a state machine.

local M = {}

-- Bound parameters: metadata (range, unit, curve) lives with the value, so a
-- slider needs no min/max at the call site and the host can enumerate them for
-- presets / MIDI-learn / an auto-generated inspector.
M.params = ui.store("synth.params", ui.params {
  cutoff    = { 800,  min = 20, max = 20000, unit = "Hz", curve = 3, log = true },
  resonance = { 0.2,  min = 0,  max = 1 },
  drive     = { 0.0,  min = 0,  max = 24,   unit = "dB" },
  bypass    = { false },
})

function M.view(ui, s)
  local p = M.params
  ui.column({ padding = 20, gap = 8, w = 360 }, function()
    ui.row({ justify = "space_between", align = "center" }, function()
      ui.label("Filter", { size = 20 })
      ui.toggle(p.bypass)                       -- bound: reads+writes p.bypass
    end)

    -- Bound sliders: label/unit/curve come from the param. Editing one is a
    -- single undoable edit (drag = one entry, not one per frame).
    ui.knob(p.cutoff)
    ui.slider(p.resonance)
    ui.slider(p.drive)

    if ui.button("Randomize") then
      p.cutoff    = math.random(80, 8000)
      p.resonance = math.random()
    end

    -- A cursor-driven readout: opt into move events (the C vv_On.move), get the
    -- pointer in local space. Only this widget rebuilds on motion.
    ui.box({ h = 120, on_move = true }, function(evt)
      if evt.move then ui.label(("x=%.0f y=%.0f"):format(evt.move.x, evt.move.y)) end
    end)
  end)
end

-- Coroutines: describe a sequence in straight-line code; the scheduler resumes
-- it each frame. This is the thing that's painful in C and natural in Lua.
function M.intro(ui)
  return coroutine.wrap(function()
    ui.animate(M.params, "drive", 12, { over = 0.4 })  -- ramp in
    ui.wait(0.2)
    ui.animate(M.params, "drive", 0, { over = 0.8, spring = "bouncy" })
  end)
end

return M

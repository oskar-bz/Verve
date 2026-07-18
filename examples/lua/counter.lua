-- counter.lua — ergonomics sketch (NOT yet runnable; Phase 7).
--
-- In Lua, the arena/lifetime constraints that pushed C toward message/update/
-- view don't exist: the GC makes closures and transient strings safe. So Lua
-- can use the *immediate* idiom — widgets return their interaction, and you
-- mutate state inline. No message enums, no update() switch, no explicit keys
-- (the call-site identity is derived automatically; pass a key only in loops).

local M = {}

-- State is just a table. `ui.store` (below) makes it survive hot reload.
M.state = ui.store("counter", { count = 0, subtract = false })

function M.view(ui, s)
  ui.column({ w = "grow", h = "grow", justify = "center", align = "center", gap = 12 }, function()
    ui.label("Counter!", { size = 26 })

    -- Controlled widget: pass the value in, assign the returned value out.
    s.subtract = ui.checkbox("Enable subtraction?", s.subtract)

    ui.label(tostring(s.count), { size = 40 })

    ui.row({ gap = 10 }, function()
      local step = s.subtract and -1 or 1
      -- Buttons return true on click; mutate state right here.
      if ui.button(s.subtract and "-1"  or "+1")  then s.count = s.count + step      end
      if ui.button(s.subtract and "-10" or "+10") then s.count = s.count + step * 10 end
      if ui.button("Reset") then s.count = 0 end
    end)
  end)
end

return M

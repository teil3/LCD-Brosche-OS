-- Minimaler Einstieg für eigene Brosche-Skripte
APP_NAME = "Starter"

local elapsed = 0
local flash = false

function setup()
  -- Wird einmal aufgerufen, wenn das Skript geladen wird.
  brosche.clear(brosche.rgb(0, 0, 0))
  brosche.log('Starter setup fertig')
end

function loop(dt)
  -- Wird ca. alle 16 ms aufgerufen (dt = vergangene Zeit in ms)
  elapsed = elapsed + dt
  if elapsed >= 800 then
    elapsed = 0
    flash = not flash
    local color = flash and brosche.rgb(20, 180, 255) or brosche.rgb(5, 20, 60)
    brosche.fill(color)
    brosche.text(120, 120, flash and 'Hallo' or 'Lua', brosche.rgb(255, 255, 255))
  end
end

function onButton(btn, event)
  -- btn == 2 bedeutet BTN2, weitere Buttons können später folgen.
  if btn ~= 2 then
    return
  end

  if event == 'single' then
    brosche.log('BTN2 kurz')
    brosche.text(120, 200, 'Single!', brosche.rgb(255, 255, 0))
  elseif event == 'double' then
    brosche.log('BTN2 doppelt')
    brosche.text(120, 200, 'Double!', brosche.rgb(0, 255, 150))
  elseif event == 'long' then
    brosche.log('BTN2 lang')
    flash = false
    brosche.fill(brosche.rgb(0, 0, 0))
    brosche.text(120, 120, 'Reset', brosche.rgb(255, 0, 80))
  end
end

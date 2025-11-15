APP_NAME = "Font"

local fonts = {
  '/system/fonts/RobotoMono18.vlw',
  '/system/fonts/JetBrainsMono16.vlw',
  '/system/font.vlw'
}

local idx = 1
local lastSwitch = 0

local function cycleFont(step)
  idx = ((idx - 1 + step) % #fonts) + 1
  local path = fonts[idx]
  local ok = brosche.loadFont(path)
  if ok then
    brosche.log('Font geladen: ' .. path)
  else
    brosche.log('Konnte Font nicht laden: ' .. path)
  end
end

function setup()
  brosche.clear(brosche.rgb(5, 20, 50))
  brosche.log('Lua Font-Demo')
  cycleFont(0)
end

function loop(dt)
  lastSwitch = lastSwitch + dt
  if lastSwitch >= 1500 then
    lastSwitch = 0
    brosche.clear(brosche.rgb(10, 30, 70))
    brosche.text(120, 100, 'Font #' .. idx, brosche.rgb(255, 255, 200))
    brosche.text(120, 150, 'BTN2: Wechsel', brosche.rgb(180, 200, 255))
  end
end

function onButton(btn, event)
  if btn ~= 2 then
    return
  end
  if event == 'single' then
    cycleFont(1)
  elseif event == 'double' then
    cycleFont(-1)
  elseif event == 'long' then
    brosche.unloadFont()
    brosche.log('Standard-Font aktiviert')
  end
end

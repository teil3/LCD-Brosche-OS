APP_NAME = 'Farbverlauf'
local angle = 0

function setup()
  brosche.clear()
  brosche.log('Lua main.lua gestartet')
end

function loop(dt)
  angle = (angle + dt / 20) % 360
  local r = math.floor((math.sin(math.rad(angle)) * 0.5 + 0.5) * 255)
  local g = math.floor((math.sin(math.rad(angle + 120)) * 0.5 + 0.5) * 255)
  local b = math.floor((math.sin(math.rad(angle + 240)) * 0.5 + 0.5) * 255)
  brosche.fill(brosche.rgb(r, g, b))
  brosche.text(120, 120, string.format('%.0f', angle), brosche.rgb(0, 0, 0))
end

function onButton(btn, event)
  brosche.log(string.format('BTN%d %s', btn, event))
end

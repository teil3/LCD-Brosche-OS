APP_NAME = 'One Tone'
local baseColor = {math.random(0, 255), math.random(0, 255), math.random(0, 255)}
local elapsed = 0
local interval = 700 -- ms zwischen neuen Mustern

local function randomColor()
  local function clamp(value)
    if value < 0 then return 0 end
    if value > 255 then return 255 end
    return value
  end
  local r = clamp(baseColor[1] + math.random(-8, 7))
  local g = clamp(baseColor[2] + math.random(-8, 7))
  local b = clamp(baseColor[3] + math.random(-8, 7))
  return brosche.rgb(r, g, b)
end

local function drawPatterns()
  local startSize = 240
  local endSize = 20
  for size = startSize, endSize, -20 do
    local color = randomColor()
    local half = math.floor(size / 2)
    brosche.rect(120 - half, 120 - half, size, size, color)
  end
end

local function newBaseColor()
  baseColor = {math.random(0, 255), math.random(0, 255), math.random(0, 255)}
end

function setup()
  brosche.clear()
  brosche.log('o_tone.lua gestartet')
  drawPatterns()
end

function loop(dt)
  elapsed = elapsed + dt
  if elapsed >= interval then
    elapsed = elapsed - interval
    newBaseColor()
    drawPatterns()
  end
end

function onButton(btn, event)
  if event == 'long' then
    newBaseColor()
    drawPatterns()
  end
end

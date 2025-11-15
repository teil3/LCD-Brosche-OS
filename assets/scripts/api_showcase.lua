APP_NAME = "API Showcase"

local lastSwitch = 0
local colorIndex = 1
local palette = {
  brosche.rgb(255, 120, 120),
  brosche.rgb(120, 220, 255),
  brosche.rgb(180, 255, 150)
}
local logPath = '/scripts/api_showcase.log'
local counter = 0

local function drawHud()
  brosche.clear(brosche.rgb(8, 15, 30))
  local t = brosche.time()
  local ring = 40 + math.floor((t % 1000) / 1000 * 60)
  local color = palette[colorIndex]
  brosche.fillCircle(120, 120, ring, color)
  brosche.circle(120, 120, 110, brosche.rgb(255,255,255))
  local angle = (t / 10) % 360
  local rad = math.rad(angle)
  brosche.line(120, 120, 120 + math.cos(rad)*90, 120 + math.sin(rad)*90, brosche.rgb(255, 220, 90))
  brosche.triangle(30, 200, 70, 170, 90, 210, brosche.rgb(100, 180, 255))
  brosche.fillTriangle(200, 210, 230, 180, 250, 215, brosche.rgb(255, 160, 210))
  brosche.text(120, 32, string.format('t=%d ms', t), brosche.rgb(255,255,255))
  brosche.text(120, 60, string.format('Temp=%.1fC', brosche.temperature()), brosche.rgb(180,255,220))
end

local function appendLog(msg)
  local existing = brosche.readFile(logPath) or ''
  existing = existing .. msg .. '\n'
  brosche.writeFile(logPath, existing)
end

function setup()
  drawHud()
  appendLog('Setup @ ' .. brosche.time())
end

function loop(dt)
  lastSwitch = lastSwitch + dt
  if lastSwitch > 800 then
    lastSwitch = 0
    drawHud()
    counter = counter + 1
    if counter % 4 == 0 then
      appendLog(string.format('tick %d temp=%.1f', counter, brosche.temperature()))
    end
  end
end

function onButton(btn, event)
  if btn ~= 2 then return end
  if event == 'single' then
    colorIndex = (colorIndex % #palette) + 1
  elseif event == 'double' then
    local slides = brosche.listFiles('/slides') or {}
    brosche.log('Slides: ' .. #slides)
  elseif event == 'long' then
    local text = brosche.readFile(logPath) or '(leer)'
    brosche.log('Log len ' .. #text)
  end
end

APP_NAME = 'Shapes & FS'

local angle = 0
local lastWrite = 0
local colorIndex = 1
local colors = {
  brosche.rgb(255, 120, 80),
  brosche.rgb(80, 200, 255),
  brosche.rgb(180, 255, 140)
}
local logPath = '/scripts/lua_shapes.log'
local maxLogBytes = 1024
local tempSamples = {}
local sampleIndex = 1
local sampleCount = 40
local tempAccum = 0

local function drawScene()
  brosche.clear(brosche.rgb(6, 10, 25))
  local t = brosche.time() % 1000
  local ring = 40 + math.floor((t / 1000) * 40)
  local color = colors[colorIndex]
  brosche.fillCircle(120, 120, ring, color)
  brosche.circle(120, 120, 110, brosche.rgb(255, 255, 255))
  local rad = math.rad(angle)
  brosche.line(120, 120, 120 + math.cos(rad) * 90, 120 + math.sin(rad) * 90, brosche.rgb(250, 220, 90))
  brosche.triangle(20, 220, 60, 200, 80, 220, brosche.rgb(120, 180, 255))
  brosche.fillTriangle(200, 220, 230, 180, 250, 220, brosche.rgb(255, 150, 200))
  brosche.text(120, 30, string.format('t=%d ms', brosche.time()), brosche.rgb(255, 255, 255))
  local avgTemp = tempAccum / math.max(1, #tempSamples)
  brosche.text(120, 55, string.format('avg temp %.1fC', avgTemp), brosche.rgb(180, 255, 220))
end

local function trimLog(data)
  if #data <= maxLogBytes then
    return data
  end
  return data:sub(#data - maxLogBytes + 1)
end

local function appendLog(msg)
  local existing = brosche.readFile(logPath) or ''
  existing = trimLog(existing .. msg)
  brosche.writeFile(logPath, existing)
end

function setup()
  brosche.clear()
  tempSamples = {}
  tempAccum = 0
  angle = 0
  drawScene()
  appendLog('Setup @ ' .. brosche.time())
end

function loop(dt)
  angle = (angle + dt / 12) % 360
  drawScene()
  if brosche.time() - lastWrite > 1500 then
    lastWrite = brosche.time()
    colorIndex = (colorIndex % #colors) + 1
    local temp = brosche.temperature()
    if #tempSamples < sampleCount then
      table.insert(tempSamples, temp)
      tempAccum = tempAccum + temp
    else
      tempAccum = tempAccum - tempSamples[sampleIndex] + temp
      tempSamples[sampleIndex] = temp
      sampleIndex = (sampleIndex % sampleCount) + 1
    end
    appendLog(string.format('t=%d angle=%d temp=%.1f', brosche.time(), math.floor(angle), temp))
  end
end

function onButton(btn, event)
  if btn ~= 2 then return end
  if event == 'single' then
    colorIndex = (colorIndex % #colors) + 1
  elseif event == 'double' then
    local files = brosche.listFiles('/slides') or {}
    brosche.log('Slides: ' .. #files)
  elseif event == 'long' then
    local text = brosche.readFile(logPath) or '(leer)'
    brosche.log('log len ' .. #text)
  end
end

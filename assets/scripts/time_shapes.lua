APP_NAME = 'Shapes & FS'

local angle = 0
local lastWrite = 0
local colors = {
  brosche.rgb(255, 120, 80),
  brosche.rgb(80, 200, 255),
  brosche.rgb(180, 255, 140)
}

local function drawScene()
  brosche.clear(brosche.rgb(6, 10, 25))
  local t = brosche.time() % 1000
  local ring = 40 + math.floor((t / 1000) * 40)
  brosche.fillCircle(120, 120, ring, colors[(math.floor(angle / 120) % #colors) + 1])
  brosche.circle(120, 120, 110, brosche.rgb(255, 255, 255))
  brosche.line(120, 120, 120 + math.cos(math.rad(angle)) * 90, 120 + math.sin(math.rad(angle)) * 90, brosche.rgb(250, 220, 90))
  brosche.triangle(20, 220, 60, 200, 80, 220, brosche.rgb(120, 180, 255))
  brosche.fillTriangle(200, 220, 230, 180, 250, 220, brosche.rgb(255, 150, 200))
  brosche.text(120, 30, string.format('t=%d ms', brosche.time()), brosche.rgb(255, 255, 255))
end

local function logState()
  local msg = string.format('time=%d angle=%d\n', brosche.time(), angle)
  local ok = brosche.writeFile('/scripts/lua_log.txt', msg)
  if not ok then
    brosche.log('writeFile fehlgeschlagen')
  end
end

function setup()
  brosche.clear()
  drawScene()
  brosche.log('Shapes Demo')
end

function loop(dt)
  angle = (angle + dt / 8) % 360
  drawScene()
  if brosche.time() - lastWrite > 3000 then
    lastWrite = brosche.time()
    logState()
  end
end

function onButton(btn, event)
  if btn ~= 2 then return end
  if event == 'single' then
    angle = (angle + 45) % 360
  elseif event == 'double' then
    local files = brosche.listFiles('/slides') or {}
    brosche.log('Slides: ' .. #files)
  elseif event == 'long' then
    local text = brosche.readFile('/scripts/lua_log.txt') or '(leer)'
    brosche.log('log len ' .. #text)
  end
end

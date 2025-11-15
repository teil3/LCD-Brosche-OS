-- Memory Info Display
-- Copy this to SD card as /scripts/meminfo.lua

APP_NAME = "Memory Info"

local updateInterval = 1000  -- Update every 1000ms (reduced updates)
local timeSinceUpdate = 0
local firstDraw = true
local lastUsedWidth = 0  -- Track last bar width to avoid redraw

function setup()
  brosche.clear(brosche.rgb(0, 0, 0))
  brosche.log("Memory Info started")
  firstDraw = true
  lastUsedWidth = 0
end

function loop(dt)
  -- Accumulate time
  timeSinceUpdate = timeSinceUpdate + dt

  -- Only update display every 1000ms to reduce load
  if timeSinceUpdate < updateInterval then
    return
  end
  timeSinceUpdate = 0

  local mem = brosche.meminfo()

  -- Force garbage collection AFTER getting meminfo
  brosche.gc()

  -- Only clear screen and draw title on first draw
  if firstDraw then
    brosche.fill(brosche.rgb(0, 0, 0))
    brosche.text(120, 30, "RAM Status", brosche.rgb(255, 255, 255), brosche.rgb(0, 0, 0))

    -- Draw static bar background once
    local bar_width = 180
    local bar_height = 20
    local bar_x = (240 - bar_width) / 2
    local bar_y = 160
    brosche.rect(bar_x, bar_y, bar_width, bar_height, brosche.rgb(50, 50, 50))

    firstDraw = false
  end

  -- Pre-calculate values to reduce string operations
  local used_pct = math.floor((mem.used * 100) / mem.total)
  local free_kb = math.floor(mem.free / 1024)
  local total_kb = math.floor(mem.total / 1024)
  local used_kb = math.floor(mem.used / 1024)
  local largest_kb = math.floor(mem.largest_block / 1024)

  -- Display memory stats with black background
  local black = brosche.rgb(0, 0, 0)

  -- Use shorter format strings to reduce memory
  brosche.text(120, 70, "Total: " .. total_kb .. " KB    ", brosche.rgb(200, 200, 200), black)
  brosche.text(120, 90, "Used: " .. used_kb .. " KB    ", brosche.rgb(255, 100, 100), black)
  brosche.text(120, 110, "Free: " .. free_kb .. " KB    ", brosche.rgb(100, 255, 100), black)
  brosche.text(120, 130, "Block: " .. largest_kb .. " KB    ", brosche.rgb(100, 200, 255), black)

  -- Update bar graph only if width changed (reduce flickering)
  local bar_width = 180
  local bar_height = 20
  local bar_x = (240 - bar_width) / 2
  local bar_y = 160
  local used_width = math.floor((bar_width * mem.used) / mem.total)

  if used_width ~= lastUsedWidth then
    -- Redraw background
    brosche.rect(bar_x, bar_y, bar_width, bar_height, brosche.rgb(50, 50, 50))

    -- Draw used bar
    if used_pct > 80 then
      brosche.rect(bar_x, bar_y, used_width, bar_height, brosche.rgb(255, 50, 50))
    elseif used_pct > 60 then
      brosche.rect(bar_x, bar_y, used_width, bar_height, brosche.rgb(255, 200, 50))
    else
      brosche.rect(bar_x, bar_y, used_width, bar_height, brosche.rgb(50, 255, 50))
    end

    lastUsedWidth = used_width
  end
end

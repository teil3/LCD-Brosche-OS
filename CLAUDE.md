# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Development Workflow

### Initial Setup (one-time)
```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "TFT_eSPI" "TJpg_Decoder"
```

### Standard Build & Flash Cycle
```bash
# 1. Build with 16MB partitions (REQUIRED before first upload)
arduino-cli compile -b esp32:esp32:esp32 \
  --build-property build.flash_size=16MB \
  --build-property build.partitions=partitions.csv \
  --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT" \
  --build-property compiler.c.extra_flags="-DSMOOTH_FONT" \
  --build-path build-16m .

# 2. Upload to device
arduino-cli upload -b esp32:esp32:esp32 --input-dir build-16m -p /dev/ttyACM0

# 3. Monitor serial output
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

**Critical:** Always run the full compile command with `--build-path build-16m` before upload to ensure the custom 8MB LittleFS partition table is applied. The upload command uses the last built artifact from `build-16m/`.

## Architecture Overview

### App-Based Plugin System
The firmware implements a simple OS with an app manager that orchestrates multiple independent apps:

- **App Interface** (`Core/App.h`): Abstract base class defining lifecycle hooks:
  - `init()` - Called when app becomes active
  - `tick(delta_ms)` - Called every loop for time-based updates
  - `draw()` - Render current frame
  - `onButton(index, event)` - Handle button events (index=1 for BTN1, 2 for BTN2)
  - `shutdown()` - Cleanup when app is switched away

- **AppManager** (`Core/AppManager.h`): Manages app lifecycle, switching, and event dispatch. Apps are registered in `ESP32-BoardOS.ino` setup() via `appman.add()`.

### Hardware Abstraction Layers

- **Gfx** (`Core/Gfx.h`): Initializes TFT (GC9A01) and SD card. Global `tft` object available to all apps. **CRITICAL:** SD must be initialized before TFT to avoid SPI conflicts.

- **Storage** (`Core/Storage.h`): Manages LittleFS (`/slides` directory in flash) and SD card access. Handles automatic formatting on first boot.

- **Buttons** (`Core/Buttons.h`): Debouncing and gesture detection (Single/Double/Triple/Long press). Global instances `btn1`, `btn2` polled in main loop.

- **TextRenderer** (`Core/TextRenderer.h`): Renders text with outline/shadow effects. Use `TextRenderer::drawCentered()` for status overlays.

- **BleImageTransfer & SerialImageTransfer**: Event-driven image upload handlers. Events are polled in main loop and forwarded to active app (currently only SlideshowApp handles these).

### Main Loop Flow (ESP32-BoardOS.ino)
1. Poll BTN1 (system control: app switching)
2. Poll BTN2 (dispatch to active app)
3. SerialImageTransfer::tick() + pump USB events
4. appman.tick(dt) - update active app
5. appman.draw() - render active app
6. SerialImageTransfer::tick() again + BleImageTransfer::tick() + pump events
7. delay(5) - keep loop responsive

**Why dual tick calls?** Ensures USB/BLE transfers get frequent polling while keeping the loop responsive for button events.

### Arduino IDE Build Quirk
Arduino CLI doesn't auto-compile .cpp files in subdirectories. Therefore, `ESP32-BoardOS.ino` force-includes all `.cpp` files at the end (lines 212-226). When adding new modules, you MUST add corresponding `#include` statements there.

## Wireless Image Transfer

The firmware supports two wireless/wired methods for transferring JPEG images directly to the device:

### BLE Image Transfer (Implemented & Working)
**Implementation:** `Core/BleImageTransfer.h/cpp`

The ESP32 acts as a BLE peripheral with a custom GATT service for receiving JPEG images wirelessly from a web browser (WebBluetooth API):

**GATT Service Structure:**
- Service UUID: `12345678-1234-1234-1234-1234567890ab`
- `chunk` characteristic: Receives image data in 20-byte chunks (BLE MTU limitation)
- `control` characteristic: Handles start/end signals and status messages

**Transfer Protocol:**
1. Browser sends start signal with filename and file size
2. Image data is transmitted in 20-byte chunks via `chunk` characteristic
3. ESP32 assembles chunks and writes to LittleFS (`/slides` directory)
4. Browser sends end signal when complete
5. ESP32 forwards events to active app (currently SlideshowApp)

**Performance:**
- ESP32 Classic (BLE 4.2): ~10-25 KB/s → 1-2 seconds for 20KB JPEG
- ESP32-S3/C3 (BLE 5): ~30-80 KB/s → 0.3-0.7 seconds for 20KB JPEG

**Web Tool Integration:**
The browser-based "Bildaufbereiter" tool (hosted on GitHub Pages) at `https://teil3.github.io/LCD-Brosche-OS/tools/bildaufbereiter/` provides:
- Image cropping/resizing to 240×240 px
- JPEG encoding
- Direct BLE transfer via WebBluetooth
- Also supports USB transfer via WebSerial (921600 baud)

**Browser Compatibility:**
WebBluetooth is supported in Chrome, Edge, Opera, Brave, and Android Chrome. **Safari/iOS does not support WebBluetooth.** HTTPS or localhost is required for security.

### USB/Serial Image Transfer (Partially Implemented)
**Implementation:** `Core/SerialImageTransfer.h/cpp`
**Status:** Code exists but not fully tested/finalized

Alternative wired transfer method via USB serial at 921600 baud using the **Web Serial API**:

**Protocol:**
1. Browser sends `START <filesize>\n` header
2. Image data is transmitted in 1KB chunks (1024 bytes)
3. Browser sends `END\n` signal when complete
4. ESP32 assembles data and writes to LittleFS

**Browser Compatibility (Web Serial API):**
- ✅ Chrome, Edge, Brave, Opera on Windows/macOS/Linux (Desktop only)
- ❌ Firefox, Safari (not supported)
- ❌ Android/iOS (mobile browsers don't support Web Serial)
- Requires HTTPS or localhost for security

**Advantages over BLE:**
- Usually faster than BLE (921600 baud vs. ~10-80 KB/s)
- More reliable connection
- No pairing required
- Works on systems where Bluetooth is problematic (some Linux/macOS setups)

**Linux Note:** User must be in `dialout` group for serial port access:
```bash
sudo usermod -a -G dialout $USER
```

The same "Bildaufbereiter" web tool supports both BLE and USB transfer methods.

**Event Handling:**
Both transfer methods are event-driven. The main loop polls `BleImageTransfer::tick()` and `SerialImageTransfer::tick()` twice per iteration to ensure responsive transfers. Events (Started, Completed, Error, Aborted) are pumped and forwarded to the active app.

Currently, only `SlideshowApp` handles these events via callbacks:
- `onBleTransferStarted/Completed/Error/Aborted()`
- `onUsbTransferStarted/Completed/Error/Aborted()`

**To add transfer support to a new app:**
1. Check if active app pointer matches your app instance in pump functions
2. Implement event handler methods (see SlideshowApp for reference)
3. Handle progress display, error messages, and list refresh after completion

## Adding New Apps

### Traditional C++ Apps (Statically Compiled)

1. Create `Apps/YourApp.h` and `Apps/YourApp.cpp`
2. Inherit from `App` base class
3. Implement all virtual methods (name, init, tick, draw, onButton, shutdown)
4. Add `#include "Apps/YourApp.h"` to ESP32-BoardOS.ino
5. Create instance: `YourApp app_yourapp;`
6. Register in setup(): `appman.add(&app_yourapp);`
7. Force-compile: Add `#include "Apps/YourApp.cpp"` at bottom of .ino

### Plugin Apps (Function Table Based) ⚠️ EXPERIMENTAL

A new plugin architecture allows apps to be developed against a stable ABI (Application Binary Interface) using a function table pattern. This enables future possibilities like dynamic loading from `.bin` files.

**Status:** Phase 1 (Static Plugins) is **WORKING**. Phase 2 (Dynamic Loading) is **IN PROGRESS**.

#### Architecture Components

**AppAPI Interface** (`Core/AppAPI.h`):
- Defines stable function table with core functionality
- Version field for ABI compatibility checking
- Provides access to: TFT display, TextRenderer, logging, timing

**PluginAppVTable** (`Core/AppAPI.h`):
- Standard vtable structure for plugin apps
- Required functions: init, onActivate, tick, draw, onButton, shutdown, getName
- All plugins must export this vtable with C linkage

**StaticPluginApp** (`Core/StaticPluginApp.h/cpp`):
- Wrapper that adapts plugin vtable to App interface
- Used for statically-linked plugins (Phase 1)
- Zero overhead - just function pointer indirection

**PluginApp** (`Core/PluginApp.h/cpp`):
- Future: Loads `.bin` files from LittleFS at runtime
- Status: Structure in place, relocation not yet implemented

#### Phase 1: Static Plugin Development (WORKING)

Create plugin apps that use only the AppAPI interface:

**Example:** See `Apps/HelloWorldApp.cpp`

```cpp
#include "Core/AppAPI.h"

static const AppAPI* api = nullptr;

extern "C" {
  void plugin_init(const AppAPI* core_api) {
    api = core_api;
  }

  void plugin_draw() {
    api->drawCentered(100, "Hello Plugin", 0xFFFF, 0x0000);
  }

  // ... other lifecycle functions

  const PluginAppVTable helloworld_vtable = {
    .init = plugin_init,
    .onActivate = plugin_onActivate,
    .tick = plugin_tick,
    .draw = plugin_draw,
    .onButton = plugin_onButton,
    .shutdown = plugin_shutdown,
    .getName = plugin_getName
  };
}

extern "C" const PluginAppVTable* getHelloWorldVTable() {
  return &helloworld_vtable;
}
```

**Register in ESP32-BoardOS.ino:**
```cpp
extern "C" const PluginAppVTable* getHelloWorldVTable();
StaticPluginApp app_helloworld(getHelloWorldVTable());

void setup() {
  // ...
  appman.add(&app_helloworld);
  // ...
}
```

**Benefits:**
- Apps are decoupled from core implementation
- Only depends on AppAPI interface
- Future-proof for dynamic loading
- Same performance as regular apps

#### Phase 2: Dynamic Plugin Loading (IN PROGRESS)

**Goal:** Load apps from `.bin` files at runtime without recompiling firmware.

**Build System:** `plugins/` directory contains separate build environment:
- `plugins/Makefile` - Compiles plugins with `-fPIC` for position independence
- `plugins/plugin.ld` - Custom linker script placing vtable at offset 0
- `plugins/runtime.c` - Minimal libc functions (memcpy, memset, memmove)
- `plugins/HelloWorldPlugin.cpp` - Standalone plugin example

**Building Plugins:**
```bash
cd plugins
make HelloWorldPlugin
# Creates: ../data/apps/HelloWorld.bin (835 bytes)
```

**Plugin Binary Format:**
- Position-independent code (PIC)
- VTable at offset 0 for easy discovery
- No external dependencies (self-contained)
- Uses only AppAPI for core access

**What Works:**
✅ Build system compiles plugins to `.bin` files
✅ Linker script places vtable at known location
✅ AppAPI interface fully functional
✅ StaticPluginApp wrapper tested and working

**What's Missing (TODO):**
❌ **Relocation:** Convert relative offsets to absolute IRAM addresses
❌ **PluginApp loader:** Finish implementing binary loader
❌ **Auto-discovery:** Scan `/apps/*.bin` at boot
❌ **Upload system:** Transfer `.bin` files via USB/BLE

**Technical Challenges:**
- ESP32 has no MMU for virtual addressing
- No standard dynamic linker
- Must manually relocate function pointers
- Limited IRAM for code (plugins load to IRAM for execution)

**See:** `plugins/TODO.md` for detailed implementation roadmap.

**Current Recommendation:** Use StaticPluginApp for new apps until Phase 2 is complete.

## Hardware Constraints & Rules

### Pin Configuration (Config.h)
**DO NOT CHANGE** without explicit confirmation:
- SPI: SCK=14, MOSI=15, MISO=2
- SD_CS=13, TFT_CS=5, TFT_BL=22
- BTN1=32, BTN2=33 or 35 (GPIO35 has no internal pull-up)

### Initialization Order
1. LittleFS mount (with auto-format fallback)
2. SD card begin (via Gfx)
3. TFT begin (via Gfx)

**Rationale:** Both SD and TFT share SPI bus. SD must claim bus first to avoid conflicts.

### JPEG Handling
- Use `TJpgDec.setSwapBytes(true)` (already configured in Gfx)
- Images must be baseline JPEG (non-progressive) at 204x240 or 240x240 pixels
- JPEG signature check: first 2 bytes must be `0xFF 0xD8` (SOI marker)

### Button Responsiveness
**NEVER** use long `delay()` calls in app logic. Use tick()-based timing instead. The main loop must remain responsive (<10ms per iteration) for button detection.

### Status Overlays
Use `TextRenderer::drawCentered()` with outline for toast messages. Follow with `pauseUntil(millis() + 1000)` to keep message visible ~1 second before clearing.

## File Organization & Tool Preferences

### Search & Find (from AGENTS.md)
- **Content search:** Use `rg "pattern"` (ripgrep respects .gitignore, much faster than grep)
- **File finding:** Use `fd` or `fdfind` (Debian/Ubuntu) instead of find
- **JSON parsing:** Use `jq`

## Web Tool Version Management

The Bildaufbereiter web tool (`tools/bildaufbereiter/index.html`) uses semantic versioning:
- Version is defined at line ~127: `const TOOL_VERSION = 'V0.X';`
- **IMPORTANT:** When making changes to `index.html`, increment ONLY the minor version (the number after the dot)
- **NEVER change the major version** (the number before the dot) - only the user can do that or will explicitly tell you
- **CRITICAL:** ALWAYS increment the version BEFORE asking the user to test changes
  - This allows the user to verify the browser reload worked by checking the version number in the UI
  - The version is displayed in the UI alongside the mozjpeg version

### git Status Note
The modified file `Core/SerialImageTransfer.cpp` is from recent development work. Check git diff before committing to avoid losing uncommitted changes.

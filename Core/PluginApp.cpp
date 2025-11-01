#include "PluginApp.h"
#include "AppAPIImpl.h"
#include <esp_heap_caps.h>
#include <SD.h>

// Reference to shared core API
AppAPI& PluginApp::coreAPI_ = AppAPIImpl::coreAPI;

PluginApp::PluginApp(const char* binPath) : binPath_(binPath) {
  Serial.printf("[PluginApp] Constructor: %s\n", binPath);
  loaded_ = loadPlugin_();
  if (!loaded_) {
    Serial.printf("[PluginApp] Failed to load: %s\n", binPath);
    appName_ = "Failed Plugin";
  }
}

PluginApp::~PluginApp() {
  unloadPlugin_();
}

bool PluginApp::loadPlugin_() {
  Serial.printf("[PluginApp] Loading from: %s\n", binPath_.c_str());

  // Try to open from LittleFS first, then SD card
  File f;
  bool fromSD = false;

  if (LittleFS.exists(binPath_.c_str())) {
    f = LittleFS.open(binPath_.c_str(), FILE_READ);
    Serial.println("[PluginApp] Opening from LittleFS");
  } else if (SD.exists(binPath_.c_str())) {
    f = SD.open(binPath_.c_str(), FILE_READ);
    fromSD = true;
    Serial.println("[PluginApp] Opening from SD card");
  } else {
    Serial.printf("[PluginApp] File not found in LittleFS or SD: %s\n", binPath_.c_str());
    return false;
  }

  if (!f) {
    Serial.printf("[PluginApp] Failed to open file: %s\n", binPath_.c_str());
    return false;
  }

  // Double-check file is valid
  if (!f.available()) {
    Serial.printf("[PluginApp] File not available (invalid handle): %s\n", binPath_.c_str());
    f.close();
    return false;
  }

  pluginSize_ = f.size();
  Serial.printf("[PluginApp] File size: %u bytes\n", (unsigned)pluginSize_);

  if (pluginSize_ == 0 || pluginSize_ > 128 * 1024) {
    Serial.printf("[PluginApp] Invalid size: %u\n", (unsigned)pluginSize_);
    f.close();
    return false;
  }

  // Check available memory types (keep file open to avoid SD re-open which triggers watchdog)
  size_t freeIRAM = heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT);
  size_t freeDRAM = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

  Serial.print("[PluginApp] Free IRAM: ");
  Serial.print(freeIRAM);
  Serial.println(" bytes");
  Serial.print("[PluginApp] Free DRAM: ");
  Serial.print(freeDRAM);
  Serial.println(" bytes");

  // Skip IRAM allocation if no space (heap_caps_malloc scans entire heap → slow)
  if (freeIRAM > pluginSize_) {
    Serial.println("[PluginApp] Trying IRAM allocation...");
    pluginMemory_ = heap_caps_malloc(pluginSize_, MALLOC_CAP_IRAM_8BIT);
  }

  if (!pluginMemory_) {
    // IRAM full or skipped, use DRAM (can execute via instruction cache)
    Serial.println("[PluginApp] Using DRAM...");
    pluginMemory_ = heap_caps_malloc(pluginSize_, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  Serial.print("[PluginApp] malloc returned: ");
  if (pluginMemory_ == nullptr) {
    Serial.println("NULL");
    Serial.println("[PluginApp] Failed to allocate memory");
    return false;
  }
  Serial.println("OK");

  // Get and validate address
  uintptr_t addr = (uintptr_t)pluginMemory_;
  Serial.print("[PluginApp] Address: 0x");
  Serial.println(addr, HEX);

  // Identify memory region
  if (addr >= 0x40070000 && addr < 0x400A0000) {
    Serial.println("[PluginApp] -> In IRAM (writable, executable)");
  } else if (addr >= 0x3FF00000 && addr < 0x40000000) {
    Serial.println("[PluginApp] -> In DRAM (writable, executable via cache)");
  } else if (addr >= 0x40000000 && addr < 0x40070000) {
    Serial.println("[PluginApp] ERROR: In Flash (read-only!)");
    heap_caps_free(pluginMemory_);
    pluginMemory_ = nullptr;
    return false;
  } else if (addr >= 0x400A0000 && addr < 0x40100000) {
    Serial.println("[PluginApp] ERROR: In ROM/Code (read-only!)");
    heap_caps_free(pluginMemory_);
    pluginMemory_ = nullptr;
    return false;
  } else {
    Serial.println("[PluginApp] WARNING: Unknown memory region");
  }

  Serial.println("[PluginApp] Memory allocated and validated");

  // Read entire file at once (file is still open from initial open)
  Serial.printf("[PluginApp] Reading %u bytes...\n", (unsigned)pluginSize_);
  f.seek(0); // Reset file position to beginning

  // Read directly into allocated memory
  size_t bytesRead = f.read((uint8_t*)pluginMemory_, pluginSize_);
  f.close();

  if (bytesRead != pluginSize_) {
    Serial.println("[PluginApp] Read mismatch");
    unloadPlugin_();
    return false;
  }

  Serial.println("[PluginApp] Read OK");

  // Find VTable in binary (with relative offsets)
  if (!findVTable_()) {
    Serial.println("[PluginApp] VTable not found");
    unloadPlugin_();
    return false;
  }

  Serial.println("[PluginApp] VTable found");

  // Relocate VTable function pointers to absolute addresses
  if (!relocateVTable_()) {
    Serial.println("[PluginApp] Relocation failed");
    unloadPlugin_();
    return false;
  }

  Serial.println("[PluginApp] Relocation done");

  // Call plugin init
  if (vtable_.init) {
    vtable_.init(&coreAPI_);
  }

  // Get plugin name
  if (vtable_.getName) {
    appName_ = String(vtable_.getName());
  } else {
    appName_ = "Unknown Plugin";
  }

  Serial.printf("[PluginApp] Successfully loaded: %s\n", appName_.c_str());

  return true;
}

void PluginApp::unloadPlugin_() {
  if (pluginMemory_) {
    heap_caps_free(pluginMemory_);
    pluginMemory_ = nullptr;
  }
  pluginSize_ = 0;
  loaded_ = false;
}

bool PluginApp::findVTable_() {
  if (!pluginMemory_ || pluginSize_ < sizeof(PluginAppVTable)) {
    return false;
  }

  // Strategy 1: VTable at beginning of binary (placed by linker script)
  PluginAppVTable* candidate = (PluginAppVTable*)pluginMemory_;

  // Sanity check: Function pointers should be either NULL or small relative offsets
  const uintptr_t MAX_RELATIVE_OFFSET = 0x10000; // 64KB max plugin size

  auto isValidOffset = [MAX_RELATIVE_OFFSET](void* ptr) -> bool {
    if (!ptr) return true;
    uintptr_t addr = (uintptr_t)ptr;
    return (addr < MAX_RELATIVE_OFFSET);
  };

  // Check if critical function pointers look like relative offsets
  if (isValidOffset((void*)candidate->init) &&
      isValidOffset((void*)candidate->draw) &&
      isValidOffset((void*)candidate->getName)) {

    memcpy(&vtable_, candidate, sizeof(PluginAppVTable));

    return true;
  }

  return false;
}

bool PluginApp::relocateVTable_() {
  if (!pluginMemory_) {
    Serial.println("[PluginApp] No plugin memory to relocate");
    return false;
  }

  uintptr_t base = (uintptr_t)pluginMemory_;

  // Relocate all function pointers from relative offsets to absolute addresses
  // Strategy: If pointer value is < 0x1000, it's likely a relative offset
  const uintptr_t OFFSET_THRESHOLD = 0x1000;

  auto relocate = [base, OFFSET_THRESHOLD](void** funcPtr) {
    if (!funcPtr || !*funcPtr) return;
    uintptr_t addr = (uintptr_t)(*funcPtr);
    if (addr < OFFSET_THRESHOLD) {
      *funcPtr = (void*)(base + addr);
    }
  };

  // Relocate each function pointer in the VTable (no Serial output here!)
  relocate((void**)&vtable_.init);
  relocate((void**)&vtable_.onActivate);
  relocate((void**)&vtable_.tick);
  relocate((void**)&vtable_.draw);
  relocate((void**)&vtable_.onButton);
  relocate((void**)&vtable_.shutdown);
  relocate((void**)&vtable_.getName);

  return true;
}

const char* PluginApp::name() const {
  return appName_.c_str();
}

void PluginApp::init() {
  if (!loaded_ || !vtable_.onActivate) return;
  vtable_.onActivate();
}

void PluginApp::tick(uint32_t delta_ms) {
  if (!loaded_ || !vtable_.tick) return;
  vtable_.tick(delta_ms);
}

void PluginApp::onButton(uint8_t index, BtnEvent e) {
  if (!loaded_ || !vtable_.onButton) return;
  vtable_.onButton(index, static_cast<uint8_t>(e));
}

void PluginApp::draw() {
  if (!loaded_ || !vtable_.draw) return;
  vtable_.draw();
}

void PluginApp::shutdown() {
  if (!loaded_ || !vtable_.shutdown) return;
  vtable_.shutdown();
}

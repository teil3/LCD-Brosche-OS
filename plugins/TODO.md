# Plugin System TODO - Phase 2 Implementation Roadmap

## 🎯 Ziel

Dynamisches Laden von `.bin` Plugin-Dateien zur Laufzeit ohne Firmware-Neukompilierung.

## ✅ Bereits erledigt (Phase 1)

- [x] AppAPI Function Table Interface (`Core/AppAPI.h`)
- [x] PluginAppVTable Definition
- [x] StaticPluginApp Wrapper (`Core/StaticPluginApp.h/cpp`)
- [x] Build-System für Plugins (`plugins/Makefile`)
- [x] Linker-Script (`plugins/plugin.ld`)
- [x] Runtime-Support (`plugins/runtime.c`)
- [x] HelloWorld Plugin statisch funktionsfähig
- [x] HelloWorldPlugin.cpp als `.bin` kompiliert (835 bytes)
- [x] Dokumentation (CLAUDE.md, README.md)

## 🚧 Phase 2: Verbleibende Aufgaben

### 1. Binary Relocation implementieren ⭐ KRITISCH

**Problem:**
Die kompilierten `.bin` Dateien enthalten relative Offsets, keine absoluten Adressen.

Beispiel aus `HelloWorld.bin`:
```
Offset 0x00:  VTable
  +0x00: init_ptr    = 0x24      (Offset zu plugin_init)
  +0x04: activate_ptr = 0x44     (Offset zu plugin_onActivate)
  ...
```

Wenn Binary nach 0x40080000 geladen wird:
```
init_ptr sollte sein:    0x40080024 (base + offset)
activate_ptr sollte sein: 0x40080044 (base + offset)
```

**Lösung:**
```cpp
// In PluginApp::loadPlugin_()
bool PluginApp::relocateVTable_() {
  if (!pluginMemory_) return false;

  uintptr_t base = (uintptr_t)pluginMemory_;
  PluginAppVTable* vtable = (PluginAppVTable*)pluginMemory_;

  // Relocate all function pointers
  if ((uintptr_t)vtable->init < 0x1000) {
    vtable->init = (void(*)(const AppAPI*))(base + (uintptr_t)vtable->init);
  }
  if ((uintptr_t)vtable->onActivate < 0x1000) {
    vtable->onActivate = (void(*)())(base + (uintptr_t)vtable->onActivate);
  }
  // ... für alle anderen Funktionszeiger

  return true;
}
```

**Test:**
- Lade `HelloWorld.bin` in IRAM
- Relocate VTable
- Rufe Funktionen auf
- Prüfe ob sie korrekt laufen

**Datei:** `Core/PluginApp.cpp` Zeile ~100

---

### 2. PluginApp.cpp finalisieren

**Aktuelle Implementierung:**
- ✅ Load binary from LittleFS
- ✅ Allocate IRAM memory
- ⚠️ VTable finding (basic implementation)
- ❌ Relocation (missing!)

**TODO:**
- [ ] Implement `relocateVTable_()` (siehe oben)
- [ ] Test mit `HelloWorld.bin`
- [ ] Error handling verbessern
- [ ] Memory leak prevention (ensure unload on errors)

**Test-Plan:**
```cpp
// In ESP32-BoardOS.ino setup():
PluginApp dynamicPlugin("/apps/HelloWorld.bin");
if (dynamicPlugin.isLoaded()) {
  appman.add(&dynamicPlugin);
  Serial.println("Dynamic plugin loaded!");
} else {
  Serial.println("Failed to load dynamic plugin");
}
```

**Datei:** `Core/PluginApp.cpp`

---

### 3. Plugin Auto-Discovery

Automatisches Finden und Laden aller `.bin` Dateien beim Boot.

**Implementation:**
```cpp
// In ESP32-BoardOS.ino setup():
void discoverPlugins() {
  File root = LittleFS.open("/apps");
  if (!root || !root.isDirectory()) {
    Serial.println("[Plugins] /apps directory not found");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (name.endsWith(".bin")) {
      String path = "/apps/" + name;
      Serial.printf("[Plugins] Found: %s\n", path.c_str());

      PluginApp* plugin = new PluginApp(path.c_str());
      if (plugin->isLoaded()) {
        appman.add(plugin);
        Serial.printf("[Plugins] Loaded: %s\n", plugin->name());
      } else {
        Serial.printf("[Plugins] Failed: %s\n", path.c_str());
        delete plugin;
      }
    }
    file = root.openNextFile();
  }
}

void setup() {
  // ... existing setup code
  discoverPlugins();
  appman.begin();
}
```

**TODO:**
- [ ] Implement `discoverPlugins()` function
- [ ] Handle memory management (plugins created with `new`)
- [ ] Add max plugin limit (avoid IRAM exhaustion)
- [ ] Priority/ordering system (alphabetical?)

**Datei:** `ESP32-BoardOS.ino` oder neue `Core/PluginManager.h/cpp`

---

### 4. Plugin Upload System

Plugins via USB/BLE hochladen (ähnlich wie Bilder).

**Option A: Erweitere SerialImageTransfer**
```cpp
// Neuer Befehl: UPLOAD_PLUGIN <filename> <size>
// Upload zu /apps/<filename>.bin

void SerialImageTransfer::handlePluginUpload(const char* filename, size_t size) {
  String path = "/apps/";
  path += filename;
  if (!path.endsWith(".bin")) path += ".bin";

  // Upload wie bei Images
  // Nach Upload: AppManager neu scannen
}
```

**Option B: Separater PluginTransfer**
- Eigenes Protokoll
- Eigene BLE/Serial Handler
- Upload nur von `.bin` Dateien

**TODO:**
- [ ] Entscheidung A oder B
- [ ] Implementierung
- [ ] Web-Tool Update (Bildaufbereiter)
- [ ] Test: Upload → Auto-Discovery → Plugin läuft

**Dateien:**
- `Core/SerialImageTransfer.cpp` (Option A)
- `Core/PluginTransfer.h/cpp` (Option B)
- `tools/bildaufbereiter/index.html` (Web-Tool)

---

### 5. AppManager Erweiterungen

**Dynamisches Nachladen:**
```cpp
class AppManager {
public:
  void reloadPlugins(); // Rescan /apps/*.bin
  void unloadPlugin(const char* name); // Remove specific plugin
  void hotReload(); // Reload all dynamic plugins
};
```

**TODO:**
- [ ] Implement Plugin reload ohne Neustart
- [ ] Handle cleanup von alten Plugins
- [ ] UI für Plugin-Management (optional)

**Datei:** `Core/AppManager.h/cpp`

---

## 🔬 Testing & Validation

### Phase 2 Test-Checklist

- [ ] **Relocation Test**
  - [ ] HelloWorld.bin lädt erfolgreich
  - [ ] VTable korrekt relocatet
  - [ ] Alle Funktionen funktional

- [ ] **Memory Test**
  - [ ] Keine Leaks bei Load/Unload
  - [ ] Mehrere Plugins parallel
  - [ ] IRAM Limits respektiert

- [ ] **Discovery Test**
  - [ ] Findet alle `.bin` in `/apps/`
  - [ ] Lädt nur valide Plugins
  - [ ] Überspringt korrupte Files

- [ ] **Upload Test**
  - [ ] USB Upload funktioniert
  - [ ] BLE Upload funktioniert
  - [ ] Auto-Discovery nach Upload

- [ ] **Integration Test**
  - [ ] Plugin läuft stabil
  - [ ] Button Events funktionieren
  - [ ] App-Wechsel sauber
  - [ ] Shutdown/Init Cycle OK

---

## 🐛 Bekannte Probleme & Lösungen

### Problem 1: VTable Relocation
**Status:** Ungelöst
**Impact:** Kritisch - ohne Relocation laufen Plugins nicht
**Lösung:** Siehe Abschnitt 1 oben

### Problem 2: IRAM Limits
**Problem:** Plugins werden in IRAM geladen (begrenzt auf ~100-120 KB frei)
**Lösung:**
- Max 3-4 Plugins gleichzeitig (~30KB each)
- Oder: Lazy loading (nur aktive Plugins in IRAM)

### Problem 3: String Handling
**Problem:** Arduino `String` nicht in Plugins verfügbar (nostdlib)
**Lösung:** Eigene String-Funktionen in `runtime.c` (bereits teilweise vorhanden)

### Problem 4: No stdlib
**Problem:** Keine `printf`, `sprintf` etc.
**Lösung:**
- Eigene `snprintf` Implementierung
- Oder: AppAPI erweitern mit formatString()

---

## 📊 Geschätzter Aufwand

| Task | Geschätzter Aufwand | Priorität |
|------|---------------------|-----------|
| Relocation implementieren | 2-3h | ⭐⭐⭐ Kritisch |
| PluginApp testen | 1h | ⭐⭐⭐ Hoch |
| Auto-Discovery | 1h | ⭐⭐ Mittel |
| Upload System | 2-3h | ⭐⭐ Mittel |
| AppManager Erweiterungen | 1h | ⭐ Niedrig |
| Tests & Debugging | 2-4h | ⭐⭐ Mittel |

**Total:** ca. 9-14 Stunden

---

## 🎓 Technische Referenzen

### Linker & Relocation
- ESP-IDF Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/linker-script-generation.html
- Position Independent Code: https://wiki.osdev.org/Position_Independent_Code
- ELF Relocation: https://refspecs.linuxbase.org/elf/gabi4+/ch4.reloc.html

### ESP32 Memory
- IRAM vs DRAM: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/memory-types.html
- heap_caps: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html

---

## 🚀 Quick Start für Entwickler

### Jetzt testen (Static):
```bash
# 1. Plugin als static einbinden (funktioniert jetzt)
# Siehe Apps/HelloWorldApp.cpp

# 2. Build & Upload
arduino-cli compile ...
arduino-cli upload ...

# 3. Mit BTN1 zu "Hello World" App wechseln
```

### Phase 2 weitermachen:
```bash
# 1. Relocation implementieren
# → Bearbeite Core/PluginApp.cpp

# 2. Plugin binary kompilieren
cd plugins
make HelloWorldPlugin

# 3. Binary auf ESP32 kopieren (manuell via LittleFS upload)
# → Oder warten auf Upload-System

# 4. Test in ESP32-BoardOS.ino
```

---

## 📝 Notizen

- **Performance:** PIC hat minimalen Overhead (~5-10%), akzeptabel
- **Size:** HelloWorld.bin ist 835 bytes - sehr kompakt!
- **API Stability:** AppAPI v1 ist stabil, versioniert
- **Future:** Mehr AppAPI Funktionen bei Bedarf (Filesystem, JSON, etc.)

---

**Letzte Aktualisierung:** 2025-10-31
**Status:** Phase 1 Complete ✅ | Phase 2 In Progress 🚧

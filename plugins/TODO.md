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

## 🚧 Phase 2: AKTUELLER STAND (2025-11-01)

### ✅ Bereits implementiert seit letztem Update:

1. **VTable Relocation** ✅ FERTIG
   - `Core/PluginApp.cpp::relocateVTable_()` implementiert
   - Konvertiert relative Offsets zu absoluten Adressen
   - Threshold-basiert (< 0x1000 = relative offset)

2. **Auto-Discovery** ✅ FERTIG
   - `ESP32-BoardOS.ino::discoverPlugins()` implementiert
   - Scannt LittleFS:/apps/*.bin UND SD:/apps/*.bin
   - Maximale Plugins: 10 (MAX_DYNAMIC_PLUGINS)
   - Läuft in loop() statt setup() (boot watchdog avoidance)

3. **Dual Filesystem Support** ✅ FERTIG
   - LittleFS und SD card beide unterstützt
   - Priority: LittleFS first, dann SD fallback beim Lesen

4. **AppAPIImpl Refactoring** ✅ FERTIG
   - `Core/AppAPIImpl.h/cpp` erstellt
   - Shared AppAPI instance für StaticPluginApp und PluginApp
   - Duplicate symbol errors gelöst

### ⚠️ KRITISCHE BLOCKER - Plugin lädt NICHT

**Serial Output zeigt:**
```
[Plugins] Found: SD:/apps/HelloWorld.bin
E (7378) task_wdt: esp_task_wdt_reset(707): task not found  (x100+)
Guru Meditation Error: Core 0 panic'ed (Cache error)
Debug exception reason: Stack canary watchpoint triggered (IDLE0)
```

**Problem-Analyse:**

1. **Kein PluginApp Serial Output** ❌
   - Plugin discovery findet die Datei
   - ABER: PluginApp::loadPlugin_() gibt KEINEN Output
   - → Crash VOR erstem Serial.println in loadPlugin_()

2. **Task Watchdog Errors**
   - `yield()` calls versuchen Task im Watchdog zu resetten
   - Task ist NICHT registriert im Task Watchdog
   - → Alle yield() calls erzeugen Fehler

3. **Stack Overflow im IDLE Task**
   - "Stack canary watchpoint triggered (IDLE0)"
   - IDLE Task läuft auf Core 0
   - → Irgendetwas überschreibt IDLE Task Stack

4. **Zwei Watchdogs auf ESP32:**
   - **Task Watchdog (TWDT):** Überwacht Task-Ausführung
   - **Interrupt Watchdog (TG1WDT):** Überwacht Interrupt-Blocking
   - Frühere Resets waren TG1WDT_SYS_RESET

**Versuchte Lösungen (alle gescheitert):**

- ❌ disableCore0WDT() / disableCore1WDT() → Stack Overflow
- ❌ yield() calls überall → "task not found" spam
- ❌ setup() → loop() move → kein Unterschied
- ❌ Serial output reduzieren → kein Unterschied
- ❌ Single file read statt chunks → kein Unterschied
- ❌ MALLOC_CAP_IRAM_8BIT → returns NULL (0 bytes free!)
- ❌ MALLOC_CAP_INTERNAL → bekommt DRAM, execution unbekannt

**Memory Status:**
```
Free IRAM: 0 bytes (!!!)
Free DRAM: ~280KB
Flash Usage: 1300203 / 1310720 bytes (99%)
```

**Root Cause vermutlich:**
- IRAM komplett voll (99% Flash = kein IRAM mehr)
- Plugin kann nicht in IRAM geladen werden
- DRAM execution ungetestet (ESP32 hat I-Cache, könnte funktionieren)
- Watchdog disable destabilisiert System → Stack Overflow

---

### 🚧 Phase 2: Verbleibende Aufgaben

### 1. IRAM Exhaustion Problem lösen ⭐ KRITISCH

**Optionen:**

**Option A: Flash Usage reduzieren**
- Compiler optimizations prüfen (-Os vs -O2)
- Unused code eliminieren
- Große Konstanten in Flash statt RAM

**Option B: DRAM Execution testen**
- ESP32 hat Instruction Cache
- DRAM könnte executable sein via I-Cache
- Test: Plugin in DRAM laden ohne Watchdog disable

**Option C: External RAM (PSRAM)**
- Hardware upgrade nötig
- Nur wenn A+B scheitern

**Nächster Test:**
```cpp
// Ohne Watchdog disable, direkt DRAM allocation
pluginMemory_ = heap_caps_malloc(pluginSize_, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
// Dann: relocate und function call testen
```

---

### 2. yield() Calls entfernen

**Problem:** Main Loop Task ist nicht im Task Watchdog registriert

**Lösung:**
- Alle `yield()` calls in PluginApp.cpp entfernen
- ODER: `esp_task_wdt_add(xTaskGetCurrentTaskHandle())` vor Loading
- ODER: Task Watchdog komplett ignorieren (scheint nicht der Haupt-Issue)

---

### 3. Plugin Auto-Discovery (TEILWEISE FERTIG)

**Was funktioniert:**
- ✅ Findet `.bin` files in SD:/apps/
- ✅ Versucht PluginApp zu erstellen

**Was fehlt:**
- ❌ Plugin lädt nicht (siehe Blocker oben)
- [ ] Error recovery wenn Plugin crash
- [ ] Memory leak prevention

---

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

### Problem 1: IRAM Exhaustion ⭐ KRITISCH - AKTUELL
**Status:** Blockiert alle Plugin-Loading Tests
**Symptome:**
- Free IRAM: 0 bytes
- MALLOC_CAP_IRAM_8BIT returns NULL
- Flash usage: 99% (1300KB / 1310KB)
**Impact:** Plugin kann nicht in executable memory geladen werden
**Mögliche Lösungen:**
1. Compiler optimization tuning (-Os, LTO)
2. Code in Flash + IRAM_ATTR nur für hot paths
3. DRAM execution via I-Cache testen
4. Größere Apps auslagern zu Plugins (Platz schaffen)

### Problem 2: Stack Overflow beim Watchdog Disable ⭐ KRITISCH - AKTUELL
**Status:** Neue Entdeckung (2025-11-01)
**Symptome:**
- "Stack canary watchpoint triggered (IDLE0)"
- Cache error + Core 0 panic
- Tritt auf bei disableCore0WDT() / disableCore1WDT()
**Root Cause:** Unbekannt - möglicherweise:
- Interrupt disable destabilisiert System
- SD card operations brauchen Interrupts
- IDLE Task wird durch disable-Calls korrumpiert
**Workaround:** Watchdog disable NICHT verwenden

### Problem 3: Task Watchdog "task not found"
**Status:** Nervend aber nicht kritisch
**Symptome:** `E (xxxx) task_wdt: esp_task_wdt_reset(707): task not found`
**Ursache:** Main loop task ist nicht im Task Watchdog registriert
**Lösung:** Alle `yield()` calls entfernen aus PluginApp.cpp

### Problem 4: VTable Relocation
**Status:** ✅ GELÖST (2025-11-01)
**Lösung:** Implementiert in Core/PluginApp.cpp::relocateVTable_()

### Problem 5: String Handling
**Status:** Gelöst für Phase 2
**Problem:** Arduino `String` nicht in Plugins verfügbar (nostdlib)
**Lösung:** Plugins benutzen nur const char* und AppAPI Funktionen

### Problem 6: No stdlib
**Status:** Gelöst für Phase 2
**Problem:** Keine `printf`, `sprintf` etc.
**Lösung:**
- runtime.c bietet memcpy/memset/memmove
- AppAPI bietet logging (logInfo, logError)

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

**Letzte Aktualisierung:** 2025-11-01
**Status:** Phase 1 Complete ✅ | Phase 2 In Progress 🚧 | **BLOCKER: IRAM Exhaustion**

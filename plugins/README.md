# ESP32-BoardOS Plugin Build System

Dieses Verzeichnis enthält das Build-System für dynamisch ladbare Plugin-Apps.

## 🎯 Ziel

Apps als kleine `.bin` Dateien kompilieren, die zur Laufzeit ohne Neu-Kompilierung der Firmware geladen werden können - ähnlich wie Bilder über BLE/USB hochgeladen werden.

## 📊 Status

### ✅ Was funktioniert (Phase 1)

- **AppAPI Interface**: Stabile Schnittstelle für Plugins
- **StaticPluginApp**: Plugins statisch in Firmware einbinden
- **Build-System**: Kompiliert Plugins als Position-Independent Code
- **HelloWorld Plugin**: Funktionierendes Beispiel

### 🚧 In Arbeit (Phase 2)

- **Relocation**: Umwandlung relativer Adressen in absolute IRAM-Adressen
- **Dynamisches Laden**: `.bin` Files aus LittleFS laden und ausführen
- **Auto-Discovery**: Automatisches Finden von `/apps/*.bin` beim Boot
- **Upload-System**: Plugins via USB/BLE hochladen

## 🏗️ Architektur

### Function Table Pattern

Plugins kommunizieren mit dem Core nur über eine Function Table (`AppAPI`):

```
┌──────────────┐
│  Core System │ ◄──┐
│              │    │
│  - TFT       │    │  AppAPI
│  - Storage   │    │  (Function Table)
│  - Buttons   │    │
└──────────────┘    │
                    │
┌──────────────┐    │
│  Plugin App  │ ───┘
│              │
│  - init()    │
│  - draw()    │
│  - tick()    │
└──────────────┘
```

**Vorteile:**
- Plugin kennt nur die API, nicht die Implementierung
- API bleibt stabil auch wenn Core sich ändert
- Ermöglicht zukünftig dynamisches Laden

## 🛠️ Plugin erstellen

### Dateistruktur

```
plugins/
├── Makefile              # Build-System
├── plugin.ld             # Linker-Script
├── runtime.c             # Minimal-Libc (memcpy, etc.)
├── HelloWorldPlugin.cpp  # Beispiel-Plugin
└── build/                # Build-Artefakte
```

### Hello World Plugin

```cpp
#include "Core/AppAPI.h"

static const AppAPI* api = nullptr;

extern "C" {

void plugin_init(const AppAPI* core_api) {
  api = core_api;
}

void plugin_draw() {
  if (!api) return;
  api->drawCentered(100, "Hello from Plugin!", 0xFFFF, 0x0000);
}

// ... andere Lifecycle-Funktionen

// VTable muss an Offset 0 stehen
__attribute__((section(".plugin_vtable"), used))
const PluginAppVTable plugin_vtable = {
  .init = plugin_init,
  .onActivate = plugin_onActivate,
  .tick = plugin_tick,
  .draw = plugin_draw,
  .onButton = plugin_onButton,
  .shutdown = plugin_shutdown,
  .getName = plugin_getName
};

} // extern "C"
```

### Kompilieren

```bash
cd plugins
make HelloWorldPlugin
```

**Output:** `../data/apps/HelloWorld.bin` (ca. 800 bytes)

## 📁 Dateien

| Datei | Beschreibung |
|-------|-------------|
| `Makefile` | Build-System für Plugins |
| `plugin.ld` | Linker-Script (VTable an Offset 0, PIC) |
| `runtime.c` | Minimal-Libc (memcpy, memset, memmove) |
| `HelloWorldPlugin.cpp` | Beispiel-Plugin (standalone) |
| `build/` | Kompilierte Objekte und ELF-Dateien |
| `../data/apps/` | Fertige `.bin` Dateien |

## 🔧 Build-System Details

### Compiler Flags

```makefile
-Os                      # Optimize for size
-fPIC                    # Position-independent code
-ffunction-sections      # Each function in own section
-fdata-sections          # Each data in own section
-fno-exceptions          # No C++ exceptions
-fno-rtti                # No runtime type info
-mlongcalls              # ESP32 specific
-mtext-section-literals  # ESP32 specific
-nostdlib                # No standard library
```

### Linker Script

- VTable an Offset 0 (Section `.plugin_vtable`)
- Code in `.text`
- Read-only data in `.rodata`
- Initialized data in `.data`
- Uninitialized data in `.bss`

### Memory Layout

```
Offset 0x00:   VTable (PluginAppVTable)
Offset 0x28:   plugin_init()
Offset 0x44:   plugin_draw()
...            weitere Funktionen
```

## 🚀 Verwendung

### Aktuell: Statisches Plugin (Phase 1)

1. Plugin in `Apps/` erstellen
2. In `ESP32-BoardOS.ino` registrieren:

```cpp
#include "Core/StaticPluginApp.h"

extern "C" const PluginAppVTable* getMyPluginVTable();
StaticPluginApp app_myplugin(getMyPluginVTable());

void setup() {
  appman.add(&app_myplugin);
}
```

### Zukünftig: Dynamisches Plugin (Phase 2)

1. Plugin kompilieren: `make MyPlugin`
2. `.bin` auf ESP32 hochladen (via USB/BLE)
3. Automatisch beim Boot gefunden und geladen

## 🐛 Debugging

### Binary inspizieren

```bash
# Größe
ls -lh ../data/apps/HelloWorld.bin

# Hex-Dump (erste 20 Zeilen)
hexdump -C ../data/apps/HelloWorld.bin | head -20

# ELF Sections
xtensa-esp32-elf-readelf -S build/HelloWorldPlugin.elf

# Symbols
xtensa-esp32-elf-nm build/HelloWorldPlugin.elf
```

### Häufige Probleme

**Problem:** `undefined reference to memcpy`
**Lösung:** `runtime.c` ist linked

**Problem:** VTable nicht an Offset 0
**Lösung:** Check `plugin.ld`, Section `.plugin_vtable` muss first sein

**Problem:** Plugin-Größe > 128 KB
**Lösung:** Code optimieren, nur AppAPI nutzen, keine Arduino-Libs

## 📚 Weitere Informationen

- **Technische Details**: Siehe `TODO.md`
- **Core Integration**: Siehe `../CLAUDE.md`
- **AppAPI Reference**: Siehe `../Core/AppAPI.h`

## 🎓 Warum so kompliziert?

ESP32 hat **kein echtes Dynamic Linking** wie Linux:
- Keine MMU (Memory Management Unit) für virtuelle Adressen
- Kein Standard-Dynamic-Linker
- Müssen manuell relocaten (Adressen patchen)

**Aber:** Das Function Table Pattern ist einfach und funktioniert jetzt schon statisch!

## 🤝 Beitragen

Neue Plugins sind willkommen! Siehe `HelloWorldPlugin.cpp` als Template.

**Wichtig:**
- Nur AppAPI verwenden, keine direkten Arduino-Includes
- VTable mit `__attribute__((section(".plugin_vtable"), used))` markieren
- Alle Strings als `const char*` (nicht `String`)
- Eigene `int_to_str()` Funktion für Zahlen-zu-Text

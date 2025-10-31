# Makefile for ESP32 Plugin Apps
#
# Compiles plugin apps as position-independent binaries that can be
# loaded dynamically at runtime.

# ESP32 Toolchain
TOOLCHAIN_BASE := $(HOME)/.arduino15/packages/esp32/tools/esp-x32/2507
TOOLCHAIN_BIN := $(TOOLCHAIN_BASE)/bin

CC := $(TOOLCHAIN_BIN)/xtensa-esp32-elf-gcc
CXX := $(TOOLCHAIN_BIN)/xtensa-esp32-elf-g++
LD := $(TOOLCHAIN_BIN)/xtensa-esp32-elf-ld
OBJCOPY := $(TOOLCHAIN_BIN)/xtensa-esp32-elf-objcopy
SIZE := $(TOOLCHAIN_BIN)/xtensa-esp32-elf-size

# Compiler flags for position-independent code
CFLAGS := -Os \
          -fPIC \
          -ffunction-sections \
          -fdata-sections \
          -fno-exceptions \
          -fno-rtti \
          -mlongcalls \
          -mtext-section-literals \
          -nostdlib \
          -Wall

CXXFLAGS := $(CFLAGS) -std=c++17

# Linker flags
LDFLAGS := -T plugin.ld \
           --gc-sections \
           -nostdlib

# Output directory
BUILD_DIR := build
BIN_DIR := ../data/apps

# Targets
PLUGINS := HelloWorldPlugin

.PHONY: all clean $(PLUGINS)

all: $(PLUGINS)

# Create output directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Build HelloWorldPlugin
HelloWorldPlugin: $(BIN_DIR)/HelloWorld.bin

$(BUILD_DIR)/runtime.o: runtime.c | $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/HelloWorldPlugin.o: HelloWorldPlugin.cpp | $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/HelloWorldPlugin.elf: $(BUILD_DIR)/HelloWorldPlugin.o $(BUILD_DIR)/runtime.o
	@echo "Linking $@..."
	$(LD) $(LDFLAGS) $^ -o $@
	$(SIZE) $@

$(BIN_DIR)/HelloWorld.bin: $(BUILD_DIR)/HelloWorldPlugin.elf | $(BIN_DIR)
	@echo "Creating binary $@..."
	$(OBJCOPY) -O binary $< $@
	@echo "Plugin binary created: $@ ($$(stat -c%s $@) bytes)"

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/*.bin

# Help
help:
	@echo "ESP32 Plugin Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all              - Build all plugins"
	@echo "  HelloWorldPlugin - Build HelloWorld plugin"
	@echo "  clean            - Remove build artifacts"
	@echo "  help             - Show this help"
	@echo ""
	@echo "Output:"
	@echo "  Binaries will be created in: $(BIN_DIR)/"

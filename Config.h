#ifndef CONFIG_H
#define CONFIG_H

// --- feste Pins (Board) ---
inline constexpr int SPI_SCK_PIN   = 14;
inline constexpr int SPI_MOSI_PIN  = 15;
inline constexpr int SPI_MISO_PIN  = 2;
inline constexpr int SD_CS_PIN     = 13;

inline constexpr int TFT_CS_PIN    = 5;
inline constexpr int TFT_BL_PIN    = 22;

// --- Buttons ---
inline constexpr int BTN1_PIN = 19;
inline constexpr int BTN2_PIN = 4;

// --- Display ---
inline constexpr uint16_t TFT_W = 240;
inline constexpr uint16_t TFT_H = 240;

// --- Button Timings ---
inline constexpr uint16_t DEBOUNCE_MS    = 30;
inline constexpr uint16_t DOUBLE_GAP_MS  = 250;
inline constexpr uint16_t TRIPLE_GAP_MS  = 350;
inline constexpr uint16_t LONG_MS        = 600;

// --- Boot ---
inline constexpr uint32_t BOOT_MS = 1000;

#endif // CONFIG_H

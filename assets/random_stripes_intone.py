from display_init import tft
import urandom
import time

# Aktuelle Basisfarbe
current_base_color = (urandom.getrandbits(8), urandom.getrandbits(8), urandom.getrandbits(8))

def random_color_in_hue(base_color):
    """Generiert eine zufällige Farbe basierend auf der Basisfarbe."""
    r, g, b = base_color
    r = min(255, max(0, r + urandom.getrandbits(4) - 8))  # Zufällige Variation
    g = min(255, max(0, g + urandom.getrandbits(4) - 8))
    b = min(255, max(0, b + urandom.getrandbits(4) - 8))
    return (r << 16) | (g << 8) | b  # RGB565 Format

def draw_random_patterns():
    for _ in range(50):  # Anzahl der zufälligen Rechtecke
        x = 0
        y = urandom.randint(0, 239)
        width = 240
        height = urandom.randint(1, 3)
        color = random_color_in_hue(current_base_color)
        tft.fill_rect(x, y, width, height, color)

def next_image():
    global current_base_color
    current_base_color = (urandom.getrandbits(8), urandom.getrandbits(8), urandom.getrandbits(8))
    print(f"Neue Basisfarbe ausgewählt: {current_base_color}")
    draw_random_patterns()

# Hauptfunktion für Animation
def run():
    global running
    if running:  # Läuft nur, wenn `boot.py` das Flag setzt
        draw_random_patterns()
        time.sleep_ms(10)  # CPU-Entlastung für Interrupts


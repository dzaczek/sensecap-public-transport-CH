# Pomodoro Timer - Quick Start Guide

## 📦 Co zostało utworzone?

```
main/view/
├── indicator_pomodoro.h       ← Header API
└── indicator_pomodoro.c       ← Implementacja (1000+ linii)

POMODORO_INTEGRATION.md        ← Pełna dokumentacja integracji
POMODORO_TEST_EXAMPLE.c        ← Standalone test app
POMODORO_QUICK_START.md        ← Ten plik
```

## 🚀 Szybki Start (3 kroki)

### 1️⃣ Kompilacja

Pliki są już gotowe! CMakeLists.txt automatycznie wykryje nowy plik.

```bash
cd /Users/dzaczek/sensecap-public-transport-CH
idf.py build
```

### 2️⃣ Integracja do Twojej Aplikacji

W pliku gdzie chcesz pokazać Pomodoro (np. `main/view/indicator_view.c`):

```c
#include "indicator_pomodoro.h"

// W funkcji tworzenia UI:
lv_obj_t *container = lv_scr_act();  // lub tab, panel, itp.
lv_indicator_pomodoro_init(container);
```

### 3️⃣ Test Standalone (Opcjonalnie)

Jeśli chcesz przetestować widok w izolacji:

```bash
# Backup obecnego main.c
cp main/main.c main/main.c.backup

# Użyj testowego przykładu
cp POMODORO_TEST_EXAMPLE.c main/main.c

# Build i flash
idf.py build flash monitor

# Przywróć oryginał po testach
mv main/main.c.backup main/main.c
```

## 🎨 Wizualizacja Architektury

```
┌─────────────────────────────────────────────────────────┐
│                     LVGL Screen                         │
│  ┌───────────────────────────────────────────────────┐  │
│  │  [← Back]           Tap to Start                  │  │
│  │                      25:00                         │  │
│  │  ┌───────────────────────────────────────────┐    │  │
│  │  │          ╔═══════════════╗                │    │  │
│  │  │          ║ ░░░░░░░░░░░░░ ║  ← Sand (top)  │    │  │
│  │  │          ║ ░░░░░░░░░░░░░ ║                │    │  │
│  │  │          ║  ░░░░░░░░░░░  ║                │    │  │
│  │  │  Canvas  ║   ░░░░░░░░░   ║                │    │  │
│  │  │  240x280 ║    ░░░░░░░    ║                │    │  │
│  │  │          ║     ░░░░░     ║                │    │  │
│  │  │          ║       ▼       ║  ← Neck        │    │  │
│  │  │          ║               ║                │    │  │
│  │  │          ║               ║                │    │  │
│  │  │          ║               ║  ← Empty (bot) │    │  │
│  │  │          ╚═══════════════╝                │    │  │
│  │  └───────────────────────────────────────────┘    │  │
│  │         Tap hourglass to flip & start             │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘

    Core 0 (GUI Thread)         Core 1 (Physics Thread)
    ─────────────────────       ────────────────────────
    │                           │
    │ LVGL Render Timer         │ FreeRTOS Task
    │ 20 FPS (50ms)             │ 25 FPS (40ms)
    │                           │
    │ ┌──────────────┐          │ ┌──────────────┐
    │ │ Draw canvas  │          │ │ Update sand  │
    │ │ Update UI    │◄────┐    │ │ Cellular     │
    │ └──────────────┘     │    │ │ Automata     │
    │                      │    │ └──────────────┘
    │                    Mutex   │        │
    │                      │     │        ▼
    │                      └─────┼────► Grid
    │                            │    (120x140)
    │                            │
    ▼                            ▼
  Display                    Physics Sim
```

## 🎯 Kluczowe Funkcje

| Funkcja | Opis |
|---------|------|
| `lv_indicator_pomodoro_init(parent)` | Tworzy widok Pomodoro w podanym kontenerze |
| `lv_indicator_pomodoro_deinit()` | Czyści zasoby (wywołaj przy zmianie ekranu) |
| `lv_indicator_pomodoro_is_running()` | Sprawdza czy timer działa |
| `lv_indicator_pomodoro_get_remaining_seconds()` | Pobiera pozostały czas |

## ⚙️ Konfiguracja

Edytuj `main/view/indicator_pomodoro.c`:

```c
// Zmień czas sesji (default: 25 minut)
#define POMODORO_DURATION_SEC    (15 * 60)  // 15 minut

// Optymalizacja wydajności (zwiększ dla wolniejszych CPU)
#define PHYSICS_UPDATE_MS        50   // Było: 40
#define RENDER_UPDATE_MS         66   // Było: 50

// Optymalizacja pamięci (zwiększ rozmiar cząstki)
#define SAND_PARTICLE_SIZE       3    // Było: 2
```

## 🐛 Debugging

### Włącz verbose logi:

W `sdkconfig` lub `idf.py menuconfig`:

```
Component config → Log output → Default log verbosity → Verbose
```

Lub w kodzie:

```c
esp_log_level_set("pomodoro", ESP_LOG_VERBOSE);
```

### Sprawdź użycie pamięci:

```c
ESP_LOGI("mem", "Free heap: %d bytes", esp_get_free_heap_size());
ESP_LOGI("mem", "Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

## 📊 Wymagania Zasobów

| Zasób | Użycie |
|-------|---------|
| **RAM (Internal)** | ~20 KB (state + grids) |
| **PSRAM** | ~170 KB (canvas buffer) |
| **CPU Core 0** | ~15% @ 240 MHz (rendering) |
| **CPU Core 1** | ~20% @ 240 MHz (physics) |
| **Stack (Physics Task)** | 4 KB |

## ✅ Checklist Przed Kompilacją

- [ ] PSRAM włączony w `sdkconfig`
- [ ] LVGL 8.3 zainstalowany
- [ ] `LV_COLOR_DEPTH` = 16 (RGB565)
- [ ] Dual-core ESP32-S3
- [ ] FreeRTOS SMP włączony
- [ ] Ekran dotykowy skonfigurowany

## 🎮 Interakcja Użytkownika

1. **Kliknięcie w klepsydrę** → Obrót + Start timera
2. **Timer działa** → Piasek przesypuje się (25 FPS)
3. **Po 25 minutach** → "Session Complete!" + Stop
4. **Ponowne kliknięcie** → Reset + Nowa sesja
5. **Przycisk Back** → Wyjście z widoku

## 🔧 Typowe Problemy

### ❌ "Failed to allocate canvas buffer"
```
Rozwiązanie: Włącz PSRAM w menuconfig:
Component config → ESP32-specific → Support for external SPI RAM
```

### ❌ "Physics task not starting"
```
Rozwiązanie: Zwiększ dostępną pamięć dla zadań:
Component config → FreeRTOS → Kernel → configTOTAL_HEAP_SIZE
```

### ❌ Piasek nie spada płynnie
```
Rozwiązanie 1: Zmniejsz częstotliwość
#define PHYSICS_UPDATE_MS        50  // Zamiast 40

Rozwiązanie 2: Zwiększ rozmiar cząstki
#define SAND_PARTICLE_SIZE       3   // Zamiast 2
```

### ❌ Ekran się zawiesza
```
Rozwiązanie: Sprawdź czy mutex nie jest deadlock:
- Dodaj timeout do xSemaphoreTake()
- Sprawdź logi z TAG "pomodoro"
```

## 📚 Następne Kroki

Po udanej kompilacji i teście:

1. **Dodaj do menu głównego** (jeśli masz tabview)
2. **Dostosuj kolory** (COLOR_SAND, COLOR_GLASS, itp.)
3. **Dodaj dźwięk** po zakończeniu sesji
4. **Zapisuj statystyki** w NVS (liczba sesji)
5. **Dodaj Break Timer** (5 min po Pomodoro)

## 💡 Przykłady Użycia

### Standalone App
```c
void app_main(void) {
    bsp_board_init();
    lv_port_init();
    lv_indicator_pomodoro_init(lv_scr_act());
}
```

### W TabView
```c
lv_obj_t *tab = lv_tabview_add_tab(tabview, LV_SYMBOL_LOOP " Timer");
lv_indicator_pomodoro_init(tab);
```

### Z Przyciskiem Menu
```c
static void menu_pomodoro_cb(lv_event_t *e) {
    lv_obj_t *screen = lv_obj_create(lv_scr_act());
    lv_indicator_pomodoro_init(screen);
}
```

## 🎓 Teoria: Cellular Automata

Symulacja piasku działa na zasadzie automatu komórkowego:

```
Reguły spadania (dla każdej cząstki piasku):
1. Jeśli poniżej puste → spadaj w dół
2. Jeśli poniżej zajęte → spróbuj ukos (lewo/prawo losowo)
3. Jeśli wszystko zajęte → zostań w miejscu

Pseudo-kod:
for każda cząstka od dołu do góry:
    if grid[y+1][x] == EMPTY:
        grid[y+1][x] = SAND
        grid[y][x] = EMPTY
    elif grid[y+1][x-1] == EMPTY:
        grid[y+1][x-1] = SAND
        grid[y][x] = EMPTY
    elif grid[y+1][x+1] == EMPTY:
        grid[y+1][x+1] = SAND
        grid[y][x] = EMPTY
```

## 📞 Pomoc

Jeśli napotkasz problemy:

1. Sprawdź logi: `idf.py monitor`
2. Zwiększ verbose: `esp_log_level_set("pomodoro", ESP_LOG_DEBUG)`
3. Sprawdź pamięć: `esp_get_free_heap_size()`
4. Przeczytaj `POMODORO_INTEGRATION.md` (pełna dokumentacja)

---

**Powodzenia! 🎉**

Stworzono dla SenseCAP Indicator D1 (ESP32-S3)  
LVGL 8.3 | FreeRTOS | Dual-Core Architecture

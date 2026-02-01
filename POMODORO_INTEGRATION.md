# Integracja Pomodoro Timer - Instrukcja

## Pliki utworzone

1. `main/view/indicator_pomodoro.h` - Nagłówek API
2. `main/view/indicator_pomodoro.c` - Implementacja z symulacją piasku

## Funkcjonalność

### Kluczowe Cechy
- ✅ Wizualizacja klepsydry z symulacją spadającego piasku (cellular automata)
- ✅ Timer 25-minutowy (standardowy Pomodoro)
- ✅ Obsługa dotyku - kliknięcie w klepsydrę odwraca ją i resetuje timer
- ✅ Optymalizacja wielordzeniowa:
  - **Core 1**: Obliczenia fizyki piasku (FreeRTOS Task)
  - **Core 0**: Renderowanie LVGL (Timer)
- ✅ Przycisk "Back" do powrotu

### Architektura Wydajnościowa

```
┌─────────────────────────────────────┐
│  Core 0 (LVGL/GUI Thread)          │
│  - Render Timer (20 FPS)            │
│  - Canvas Drawing                   │
│  - UI Updates                       │
└─────────────────────────────────────┘
              │
              │ Mutex-protected grid access
              ▼
┌─────────────────────────────────────┐
│  Shared Memory                      │
│  - Sand grid (double buffer)        │
│  - Hourglass state                  │
└─────────────────────────────────────┘
              ▲
              │ Mutex-protected grid access
              │
┌─────────────────────────────────────┐
│  Core 1 (Physics Thread)            │
│  - Physics Task (25 FPS)            │
│  - Cellular Automata                │
│  - Sand Particle Updates            │
└─────────────────────────────────────┘
```

## Integracja w Aplikacji

### Krok 1: Dodaj do CMakeLists.txt

Edytuj `main/CMakeLists.txt` i dodaj nowy plik do źródeł:

```cmake
set(srcs 
    "main.c"
    "lv_port.c"
    "sbb_clock.c"
    "view/indicator_view.c"
    "view/indicator_pomodoro.c"    # <-- DODAJ TĘ LINIĘ
    # ... reszta plików
)
```

### Krok 2: Dodaj do Enuma Ekranów

W `main/view_data.h`, dodaj nowy ekran do enuma:

```c
enum start_screen {
    SCREEN_SENSECAP_LOG,
    SCREEN_WIFI_CONFIG,
    SCREEN_BUS_COUNTDOWN,
    SCREEN_TRAIN_STATION,
    SCREEN_SETTINGS,
    SCREEN_POMODORO,        // <-- DODAJ
};
```

### Krok 3: Dodaj Tab w TabView

W `main/view/indicator_view.c`, zaimportuj nagłówek:

```c
#include "indicator_pomodoro.h"
```

Następnie w funkcji `indicator_view_init()`, dodaj nowy tab:

```c
// W miejscu tworzenia tabview (przykład):
static lv_obj_t *pomodoro_screen = NULL;

int indicator_view_init(void) {
    // ... istniejący kod tworzenia tabview ...
    
    // Dodaj tab Pomodoro
    lv_obj_t *tab_pomodoro = lv_tabview_add_tab(tabview, LV_SYMBOL_LOOP " Pomodoro");
    pomodoro_screen = lv_indicator_pomodoro_init(tab_pomodoro);
    
    // ... reszta kodu ...
}
```

### Krok 4: Standalone Uruchomienie (Opcjonalnie)

Jeśli chcesz uruchomić widok jako standalone ekran (bez tabview):

```c
#include "indicator_pomodoro.h"

void app_main(void) {
    // ... inicjalizacja LVGL, display, itp. ...
    
    // Stwórz pełnoekranowy widok Pomodoro
    lv_obj_t *screen = lv_scr_act();
    lv_indicator_pomodoro_init(screen);
    
    // ... główna pętla ...
}
```

## Użycie

### Podstawowe Operacje

1. **Uruchomienie**: Kliknij w klepsydrę na ekranie
2. **Reset**: Kliknij ponownie (klepsydra się odwróci)
3. **Powrót**: Kliknij przycisk "Back" w lewym górnym rogu

### API (jeśli potrzebujesz kontroli programowej)

```c
// Sprawdź czy timer działa
bool running = lv_indicator_pomodoro_is_running();

// Pobierz pozostały czas w sekundach
int remaining = lv_indicator_pomodoro_get_remaining_seconds();

// Cleanup (np. przy zmianie ekranu)
lv_indicator_pomodoro_deinit();
```

## Konfiguracja

Możesz dostosować parametry w `indicator_pomodoro.c`:

```c
// Czas Pomodoro (domyślnie 25 minut)
#define POMODORO_DURATION_SEC    (25 * 60)

// Rozmiar canvasu
#define CANVAS_WIDTH             240
#define CANVAS_HEIGHT            280

// Rozmiar "ziarna piasku" (większy = szybsze renderowanie)
#define SAND_PARTICLE_SIZE       2

// Częstotliwość aktualizacji fizyki (ms)
#define PHYSICS_UPDATE_MS        40    // 25 FPS

// Częstotliwość renderowania (ms)
#define RENDER_UPDATE_MS         50    // 20 FPS
```

## Wymagania Sprzętowe

- **RAM**: ~50 KB (grid + canvas buffer)
- **PSRAM**: Zalecane dla canvas buffer
- **CPU**: Dual-core (Core 0: GUI, Core 1: Physics)
- **Display**: Dotykowy ekran

## Optymalizacja Wydajności

### Jeśli wydajność jest niska:

1. **Zwiększ `SAND_PARTICLE_SIZE`** (2 → 3 lub 4)
   - Mniejsza rozdzielczość symulacji = szybsze obliczenia

2. **Zmniejsz FPS fizyki/renderowania**:
   ```c
   #define PHYSICS_UPDATE_MS    50   // 20 FPS zamiast 25
   #define RENDER_UPDATE_MS     66   // 15 FPS zamiast 20
   ```

3. **Zmniejsz liczbę cząstek piasku**:
   ```c
   // W init_sand_grid():
   int max_sand_particles = (GRID_WIDTH * GRID_HEIGHT) / 10;  // Było /8
   ```

### Jeśli brakuje RAM:

1. Użyj PSRAM dla buforów:
   ```c
   // Już zaimplementowane:
   g_state->canvas_buf = heap_caps_malloc(buf_size, 
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
   ```

## Rozwiązywanie Problemów

### Problem: Piasek nie spada płynnie
**Rozwiązanie**: Zmniejsz `PHYSICS_UPDATE_MS` (np. do 30ms)

### Problem: Ekran mruga/tearing
**Rozwiązanie**: Zwiększ `RENDER_UPDATE_MS` lub włącz double buffering w LVGL

### Problem: Klepsydra nie reaguje na dotyk
**Rozwiązanie**: Sprawdź czy `LV_OBJ_FLAG_CLICKABLE` jest ustawiona na canvas

### Problem: Crash przy deinit
**Rozwiązanie**: Upewnij się że `lv_indicator_pomodoro_deinit()` jest wywołana z LVGL thread (Core 0)

## Przykładowy Scenariusz Użycia

```
1. Użytkownik wchodzi na tab "Pomodoro"
2. Widzi klepsydrę z piaskiem w górnej komorze
3. Timer pokazuje "25:00"
4. Status: "Tap to Start"
5. Użytkownik klika w klepsydru
   → Klepsydra obraca się (flip animation via grid flip)
   → Timer startuje: 24:59, 24:58...
   → Piasek zaczyna przesypywać się
   → Status: "Focus Time"
6. Po 25 minutach:
   → Timer: "00:00"
   → Status: "Session Complete!"
   → Piasek przestaje się przesypywać
7. Użytkownik klika ponownie → Reset i nowa sesja
```

## Kolejne Kroki (Enhancement Ideas)

1. **Dźwięk**: Dodaj buzzer/beep po zakończeniu sesji
2. **Wibracje**: Wykorzystaj RP2040 do wibracji po zakończeniu
3. **Break Timer**: Automatyczny 5-minutowy break po Pomodoro
4. **Statystyki**: Licznik ukończonych sesji (NVS storage)
5. **Animacja obrotu**: Smooth rotation zamiast instant flip
6. **Kolory**: Różne kolory piasku dla Work/Break

## Licencja

Zgodnie z licencją projektu SenseCAP Indicator.

---

**Autor**: Senior Embedded Developer  
**Data**: 2026-01-31  
**Wersja LVGL**: 8.3  
**Hardware**: SenseCAP Indicator D1 (ESP32-S3)

# 🔧 Stack Overflow Fix - Pomodoro Timer

## ❌ Problem

```
***ERROR*** A stack overflow in task sys_evt has been detected.
```

ESP32 restartuje się z powodu przepełnienia stosu w zadaniu systemowym `sys_evt` (WiFi/TCP/IP events).

---

## ✅ Rozwiązanie - Wprowadzone Zmiany

### 1. Zwiększenie Stosu Systemowego (`sdkconfig.defaults`)

Dodano konfiguracje zwiększające rozmiary stosu:

```ini
# System event task stack (WiFi, TCP/IP events)
# Increased from default 2304 to prevent stack overflow with Pomodoro timer
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096

# Main task stack size (for app_main)
# Increased to accommodate additional LVGL widgets
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

### 2. Optymalizacja Pomodoro Physics Task

Zmniejszono stos physics task z 4096 do 3072 bajtów:

```c
// Before:
xTaskCreatePinnedToCore(..., 4096, ...)

// After:
xTaskCreatePinnedToCore(..., 3072, ...)  // Reduced to save memory
```

### 3. Dodano Monitoring Stosu

Dodano logowanie użycia stosu w physics task:

```c
UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(NULL);
ESP_LOGI(TAG, "Physics task stack high water mark: %d bytes free", 
         stack_hwm * sizeof(StackType_t));
```

---

## 🚀 Co Teraz?

### Krok 1: Rebuild z Nową Konfiguracją

```bash
cd /Users/dzaczek/sensecap-public-transport-CH

# WAŻNE: Fullclean aby zastosować nowy sdkconfig
idf.py fullclean

# Build z nowymi ustawieniami
idf.py build

# Flash i monitor
idf.py flash monitor
```

### Krok 2: Sprawdź Logi

Po uruchomieniu powinieneś zobaczyć:

```
I (xxx) pomodoro: Physics task started on core 1
I (xxx) pomodoro: Physics task stack high water mark: XXXX bytes free
I (xxx) pomodoro: Pomodoro timer initialized successfully
```

**Jeśli `high water mark` pokazuje >500 bytes free** = OK ✅  
**Jeśli pokazuje <200 bytes free** = Trzeba dalej optymalizować ⚠️

---

## 📊 Użycie Pamięci (Po Zmianach)

| Task | Stack Size | Zmiana |
|------|-----------|--------|
| `sys_evt` (system events) | 4096 bytes | ↑ +1792 (było 2304) |
| `app_main` (main task) | 8192 bytes | ↑ +4096 (było 4096) |
| `pomodoro_physics` | 3072 bytes | ↓ -1024 (było 4096) |
| **RAZEM** | +4864 bytes | Dodatkowe użycie RAM |

---

## 🔍 Diagnostyka (Jeśli Dalej Crashuje)

### Sprawdź Wolną Pamięć

Dodaj do `main.c` lub `indicator_view.c`:

```c
ESP_LOGI("mem", "Free heap: %d bytes", esp_get_free_heap_size());
ESP_LOGI("mem", "Min free heap: %d bytes", esp_get_minimum_free_heap_size());
ESP_LOGI("mem", "Free PSRAM: %d bytes", 
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

### Monitoruj Stack Overflow

W `idf.py menuconfig`:

```
Component config → FreeRTOS → Kernel
  → Check for stack overflow: "Check using canary bytes"
```

### Dalsze Optymalizacje (Jeśli Potrzebne)

#### Opcja A: Zmniejsz Canvas

W `indicator_pomodoro.c`:

```c
// Before:
#define CANVAS_WIDTH   240
#define CANVAS_HEIGHT  280

// After (mniejszy):
#define CANVAS_WIDTH   200
#define CANVAS_HEIGHT  240
```

#### Opcja B: Zwiększ Rozmiar Cząstki (Mniej Obliczeń)

```c
// Before:
#define SAND_PARTICLE_SIZE  2

// After (większe ziarna = mniej cząstek):
#define SAND_PARTICLE_SIZE  3
```

#### Opcja C: Zmniejsz FPS

```c
// Before:
#define PHYSICS_UPDATE_MS  40  // 25 FPS

// After (wolniej):
#define PHYSICS_UPDATE_MS  50  // 20 FPS
```

---

## ✅ Oczekiwany Rezultat

Po zmianach system powinien:

1. ✅ Bootować bez crashu
2. ✅ Łączyć się z WiFi bez błędów
3. ✅ Pokazywać wszystkie taby (Clock, Bus, Train, Timer, Settings)
4. ✅ Timer Pomodoro działa płynnie
5. ✅ Brak restartów i stack overflow

---

## 📝 Notatki

### Dlaczego `sys_evt` przepełnił się?

Task `sys_evt` obsługuje eventy WiFi i TCP/IP. Gdy dodaliśmy:
- Nowy tab (Pomodoro)
- Physics task (Core 1)
- Canvas rendering (duże bufory)
- ESP Timery

...system potrzebował więcej stosu dla event queue.

### Optymalne Wartości

Dla SenseCAP Indicator D1 z Pomodoro:
- System events: 4096 bytes (zwiększone)
- Main task: 8192 bytes (zwiększone)
- Physics task: 3072 bytes (zoptymalizowane)
- LVGL task: default (~8KB)

---

## 🆘 Jeśli Dalej Nie Działa

1. **Zwiększ jeszcze bardziej `CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`:**
   ```ini
   CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=6144
   ```

2. **Wyłącz Pomodoro tymczasowo** (debug):
   W `indicator_view.c` zakomentuj:
   ```c
   // pomodoro_screen = lv_indicator_pomodoro_init(pomodoro_tab);
   ```

3. **Sprawdź całkowite użycie heap:**
   ```bash
   idf.py size
   ```

4. **Włącz verbose memory tracking:**
   W `sdkconfig.defaults`:
   ```ini
   CONFIG_HEAP_TRACING=y
   CONFIG_HEAP_TRACING_STACK_DEPTH=10
   ```

---

## 📊 Memory Budget (Orientacyjny)

```
SRAM (520 KB total):
  - System/WiFi/TCP:    ~100 KB
  - FreeRTOS stacks:    ~40 KB  (↑ po zmianach)
  - LVGL memory:        ~60 KB
  - Application:        ~50 KB
  - Pomodoro grids:     ~35 KB
  - Free buffer:        ~235 KB
  ────────────────────────────
  TOTAL:                ~520 KB

PSRAM (8 MB):
  - Canvas buffer:      ~170 KB
  - LVGL buffers:       ~2 MB
  - Free:               ~5.8 MB
```

---

**Status:** ✅ Fix Applied  
**Tested:** Pending (po rebuild)  
**Expected:** Stack overflow resolved

---

Rebuild projektu i daj znać czy działa! 🚀

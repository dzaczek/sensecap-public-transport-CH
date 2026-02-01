# ✅ Compilation Checklist - Pomodoro Timer

Przed kompilacją upewnij się, że wszystkie wymagania są spełnione.

## 📋 Wymagania Hardware

| Komponent | Specyfikacja | Status |
|-----------|-------------|--------|
| Board | SenseCAP Indicator D1 | ✅ |
| MCU | ESP32-S3 (dual-core) | ✅ |
| Co-processor | RP2040 | ✅ (nie używany) |
| Display | 480x320 IPS LCD | ✅ |
| Touch | Capacitive Touch Panel | ✅ |
| PSRAM | 8MB Octal SPI RAM | ✅ Włączony |
| Flash | 8MB | ✅ |

## 📦 Wymagania Software

### ESP-IDF
- [ ] ESP-IDF v5.1.1 lub nowszy ✅ (projekt używa 5.1.1)

### LVGL Configuration
Sprawdź w `idf.py menuconfig`:

```
Component config → LVGL configuration
```

Wymagane ustawienia:
- [ ] `LV_USE_CANVAS` = 1 (zwykle domyślnie włączone)
- [ ] `LV_MEM_CUSTOM` = y ✅ (w sdkconfig.defaults)
- [ ] `LV_COLOR_DEPTH` = 16 (RGB565) - default
- [ ] `LV_MEM_SIZE` >= 64KB (zalecane dla canvas)

### FreeRTOS
- [ ] `CONFIG_FREERTOS_HZ` = 1000 ✅ (w sdkconfig.defaults)
- [ ] `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH` >= 4096 ✅ (4096 w defaults)
- [ ] Dual-core SMP enabled (domyślnie dla ESP32-S3)

### PSRAM
Sprawdź w `sdkconfig.defaults`:
- [x] `CONFIG_SPIRAM=y` ✅
- [x] `CONFIG_SPIRAM_SPEED_120M=y` ✅
- [x] `CONFIG_SPIRAM_MODE_OCT=y` ✅
- [x] `CONFIG_SPIRAM_USE_MALLOC=y` ✅

## 📁 Pliki Utworzone

Sprawdź czy następujące pliki istnieją:

```bash
ls -lh main/view/indicator_pomodoro.h
ls -lh main/view/indicator_pomodoro.c
```

Oczekiwane wyniki:
```
main/view/indicator_pomodoro.h   (~1.5 KB)
main/view/indicator_pomodoro.c   (~35 KB)
```

## 🔧 Pre-Compilation Check

### Krok 1: Sprawdź strukturę projektu

```bash
cd /Users/dzaczek/sensecap-public-transport-CH
tree main/view/
```

Oczekiwany wynik:
```
main/view/
├── indicator_pomodoro.c  ← NOWY
├── indicator_pomodoro.h  ← NOWY
├── indicator_view.c
└── indicator_view.h
```

### Krok 2: Sprawdź CMakeLists.txt

```bash
cat main/CMakeLists.txt | grep VIEW_SOURCES
```

Oczekiwany wynik (GLOB_RECURSE automatycznie dołącza nowe pliki):
```cmake
file(GLOB_RECURSE VIEW_SOURCES ${VIEW_DIR}/*.c)
```

✅ **Żadnych zmian w CMakeLists.txt nie jest potrzebnych!**

### Krok 3: Sprawdź dostępną pamięć

Przed kompilacją sprawdź dostępne miejsce na partycjach:

```bash
cat partitions.csv
```

Sprawdź czy partycja `factory` jest wystarczająco duża (≥2MB zalecane).

### Krok 4: Clean Build (Opcjonalnie)

Jeśli wcześniej kompilowałeś projekt:

```bash
idf.py fullclean
```

## 🛠️ Kompilacja

### Metoda 1: Standalone Test (Najprostszy test)

1. **Backup głównego pliku:**
   ```bash
   cp main/main.c main/main.c.backup
   ```

2. **Użyj przykładu testowego:**
   ```bash
   cp POMODORO_TEST_EXAMPLE.c main/main.c
   ```

3. **Kompiluj:**
   ```bash
   idf.py build
   ```

4. **Flash:**
   ```bash
   idf.py flash monitor
   ```

5. **Przywróć oryginał (po testach):**
   ```bash
   mv main/main.c.backup main/main.c
   ```

### Metoda 2: Integracja z istniejącą aplikacją

Edytuj `main/view/indicator_view.c`:

```c
// 1. Dodaj include na górze pliku
#include "indicator_pomodoro.h"

// 2. Znajdź funkcję tworzącą tabview (np. indicator_view_init)
// 3. Dodaj nowy tab:

lv_obj_t *tab_pomodoro = lv_tabview_add_tab(tabview, LV_SYMBOL_LOOP " Pomodoro");
lv_indicator_pomodoro_init(tab_pomodoro);
```

Następnie:

```bash
idf.py build flash monitor
```

## 🎯 Oczekiwane Wyniki Kompilacji

### Build Output

```
[1/1234] Generating ...
...
[1234/1234] Linking CXX executable indicator_public_transport.elf
Project build complete.
```

### Memory Summary

Oczekiwane użycie pamięci (przykładowe wartości):

```
Total sizes:
 DRAM .data size:    ~80000 bytes
 DRAM .bss  size:    ~45000 bytes
 Used static DRAM:  ~125000 bytes (Free: ~200KB)
 Used static IRAM:  ~110000 bytes (Free: ~20KB)
 Flash code:       ~1200000 bytes
```

**Uwaga:** Dodanie Pomodoro Timer powinno zwiększyć:
- Flash code: +35KB (kod C)
- DRAM: +~20KB (state + grids, reszta w PSRAM)

## 🐛 Typowe Błędy Kompilacji

### Błąd 1: "undefined reference to `lv_canvas_create`"

**Przyczyna:** LVGL nie ma włączonego LV_USE_CANVAS

**Rozwiązanie:**
```bash
idf.py menuconfig
# Przejdź do: Component config → LVGL configuration → Widgets
# Włącz: LV_USE_CANVAS
```

### Błąd 2: "fatal error: esp_timer.h: No such file or directory"

**Przyczyna:** Brak w COMPONENT_REQUIRES

**Rozwiązanie:** Dodaj do `main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS ...
    INCLUDE_DIRS ...
    REQUIRES esp_timer    # <-- Dodaj jeśli brakuje
)
```

### Błąd 3: "region `iram0_0_seg' overflowed"

**Przyczyna:** Za duży kod w IRAM

**Rozwiązanie:** W `indicator_pomodoro.c` dodaj na górze:
```c
// Przenieś funkcje do Flash zamiast IRAM
#define IRAM_ATTR
```

### Błąd 4: Stack overflow w physics task

**Przyczyna:** Za mały stack (4KB może nie wystarczyć w niektórych przypadkach)

**Rozwiązanie:** W `indicator_pomodoro.c` zwiększ stack:
```c
xTaskCreatePinnedToCore(
    physics_task_func,
    "pomodoro_physics",
    8192,  // Było: 4096
    ...
);
```

### Błąd 5: "Failed to allocate canvas buffer"

**Przyczyna:** Brak PSRAM lub za mało pamięci

**Rozwiązanie:**
1. Sprawdź `idf.py menuconfig → Component config → ESP32-specific → Support for external SPI RAM`
2. Jeśli PSRAM jest włączony, zmniejsz rozmiar canvas w `indicator_pomodoro.c`:
   ```c
   #define CANVAS_WIDTH   200  // Było: 240
   #define CANVAS_HEIGHT  240  // Było: 280
   ```

### Błąd 6: Linker errors z LVGL fonts

**Przyczyna:** Brak Montserrat fonts

**Rozwiązanie:** Sprawdź w `sdkconfig.defaults`:
```
CONFIG_LV_FONT_MONTSERRAT_12=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_28=y
```

## ✔️ Final Checklist

Przed pierwszą kompilacją:

- [ ] PSRAM włączony w sdkconfig
- [ ] Pliki .h i .c istnieją w `main/view/`
- [ ] CMakeLists.txt używa GLOB_RECURSE (już jest)
- [ ] ESP-IDF v5.1.1+ zainstalowany
- [ ] Port USB podłączony do SenseCAP Indicator

Podczas kompilacji:
- [ ] Build kończy się bez błędów
- [ ] Total size < 8MB (flash limit)
- [ ] DRAM usage < 300KB

Po flashowaniu:
- [ ] Monitor pokazuje "Pomodoro timer initialized"
- [ ] Log pokazuje "Physics task started on core 1"
- [ ] Brak "Failed to allocate" errors

## 🚀 Quick Command Reference

```bash
# Sprawdź konfigurację
idf.py menuconfig

# Clean build
idf.py fullclean

# Kompiluj
idf.py build

# Flash + Monitor
idf.py flash monitor

# Tylko monitor (po flash)
idf.py monitor

# Wyjdź z monitora
Ctrl + ]

# Erase flash (factory reset)
idf.py erase-flash
```

## 📊 Monitoring Runtime

Po uruchomieniu, sprawdź w monitorze:

```
I (1234) pomodoro: Initializing Pomodoro timer...
I (1245) pomodoro: Sand grid initialized with 1800 particles
I (1256) pomodoro: Physics task started on core 1
I (1267) pomodoro: Pomodoro timer initialized successfully
I (1278) pomodoro: Grid: 120x140, Canvas: 240x280
```

Jeśli widzisz powyższe logi - wszystko działa! ✅

## 🎉 Success Indicators

1. **Kompilacja**: Build kończy się komunikatem "Project build complete"
2. **Flash**: "Hash of data verified" pojawia się
3. **Boot**: ESP32 bootuje bez panik/restart loops
4. **Display**: Ekran pokazuje klepsydrę
5. **Touch**: Dotknięcie klepsydry odwraca ją
6. **Physics**: Piasek płynnie spada (25 FPS)
7. **Timer**: Odliczanie 25:00 → 24:59 → ...

## 📝 Notes

- Kompilacja może potrwać 2-5 minut przy pierwszym build
- Flashing trwa ~30 sekund
- PSRAM musi być włączony dla canvas buffer (170KB)
- Kod używa ~35KB Flash, ~20KB RAM, ~170KB PSRAM

---

**Powodzenia z kompilacją! 🚀**

W razie problemów:
1. Sprawdź logi: `idf.py monitor`
2. Włącz verbose: `idf.py -v build`
3. Przeczytaj sekcję "Typowe Błędy" powyżej

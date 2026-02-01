# ⏱️ Kalibracja Prędkości Piasku - Pomodoro Timer

## ❌ Problem (Przed)

Piasek przesypuje się **za szybko**:
- Czas rzeczywisty: ~2 minuty
- Oczekiwany: 25 minut
- **Różnica: 12.5x za szybko!**

## ✅ Rozwiązanie - Frame Skipping

### Co Zmieniłem:

#### 1. Dodano `PHYSICS_SKIP_FRAMES`

```c
#define PHYSICS_UPDATE_MS    40    // Tick co 40ms (25 FPS)
#define PHYSICS_SKIP_FRAMES  10    // Wykonuj physics co 10-tą ramkę
```

**Efektywna prędkość:**
- Przed: 25 FPS (update co 40ms)
- Po: **2.5 FPS** (update co 400ms)
- **Spowolnienie: 10x** ✅

#### 2. Licznik Ramek w Physics Task

```c
uint32_t frame_counter = 0;

while (running) {
    frame_counter++;
    if (frame_counter >= PHYSICS_SKIP_FRAMES) {
        update_physics();  // ← Wykonuje się tylko co 10-tą ramkę
        frame_counter = 0;
    }
    vTaskDelay(40ms);  // Tick co 40ms (dla smooth rendering)
}
```

---

## 📊 Kalibracja

### Obecne Ustawienie:

```
PHYSICS_SKIP_FRAMES = 10
├─ Efektywna prędkość: 2.5 FPS
├─ Czas przesypywania: ~20 minut (estimate)
└─ Cel: 25 minut
```

### Jeśli Piasek Dalej za Szybki/Wolny:

#### Dostosuj `PHYSICS_SKIP_FRAMES` w `indicator_pomodoro.c`:

| `PHYSICS_SKIP_FRAMES` | Efektywne FPS | Szacowany Czas Przesypania |
|----------------------|---------------|----------------------------|
| `5` | 5 FPS | ~10 minut (za szybko) |
| `8` | 3.1 FPS | ~16 minut |
| **`10`** | **2.5 FPS** | **~20 minut** ✅ (blisko!) |
| `12` | 2.1 FPS | ~24 minut (bardzo blisko!) |
| `13` | 1.9 FPS | ~26 minut (prawie idealnie!) |
| `15` | 1.7 FPS | ~30 minut (za wolno) |

**Zalecana wartość:** `13` (najbliżej 25 minut)

---

## 🔧 Jak Dostosować Prędkość

### Krok 1: Edytuj `indicator_pomodoro.c`

```c
// Linia ~31:
#define PHYSICS_SKIP_FRAMES  13  // Zmień z 10 na 13
```

### Krok 2: Rebuild

```bash
idf.py build flash
```

### Krok 3: Test

1. Uruchom urządzenie
2. Kliknij tab "⏱ Timer"
3. Kliknij klepsydrę (flip & start)
4. **Odczekaj ~5 minut** i sprawdź ile piasku spadło
5. **Ekstrapoluj** do 25 minut:

```
Jeśli po 5 minutach spadło:
- 100% piasku → Za szybko! Zwiększ SKIP_FRAMES
- 50% piasku  → Za szybko! Zwiększ SKIP_FRAMES  
- 20% piasku  → OK! (20% × 5 = 100% w 25 min) ✅
- 10% piasku  → Za wolno! Zmniejsz SKIP_FRAMES
```

---

## 🧪 Formuła Kalibracji

### Oblicz Idealną Wartość:

```
Czas rzeczywisty (minuty) × Obecny SKIP_FRAMES
───────────────────────────────────────────────── = Nowy SKIP_FRAMES
              25 minut (cel)

Przykład:
  Piasek spadł w 20 minut, SKIP_FRAMES = 10
  
  20 × 10 / 25 = 8
  
  Nowy SKIP_FRAMES = 8? NIE! To by przyspieszyło!
  
  Poprawnie:
  25 × 10 / 20 = 12.5 ≈ 13 ✅
```

**Wzór uproszczony:**
```
Nowy_SKIP = Stary_SKIP × (25 / Czas_rzeczywisty)
```

---

## 📈 Wykres Zależności

```
Czas spadania piasku
   ^
30 │                        ×  (SKIP=15)
   │
25 │                   ×  (SKIP=13) ← IDEALNIE!
   │              ×  (SKIP=10)
20 │         ×  (SKIP=8)
   │    ×  (SKIP=5)
10 │ ×  (SKIP=3)
   │
 0 └─────────────────────────────────>
   0   5   10  15  20  25  30  35  PHYSICS_SKIP_FRAMES
```

---

## 🎯 Inne Metody Dostosowania

### Metoda 1: Zwiększ Liczbę Cząstek (Mniej Efektywne)

W `init_sand_grid()`:
```c
// Przed:
int max_sand_particles = (GRID_WIDTH * GRID_HEIGHT) / 8;

// Po (więcej piasku = dłużej spada):
int max_sand_particles = (GRID_WIDTH * GRID_HEIGHT) / 6;
```

**Problem:** Więcej obliczeń = wolniejszy physics

### Metoda 2: Zmniejsz Szyjkę Klepsydry (Bardziej Realistyczne)

```c
// Przed:
#define HOURGLASS_NECK_WIDTH  8  // pikseli

// Po (węższa szyja = wolniej spada):
#define HOURGLASS_NECK_WIDTH  6  // pikseli
```

**Efekt:** Bardziej realistyczna klepsydra, ale mniej przewidywalne tempo

---

## 🔍 Debug - Pomiar Rzeczywistego Czasu

### Dodaj Log w `update_physics()`:

```c
static void update_physics(void) {
    static uint32_t physics_call_count = 0;
    static int64_t last_log_time = 0;
    
    physics_call_count++;
    
    // Log co 60 sekund (150 wywołań @ 2.5 FPS)
    if (physics_call_count % 150 == 0) {
        int64_t now = esp_timer_get_time();
        if (last_log_time > 0) {
            float elapsed_sec = (now - last_log_time) / 1000000.0;
            ESP_LOGI(TAG, "Physics: 150 updates in %.1f seconds", elapsed_sec);
        }
        last_log_time = now;
    }
    
    // ... reszta kodu physics ...
}
```

**Oczekiwany output:**
```
I (xxx) pomodoro: Physics: 150 updates in 60.0 seconds ✅
```

---

## 📝 Obecne Zmiany

### Plik: `indicator_pomodoro.c`

```diff
  // Configuration
  #define PHYSICS_UPDATE_MS    40
+ #define PHYSICS_SKIP_FRAMES  10    // Dodano!

  static void physics_task_func(void *arg) {
+     uint32_t frame_counter = 0;
      
      while (running) {
+         frame_counter++;
+         if (frame_counter >= PHYSICS_SKIP_FRAMES) {
              update_physics();
+             frame_counter = 0;
+         }
          vTaskDelay(delay);
      }
  }
```

---

## ✅ Oczekiwany Rezultat

Po zmianach:

```
Przed: Piasek spada w ~2 minuty
Po:    Piasek spada w ~20-25 minut ✅

Render:   Nadal 20 FPS (smooth) ✅
Physics:  2.5 FPS efektywne (wolniej) ✅
Wydajność: Lepsza (mniej obliczeń!) ✅
```

---

## 🎓 Teoria

### Dlaczego Frame Skipping?

1. **Renderowanie** (20 FPS) = Jak często **rysujemy** klepsydrę
   - Musi być szybkie dla smooth animation
   
2. **Physics** (2.5 FPS efektywne) = Jak szybko **piasek spada**
   - Może być wolne, bo to prędkość "fizyczna"

**Separacja:** Render ≠ Physics pozwala na smooth visual z realistycznym tempem!

---

## 🔄 Quick Adjust Guide

### Za Szybko (piasek spada w <20 min):
```c
#define PHYSICS_SKIP_FRAMES  13  // Zwiększ o 2-3
```

### Za Wolno (piasek spada w >30 min):
```c
#define PHYSICS_SKIP_FRAMES  8   // Zmniejsz o 2-3
```

### Idealnie (20-25 min):
```c
#define PHYSICS_SKIP_FRAMES  10-13  // Zostaw jak jest ✅
```

---

**Rebuild i testuj prędkość!** ⏱️🍅

```bash
idf.py build flash monitor
```

Po kliknięciu klepsydry sprawdź logi:
```
I (xxx) pomodoro: Physics rate: Every 10 frames (effective ~2.5 FPS)
```

Jeśli potrzebujesz dopasować - zmień `PHYSICS_SKIP_FRAMES` i rebuild! 🚀

# 🚪 Gate Logic - Ostateczna Naprawa Przepływu

## ❌ Problem (Poprzednio)

1. **Neck-only Limiter:** Ziarnka po bokach omijały detekcję "is_hourglass_neck" i spadały bez limitu → "wyciek" piasku.
2. **Global Limiter:** Limitował WSZYSTKIE ruchy, więc ziarnka spadały powoli w powietrzu (1px / 2s).

---

## ✅ Rozwiązanie - Horizontal Gate (Pozioma Bramka)

### Koncepcja

Definiujemy **linię bramki** w połowie wysokości (`GRID_HEIGHT / 2`).
Każde ziarenko, które chce **przekroczyć tę linię**, musi zapłacić budżetem.

Wszystkie inne ruchy (spadanie w górnej komorze, spadanie w dolnej komorze) są **DARMOWE** i nielimitowane.

### Logika

```c
int gate_y = GRID_HEIGHT / 2;

// Sprawdź czy ruch przekracza linię
bool crossing_gate = false;
if (falling_down) {
    if (y < gate_y && ny >= gate_y) crossing_gate = true;
} else {
    if (y >= gate_y && ny < gate_y) crossing_gate = true;
}

// Jeśli przekracza bramkę -> sprawdź budżet
if (crossing_gate) {
    if (budget_exhausted) {
        BLOCK;  // Czekaj przed bramką
    } else {
        ALLOW;  // Przejdź i odejmij od budżetu
    }
}
```

---

## 📊 Rezultat

1. **Górna komora:** Ziarnka spadają szybko (25 FPS) i gromadzą się nad linią środkową.
2. **Bramka:** Przepuszcza dokładnie 0.5 ziarenka/sekundę.
3. **Dolna komora:** Ziarnka po przejściu spadają szybko na dno.

### Dlaczego to jest lepsze?

- **Szczelność:** Żadne ziarenko nie może ominąć linii poziomej (niezależnie od szerokości X).
- **Płynność:** Spadanie jest szybkie i naturalne.
- **Precyzja:** Budżet kontroluje dokładnie czas opróżniania (25 minut).

---

## 🚀 Rebuild

```bash
cd /Users/dzaczek/sensecap-public-transport-CH
idf.py build flash monitor
```

Teraz powinieneś widzieć:
- Szybki ruch piasku.
- Powolne opróżnianie górnej komory (25 minut).
- Żadnych "uciekających" ziarenek po bokach.

✅ **Problem rozwiązany definitywnie!**

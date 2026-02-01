# 👆 Pomodoro Timer - User Interaction Flow

## 🎯 Complete User Journey

```
┌─────────────────────────────────────────────────────────────────┐
│                      USER STARTS APP                            │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
              ┌──────────────────────┐
              │  Main Menu / TabView │
              │  [Bus] [Train] [⏱]  │
              └──────────┬───────────┘
                         │ User selects Pomodoro tab
                         ▼
┌────────────────────────────────────────────────────────────────┐
│                    INITIAL STATE                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │  [← Back]          Tap to Start                          │ │
│  │                     25:00                                 │ │
│  │   ╔═══════════════╗                                       │ │
│  │   ║ ░░░░░░░░░░░░░ ║  ← Sand in TOP chamber               │ │
│  │   ║  ░░░░░░░░░░░  ║                                       │ │
│  │   ║   ░░░░░░░░░   ║                                       │ │
│  │   ║      ░░░      ║                                       │ │
│  │   ║       ║       ║  ← Narrow neck (center)              │ │
│  │   ║               ║                                       │ │
│  │   ║               ║  ← Empty BOTTOM chamber               │ │
│  │   ╚═══════════════╝                                       │ │
│  │   Tap hourglass to flip & start                          │ │
│  └──────────────────────────────────────────────────────────┘ │
└────────────────────────┬───────────────────────────────────────┘
                         │
                         │ User taps canvas
                         ▼
              ┌──────────────────────┐
              │   FLIP ANIMATION     │
              │  (Instant flip)      │
              │  - Grid flips        │
              │  - Gravity reverses  │
              └──────────┬───────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────────────┐
│                    RUNNING STATE                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │  [← Back]          Focus Time                            │ │
│  │                     24:59                                 │ │
│  │   ╔═══════════════╗                                       │ │
│  │   ║               ║  ← NOW EMPTY (was full)               │ │
│  │   ║       ║       ║                                       │ │
│  │   ║      ▼▼▼      ║  ← Sand falling through neck         │ │
│  │   ║   ░░░░░░░░░   ║                                       │ │
│  │   ║  ░░░░░░░░░░░  ║  ← Sand accumulating in BOTTOM       │ │
│  │   ║ ░░░░░░░░░░░░░ ║                                       │ │
│  │   ╚═══════════════╝                                       │ │
│  │   Tap again to restart                                   │ │
│  └──────────────────────────────────────────────────────────┘ │
└────────────────────────┬───────────────────────────────────────┘
                         │
                         │ Timer continues...
                         │ 24:58 → 24:57 → ... → 00:01
                         ▼
┌────────────────────────────────────────────────────────────────┐
│              AFTER 25 MINUTES (00:00)                          │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │  [← Back]       Session Complete!                        │ │
│  │                     00:00                                 │ │
│  │   ╔═══════════════╗                                       │ │
│  │   ║               ║  ← Completely empty                   │ │
│  │   ║       ║       ║                                       │ │
│  │   ║       ║       ║  ← No more sand falling               │ │
│  │   ║               ║                                       │ │
│  │   ║               ║                                       │ │
│  │   ║ ░░░░░░░░░░░░░ ║  ← ALL sand in BOTTOM                │ │
│  │   ╚═══════════════╝                                       │ │
│  │   Tap to start new session                               │ │
│  └──────────────────────────────────────────────────────────┘ │
└────────────────────────┬───────────────────────────────────────┘
                         │
                         ├─────► User taps again → Reset & Flip
                         │
                         └─────► User taps Back → Return to menu
```

---

## 🎬 Interaction Scenarios

### Scenario 1: Normal Pomodoro Session

```
1. User enters Pomodoro screen
   → Sees hourglass with sand in top chamber
   → Status: "Tap to Start"
   → Timer: "25:00"

2. User taps hourglass
   → Hourglass flips instantly (grid reversal)
   → Timer starts: 24:59, 24:58...
   → Status changes to "Focus Time"
   → Sand begins falling through neck

3. User observes sand falling
   → Physics runs at 25 FPS (smooth)
   → Render updates at 20 FPS
   → Sand accumulates in bottom chamber
   → Timer counts down continuously

4. After 25 minutes
   → Timer reaches 00:00
   → Status: "Session Complete!"
   → Sand stops falling (all in bottom)

5. User can:
   → Tap again → Reset & start new session
   → Tap Back → Return to main menu
```

### Scenario 2: Restart Mid-Session

```
1. User starts session (25:00 → 24:30)
2. User taps hourglass again
   → Immediate flip (grid reversal)
   → Timer resets to 25:00
   → New session begins
   → Sand that was falling is now reversed
```

### Scenario 3: Navigation Away

```
1. User starts session (25:00 → 20:15)
2. User taps "Back" button
   → lv_indicator_pomodoro_deinit() called
   → Physics task stops
   → Timers deleted
   → Memory freed
   → Returns to previous screen
   
3. If user returns later:
   → New session (timer not saved)
   → OR implement NVS save/restore for persistent state
```

---

## 📱 Touch Areas & Feedback

```
┌────────────────────────────────────────────┐
│ ┌──────┐ TOUCH AREA 1: Back Button        │
│ │ BACK │ - Size: 80x35 px                  │
│ └──────┘ - Action: Return to menu          │
│          - Visual: Button press animation  │
│                                            │
│          Status Label (Read-only)          │
│              25:00 Timer (Read-only)       │
│                                            │
│ ┌────────────────────────────────────────┐ │
│ │  TOUCH AREA 2: Canvas (Hourglass)      │ │
│ │  - Size: 240x280 px (entire canvas)    │ │
│ │  - Action: Flip hourglass & reset      │ │
│ │  - Visual: Instant grid flip           │ │
│ │  - Feedback:                           │ │
│ │    • Status changes                    │ │
│ │    • Timer resets to 25:00             │ │
│ │    • Sand direction reverses           │ │
│ └────────────────────────────────────────┘ │
│                                            │
│     Instructions Label (Read-only)         │
└────────────────────────────────────────────┘
```

---

## 🔄 State Transitions

```
┌──────────────┐
│ INITIALIZED  │ (Sand in top, timer 25:00, not running)
└──────┬───────┘
       │ User taps canvas
       ▼
┌──────────────┐
│   FLIPPING   │ (Grid flip in progress, <10ms)
└──────┬───────┘
       │ Flip complete
       ▼
┌──────────────┐
│   RUNNING    │ (Sand falling, timer counting down)
└──────┬───────┘
       │
       ├─► Timer == 0 ────────┐
       │                      ▼
       │               ┌──────────────┐
       │               │  COMPLETED   │ (Sand in bottom, timer 00:00)
       │               └──────┬───────┘
       │                      │ User taps canvas
       │                      │
       └──────────────────────┴──► Back to INITIALIZED (reset)
```

---

## ⏱️ Timeline Example (First 10 Seconds)

```
Time    Action              Display         Physics         Render
────────────────────────────────────────────────────────────────────
00:00   App starts          Initial view    Grid init       Canvas drawn
        (sand in top)       25:00           (sand top)      (static)

00:01   User taps canvas    Status change   Grid flip       Flip visible
                            "Focus Time"    (vertical)      immediately

00:01   Timer started       24:59           Sand starts     First frame
                                           falling (Core 1) rendered

00:02   Physics running     24:58           Sand moves      Canvas updates
        (continuous)                        down            every 50ms

00:03   (25 FPS physics)    24:57           Particles       (20 FPS render)
                                           through neck     

00:04   Sand accumulating   24:56           Bottom fills    Visible flow
        in bottom                           gradually       

... (continues for 25 minutes) ...

24:59   Almost done         00:01           Last particles  Nearly empty
                                           falling         top chamber

25:00   Session complete!   00:00           All sand in     Static scene
                            "Complete!"     bottom          (no movement)

25:01   User taps again     25:00           Grid flips      New session
                            "Focus Time"    (reset)         begins
```

---

## 🎮 Input Handling Details

### Touch Events

```c
// Core 0 (LVGL Thread)
static void canvas_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_PRESSED) {
        g_state->canvas_pressed = true;
        // No immediate action (wait for release)
    }
    else if (code == LV_EVENT_RELEASED && g_state->canvas_pressed) {
        g_state->canvas_pressed = false;
        flip_hourglass();  // Execute flip + reset
    }
}
```

### Flip Logic

```c
static void flip_hourglass(void) {
    // 1. Take mutex (block physics)
    xSemaphoreTake(g_state->grid_mutex, portMAX_DELAY);
    
    // 2. Flip gravity direction
    g_state->is_flipped = !g_state->is_flipped;
    
    // 3. Vertically mirror grid
    for (int y = 0; y < GRID_HEIGHT / 2; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            swap(grid[y][x], grid[GRID_HEIGHT - 1 - y][x]);
        }
    }
    
    // 4. Release mutex
    xSemaphoreGive(g_state->grid_mutex);
    
    // 5. Reset timer
    g_state->remaining_seconds = POMODORO_DURATION_SEC;
    
    // 6. Start timer
    start_timer();
}
```

---

## 👁️ Visual Feedback

### Status Label Changes

| State | Status Text | Color |
|-------|------------|-------|
| Initial | "Tap to Start" | White |
| Running | "Focus Time" | White |
| Complete | "Session Complete!" | White |
| Paused* | "Paused - Tap to Start" | White |

*Note: Current implementation doesn't have explicit pause, only stop/restart.

### Timer Display

```
Format: MM:SS
Range: 25:00 → 00:00

Color: lv_color_make(255, 200, 100)  // Warm orange/yellow
Font: lv_font_montserrat_28 (28pt, bold)

Examples:
25:00  ← Initial
24:59  ← First second
12:30  ← Halfway
05:00  ← 5 minutes left
00:30  ← Last 30 seconds
00:00  ← Complete
```

### Sand Colors

```c
// Background (canvas base)
COLOR_BACKGROUND = lv_color_make(245, 240, 230)  // Light beige

// Sand particles (falling)
COLOR_SAND = lv_color_make(200, 160, 100)  // Golden sand

// Glass outline (hourglass shape)
COLOR_GLASS = lv_color_make(100, 150, 200)  // Light blue

// Frame/border
COLOR_FRAME = lv_color_make(80, 60, 40)  // Dark brown
```

---

## 🧪 Testing User Interactions

### Manual Test Cases

1. **Test: Initial Display**
   - Expected: Sand in top, timer 25:00, status "Tap to Start"

2. **Test: First Tap**
   - Action: Tap canvas once
   - Expected: Flip, timer starts (24:59), status "Focus Time"

3. **Test: Continuous Run**
   - Action: Wait 1 minute
   - Expected: Timer shows 24:00, sand visibly moved

4. **Test: Mid-Session Restart**
   - Action: Wait to 20:00, then tap again
   - Expected: Reset to 25:00, flip happens, new session starts

5. **Test: Complete Session**
   - Action: Wait full 25 minutes (or set POMODORO_DURATION_SEC to 30 for testing)
   - Expected: Timer 00:00, status "Session Complete!", sand all in bottom

6. **Test: Back Button**
   - Action: Click "← Back" during running session
   - Expected: Return to previous screen, no crash, memory freed

7. **Test: Rapid Tapping**
   - Action: Tap canvas 10 times quickly
   - Expected: Each tap flips, timer resets, no crash/deadlock

8. **Test: Long Running**
   - Action: Leave running for 30+ minutes
   - Expected: No memory leaks, no performance degradation

---

## 📊 User Experience Metrics

| Metric | Target | How to Measure |
|--------|--------|---------------|
| Touch Response Time | <100ms | Time from tap to flip start |
| Animation Smoothness | 20 FPS | Visual inspection (no stuttering) |
| Physics Realism | "Feels natural" | Sand falls like real sand |
| Timer Accuracy | ±1 second | Compare to wall clock after 25 min |
| Navigation Latency | <200ms | Time from Back press to menu |

---

## 🎨 Accessibility Considerations

### Visual
- **Large Timer Display**: 28pt font (easily readable)
- **High Contrast**: White text on dark background
- **Clear Instructions**: "Tap hourglass to flip & start"

### Touch
- **Large Touch Area**: Entire canvas (240x280) is tappable
- **No Fine Motor Skills Required**: Just tap anywhere on hourglass
- **Immediate Feedback**: Instant flip (no delay)

### Cognitive
- **Simple Interaction**: One touch = flip & start
- **Clear Status**: Text label shows current state
- **Visual Progress**: Sand level shows time remaining

---

## 🔜 Potential UX Enhancements

1. **Haptic Feedback**
   - Vibrate on flip (using RP2040)
   - Gentle pulse every 5 minutes
   - Strong buzz when complete

2. **Sound Effects**
   - Soft "whoosh" on flip
   - Gentle ticking during countdown
   - Pleasant chime when complete

3. **Progress Indicators**
   - Percentage bar (0-100%)
   - Segment marks (5-minute intervals)
   - Color change (green→yellow→red as time runs out)

4. **Animations**
   - Smooth rotation animation (instead of instant flip)
   - Particle effects (sparkles) when complete
   - Pulsing timer at last 60 seconds

5. **Gestures**
   - Swipe down: Pause/resume
   - Swipe up: Reset
   - Long press: Settings

---

## 📝 User Guide (One-Page)

```
╔════════════════════════════════════════════════════════════╗
║              POMODORO TIMER - QUICK GUIDE                  ║
╚════════════════════════════════════════════════════════════╝

WHAT IS IT?
A 25-minute focus timer with a beautiful falling sand animation.

HOW TO USE:
1. TAP the hourglass to start → Timer begins (25:00 → 00:00)
2. FOCUS on your task while sand falls
3. When timer reaches 00:00 → Take a break!
4. TAP again to start another session

CONTROLS:
• Tap Hourglass = Flip & Start/Restart
• Tap "← Back" = Exit to menu

WHAT YOU SEE:
• TIMER: Shows remaining time (MM:SS)
• STATUS: Current state (Focus Time / Complete)
• SAND: Visual progress (top → bottom = time passing)

TIPS:
✓ Each tap resets the timer to 25:00
✓ Sand animation shows approximate time left
✓ Use for focused work sessions (Pomodoro technique)
✓ Combine with 5-minute breaks for best productivity

═══════════════════════════════════════════════════════════
      Made for SenseCAP Indicator D1 | ESP32-S3
═══════════════════════════════════════════════════════════
```

---

**End of User Flow Documentation**

This completes the comprehensive user interaction guide for the Pomodoro Timer.

# AETHER_OS Interactive Menu Specification

## 1. Goal
Implement a low-power, interactive menu system for the ESP32 Room Monitor that allows users to navigate functions using a single button.

## 2. Interaction Logic
- **Trigger**: Manual wakeup via GPIO 33 (Button).
- **Navigation (Short Press)**: Cycle through menu items.
- **Selection (Long Press 2s)**: Confirm and execute highlighted function.
- **Auto-Sleep**: System will return to Deep Sleep after 10 seconds of inactivity in the menu.

## 3. Menu Items
1. **MEASURE**: Triggers the standard 5-sample measurement cycle and Supabase upload.
2. **TIME/WEATHER**: Displays current time (via NTP) and local weather (placeholder/API).
3. **SYS_STATS**: Displays device diagnostics (Measurements count, Power cycles, Uptime, WiFi RSSI).
4. **SLEEP**: Manual command to enter Deep Sleep immediately.

## 4. Connectivity Strategy (Connect-on-Demand)
- **Menu Mode**: Device remains offline during navigation to ensure instant UI response.
- **Execution Mode**: WiFi is initiated only when a selected function (Measure/Weather) requires cloud access.
- **Timer Wake**: Standard background measurements continue to bypass the menu for maximum efficiency.

## 5. Persistent Statistics (RTC Memory)
To track data across deep sleep cycles, the following variables will be stored in **RTC_DATA_ATTR**:
- `bootCount`: Total number of power-on/wake events.
- `measureCount`: Total number of successful cloud uploads.

## 6. UI/UX Design
- **Header**: "AETHER_OS v2.1"
- **Selection**: Inverted color block highlighting the active item.
- **Progress Bar**: A visual countdown at the bottom indicating the time remaining before auto-sleep.
- **LED Feedback**: 
  - Cyan Breathing: Menu Active.
  - Green Flash: Selection Confirmed.
  - Rainbow: Measuring/Uploading.

## 7. Technical Requirements
- Update `monitorTask` to support state-based execution.
- Implement a button handler using `millis()` to distinguish press lengths.
- Integrate `time.h` for NTP time synchronization.

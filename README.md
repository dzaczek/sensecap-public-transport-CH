# SenseCAP Indicator - Public Transport Monitor
![IMG_2392](https://github.com/user-attachments/assets/b7e9e707-1367-475a-8f56-98d6d0495c97)


A standalone ESP32-S3 project for the SenseCAP Indicator that displays real-time public transport departures (Bus & Train).


https://github.com/user-attachments/assets/1254bb7c-477e-4dfc-a4a2-b6e64ac58457


This application fetches data from the [Open Transport Data Swiss API](https://transport.opendata.ch/) and displays it on the device's screen using LVGL.

## Features

- **Bus Countdown:** Shows minutes remaining until the next bus departures.
- **Train Station Board:** Displays upcoming train departures, platforms, and delays.
- **Smart Refresh:** 
  - Updates every 5 minutes during the day (06:00 - 21:00).
  - Updates every 15 minutes at night to save power.
- **Morning Schedule:** Keeps the screen active during morning rush hour (06:15 - 07:15, Mon-Fri).
- **WiFi Connectivity:** Connects to your local network to fetch API data.

## Configuration

### Managing stations and stops

The list of bus/tram stops and train stations available in the app is defined in **`main/view/indicator_view.c`**:

- **Bus/tram stops:** edit the `predefined_bus_stops[]` array — add or change entries as `{"Name", "ID"}`.
- **Train stations:** edit the `predefined_stations[]` array the same way.

Find stop and station IDs via the API: `http://transport.opendata.ch/v1/locations?query=CITY_NAME`

Optionally, in **`main/model/transport_data.h`** (USER CONFIGURATION section) you can set default `BUS_STOP_*` / `TRAIN_STATION_*` and `SELECTED_BUS_LINES` (line filter, e.g. `"1,2,4"` or `"*"` for all).

## Build & Flash

This project is standalone and includes all necessary components.

### Prerequisites

- ESP-IDF v5.1.1 or later.
- SenseCAP Indicator Board (ESP32-S3).

### Building

1. Navigate to the project directory:
   ```bash
   cd indicator_public_transport
   ```

2. Set the target to ESP32-S3:
   ```bash
   idf.py set-target esp32s3
   ```

3. Build the project:
   ```bash
   idf.py build
   ```

4. Flash and Monitor:
   ```bash
   idf.py -p /dev/tty.usbmodem123456 flash monitor
   ```
   *(Replace `/dev/tty...` with your actual serial port)*

## Privacy & Security

- This project does **not** contain any hardcoded WiFi credentials in the source code.
- Credentials are managed via the device's UI (WiFi Config Screen) or `sdkconfig` (which is git-ignored).
- The `sdkconfig` file containing local configuration is excluded from the repository.

## License

MIT License. See the LICENSE file for details.

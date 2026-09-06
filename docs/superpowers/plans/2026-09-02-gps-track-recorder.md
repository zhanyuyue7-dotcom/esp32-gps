# GPS Track Recorder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an ESP32-S3 cycling/running GPS recorder that reuses the proven SPI OLED/font code from `gas` and the responsive local-dashboard visual system from `esp32`.

**Architecture:** A host-testable `GpsTrackerCore` owns NMEA parsing, fix validation, activity state, distance/statistics, OLED layout, and JSON serialization. The ESP-IDF `main` layer owns UART, GPIO buttons, SPI OLED, SPIFFS persistence, Wi-Fi SoftAP, and HTTP streaming. Completed activities are stored as CSV plus a compact JSON summary; the phone page reads the list/detail APIs and exports CSV or GPX.

**Tech Stack:** ESP-IDF 5.5.x, C++17, ESP32-S3, NEO-6M NMEA 0183 at the hardware-verified 38400 baud, SSD1306 128x64 4-wire SPI, SPIFFS, ESP HTTP Server, native HTML/CSS/JavaScript, CMake/CTest/Node.js host tests.

---

## File structure

- `lib/GpsTrackerCore/NmeaParser.*`: byte-stream NMEA checksum, RMC, and GGA parsing.
- `lib/GpsTrackerCore/ActivityTracker.*`: recording state, validation, Haversine distance, speed/pace statistics.
- `lib/GpsTrackerCore/StatusJson.*`: stable status and activity-summary JSON contracts.
- `lib/GpsTrackerCore/OledFont.*` and `OledFontData.inc`: reused Atkinson Hyperlegible bitmap digits.
- `lib/GpsTrackerCore/OledLayout.*`: screen text/value formatting and metric-based centering.
- `main/Ssd1306.*` and `Font5x7.h`: reused SPI framebuffer plus complete compact ASCII labels.
- `main/main.cpp`: ESP-IDF hardware, storage, Wi-Fi, HTTP, tasks, and integration.
- `main/web_page.html`: mobile dashboard, activity list, route preview, download, and delete UI.
- `test/*`: host contracts for parser, activity logic, OLED layout, JSON, and web source.
- `docs/wiring.md`: exact GPIO wiring and first-power-on procedure.

### Task 1: Lock NMEA parsing behavior

**Files:**
- Create: `test/test_nmea_parser/test_main.cpp`
- Create: `lib/GpsTrackerCore/NmeaParser.h`
- Create: `lib/GpsTrackerCore/NmeaParser.cpp`

- [ ] Write tests for valid GPRMC/GNRMC and GPGGA/GNGGA sentences, checksum rejection, hemispheres, and malformed input.
- [ ] Run `cmake -S host -B build-host -G Ninja && cmake --build build-host --target test_nmea_parser` and verify RED because the parser does not exist.
- [ ] Implement the minimal byte-stream parser and rerun the test to GREEN.

### Task 2: Lock activity and filtering behavior

**Files:**
- Create: `test/test_activity_tracker/test_main.cpp`
- Create: `lib/GpsTrackerCore/ActivityTracker.h`
- Create: `lib/GpsTrackerCore/ActivityTracker.cpp`

- [ ] Test start/pause/resume/finish, cycling/running mode, duplicate timestamps, weak fixes, stationary drift, impossible jumps, distance, average speed, maximum speed, and pace.
- [ ] Verify RED before production code.
- [ ] Implement the state machine and Haversine/filter logic, then verify GREEN.

### Task 3: Reuse the proven OLED rendering system

**Files:**
- Create from `gas`: `main/Ssd1306.*`, `main/Font5x7.h`, `lib/GpsTrackerCore/OledFont.*`, `lib/GpsTrackerCore/OledFontData.inc`, `third_party/atkinson-hyperlegible/OFL.txt`
- Create: `test/test_oled_layout/test_main.cpp`
- Create: `lib/GpsTrackerCore/OledLayout.*`

- [ ] Test that cycling speed, running pace, distance, elapsed time, satellite count, and every device state fit 128x64.
- [ ] Verify RED before adding the GPS layout.
- [ ] Add complete small ASCII rendering while retaining Atkinson Hyperlegible for the primary digits.
- [ ] Implement GPS layouts and verify GREEN.

### Task 4: Lock the phone API and page contracts

**Files:**
- Create: `test/test_status_json/test_main.cpp`
- Create: `test/test_web_page/test_main.mjs`
- Create: `lib/GpsTrackerCore/StatusJson.*`
- Create: `main/web_page.html`

- [ ] Test exact status/summary JSON fields and escaping.
- [ ] Test mobile viewport, responsive palette, activity/detail panels, local SVG route, polling, CSV/GPX download, and confirmed delete.
- [ ] Verify both tests RED.
- [ ] Implement the serializers and adapt the `esp32` dashboard visual system, then verify GREEN.

### Task 5: Integrate ESP32-S3 hardware and persistence

**Files:**
- Create: `main/main.cpp`, `main/CMakeLists.txt`, `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`

- [ ] Configure OLED GPIO9-12, GPS UART GPIO4/5, and active-low buttons GPIO6/7.
- [ ] Mount SPIFFS and persist completed CSV/JSON activities.
- [ ] Register status, activity list/detail, CSV, GPX, and DELETE handlers.
- [ ] Start `TrackBox-XXXX` WPA2 SoftAP and serve the embedded page.
- [ ] Build with `idf.py set-target esp32s3` and `idf.py build`.

### Task 6: Document and verify the deliverable

**Files:**
- Create: `README.md`, `docs/wiring.md`

- [ ] Document exact crossed UART wiring, OLED 3.3 V rule, buttons-to-ground wiring, antenna placement, build/flash commands, and phone workflow.
- [ ] Run all host tests with `ctest --test-dir build-host --output-on-failure`.
- [ ] Run a clean ESP-IDF firmware build.
- [ ] Inspect the web page at desktop/mobile widths and confirm no horizontal overflow.
- [ ] Confirm R01-R08 and R10-R15 each maps to implementation and a verification step.

No commit steps are included because `esp32-gps` is not a Git repository.

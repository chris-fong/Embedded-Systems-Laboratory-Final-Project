# Embedded Systems Laboratory — Final Project
### Autonomous & Remote-Controlled BB Car Navigation System

**Course:** EE2405 Embedded Systems Laboratory (NTHU)
**Author:** Christopher · Student ID 112006233

A dual-board embedded system that drives a **BB Car** (Boe-Bot style robot) either
**autonomously** — following a printed line and reading barcodes with a Pixy2 camera — or
under **manual remote control** from a custom touchscreen GUI. The two boards talk to each
other over Wi‑Fi/Ethernet through an **MQTT broker**.

---

## 1. Overview

The system is split across two Mbed boards that never talk directly; they exchange short
commands through an MQTT topic (`car/control`) hosted on a Mosquitto broker running on a PC.

| Role | Board | Network | Job |
|------|-------|---------|-----|
| **Vehicle** | B‑L4S5I‑IOT01A (BB Car) | Wi‑Fi (onboard ISM43362) | Autonomous line/barcode navigation + manual driving |
| **Controller** | STM32F769I‑DISCO | Ethernet | LVGL touchscreen remote (mode toggle + D‑pad) |

The core engineering achievement is the **thread-safe integration of MQTT networking with
time-critical hardware loops** (the 50 Hz motor-control FSM on the car, and the LVGL render
loop on the remote) so that neither the driving nor the UI stalls while the network blocks.

```
   ┌─────────────────────────┐        MQTT: car/control        ┌─────────────────────────┐
   │  STM32F769I-DISCO        │   MODE_AUTO / MODE_MANUAL        │  B-L4S5I-IOT01A (BB Car) │
   │  "F769_Remote"           │   F / B / L / R / S             │  "BL_IOT_Car"            │
   │                          │ ──────────────────────────────▶ │                          │
   │  • LVGL touchscreen GUI  │        (via Mosquitto broker)   │  • Pixy2 line + barcode  │
   │  • Auto/Manual switch    │                                 │  • PD line-follow FSM    │
   │  • Forward/Back/Left/Rt  │                                 │  • Continuous-servo drive│
   │  • Publisher (Ethernet)  │                                 │  • Subscriber (Wi-Fi)    │
   └─────────────────────────┘                                 └─────────────────────────┘
```

---

## 2. Repository Layout

```
.
├── BB_Car/          # B-L4S5I-IOT01A firmware (the vehicle)
│   ├── main.cpp         # FSM: line follow → barcode → seek → stop, + MQTT manual mode
│   ├── CMakeLists.txt   # Links mbed-os, bbcar, pwmin, wifi-ism43362, paho_mqtt
│   ├── mbed_app.json / .json5
│   └── ...
├── F769/            # STM32F769I-DISCO firmware (the remote control)
│   ├── main.cpp         # LVGL GUI + MQTT publisher
│   ├── CMakeLists.txt   # Links mbed-os, lvgl, paho_mqtt, STM32F769 HAL/display
│   ├── lv_conf.h        # LVGL configuration
│   ├── mbed_app.json / .json5
│   └── ...
└── README.md
```

> **Note:** These folders contain only the application source and build scripts. The bulky
> library dependencies (`mbed-os`, `lvgl`, `bbcar`, `pwmin`, `Pixy2`, `paho_mqtt`,
> `components/wifi-ism43362`, and the F769 HAL/`hal_stm_lvgl` utilities) are **not committed**
> and must be dropped into each project folder before building — see
> [Building & Flashing](#5-building--flashing).

---

## 3. The Vehicle — `BB_Car/` (B‑L4S5I‑IOT01A)

The car runs a **finite state machine** in a 50 Hz main loop (`ThisThread::sleep_for(20ms)`).

### Hardware wiring
| Peripheral | Pins |
|------------|------|
| Left servo (control / feedback) | `D11` / `D9` |
| Right servo (control / feedback) | `D12` / `D10` |
| Pixy2 camera (SPI) | `PD_4`, `PD_3`, `PD_1`, `PD_5` |
| Wi‑Fi | onboard ISM43362 module |

### Autonomous mode (default)
- **Line following — PD controller.** The Pixy2 (in `line` program) reports the primary line
  vector. The controller computes the horizontal error between the frame center and the line
  midpoint, smooths it with an exponential filter (`0.6·raw + 0.4·prev`), applies a ±5 px
  deadband, and drives the wheels with `heading = Kp·err + Kd·Δerr` (`Kp = Kd = 2.0`).
  Base speed is scaled down proportionally to the error so the car slows into sharp turns.
- **Barcode handling — "coasting" logic.** A barcode is only acted on once it is close enough
  (`m_y > 40`); a 150-tick cooldown then prevents the same marker from firing twice. Barcodes
  are read **dynamically** off the track rather than from a stored map:

  | Code | Action |
  |------|--------|
  | `0` | Turn left |
  | `1` | Go straight |
  | `2` | Turn right |
  | `3` | Stop (spin in place, then halt) |

- **Line re-acquisition.** After a barcode turn the FSM enters `SEEK_LINE`, rotating toward the
  last turn direction until the line vector re-centers, then resumes following.

### Manual mode
When the remote sends `MODE_MANUAL`, the FSM stops processing the camera and maps single-char
MQTT commands straight to the motors: `F` forward, `B` back, `L`/`R` rotate, `S` stop.

### Fault-tolerant networking
Wi‑Fi and MQTT bring-up is fully wrapped in error handling. If the router, broker, or DHCP
lease is unavailable, the car prints the failure reason and boots straight into **offline
autonomous mode** — it stays fully drivable during a live demo even with no network. A
dedicated background thread continuously calls `client->yield()` so incoming manual commands
are never missed (no "sticky" buttons) without delaying the motor loop.

### FSM states
`STATE_FOLLOW_LINE` → `STATE_HANDLE_BARCODE` → `STATE_SEEK_LINE` → `STATE_STOP`

---

## 4. The Controller — `F769/` (STM32F769I‑DISCO)

A custom **LVGL touchscreen GUI** that acts as the remote control:

- **Auto/Manual toggle switch** — publishes `MODE_AUTO` / `MODE_MANUAL`.
- **Directional D‑pad** (▲ ▼ ◀ ▶) — publishes `F` / `B` / `L` / `R` on press and `S` on
  release, so the car stops the instant a button is let go.

### Threading model
To keep the touchscreen responsive while the network stack blocks, the board uses RTOS
multithreading:
- **UI thread** (4096‑byte stack) calls `lv_tick_inc()` + `lv_task_handler()` every 10 ms —
  the display renders instantly and never freezes during `net->connect()`.
- **Main thread** handles the blocking Ethernet + MQTT setup and publishes commands from a
  thread-safe `pending_cmd` flag set by the LVGL button callbacks.

---

## 5. MQTT Protocol

Both boards use topic **`car/control`**, QoS 0, via a Mosquitto broker.

| Payload | Direction | Meaning |
|---------|-----------|---------|
| `MODE_AUTO` | F769 → Car | Enter autonomous FSM |
| `MODE_MANUAL` | F769 → Car | Enter manual driving |
| `F` `B` `L` `R` | F769 → Car | Drive forward / back / rotate left / right (manual) |
| `S` | F769 → Car | Stop |

Client IDs: `F769_Remote` (publisher), `BL_IOT_Car` (subscriber).

---

## 6. Building & Flashing

Built with the **Mbed CE** toolchain (CMake + Ninja + arm-none-eabi-gcc).

### Prerequisites
1. Both boards on the **same local subnet**.
2. A **Mosquitto MQTT broker** running on a host PC.
3. Update the configuration constants at the top of **both** `main.cpp` files:
   - `broker_ip` → the broker PC's local IP (default `192.168.50.176`, port `1883`)
   - On the car: `WIFI_SSID` / `WIFI_PASS` (hard-coded in `BB_Car/main.cpp`)

> Also drop the required library folders into each project before configuring — see the note in
> [Repository Layout](#2-repository-layout).

### Remote — F769 board
```bash
# Connect the Ethernet cable first
cd F769
mkdir build && cd build
cmake .. -GNinja -DMBED_TARGET=DISCO_F769NI
ninja flash-F769_Remote
```

### Vehicle — BB Car (B‑L4S5I‑IOT01A)
```bash
cd BB_Car
# If you change WIFI_SSID / WIFI_PASS, delete build/ first so CMake regenerates config headers
rm -rf build
mkdir build && cd build
cmake .. -GNinja -DMBED_TARGET=B_L4S5I_IOT01A
ninja flash-MbedCEHelloWorld
```

---

## 7. Running the Demo
1. Power on the **F769** remote; wait for `Network Connected` on the serial monitor (115200 baud).
2. Power on the **BB Car**; wait for the Pixy2 to initialize and Wi‑Fi to connect (or it falls
   back to offline autonomous mode).
3. Use the touchscreen toggle to switch between **Autonomous** and **Manual**:
   - *Autonomous* — place the car on the printed track and let it follow the line and obey barcodes.
   - *Manual* — drive it directly with the on-screen D‑pad.

---

## 8. Engineering Challenges Solved
- **Blocking network calls freezing the GUI** → moved `lv_task_handler()` onto an isolated RTOS
  thread so the F769 UI renders while the main thread blocks on DHCP/MQTT.
- **Motors spinning instead of driving** → the two mirror-mounted continuous-rotation servos
  are inverted inside the `BBCar` driver so logical directions map to correct wheel rotation.
- **CMake caching stale config headers** → deleting `build/` forces regeneration of the
  Wi‑Fi/config macros after a settings change.
- **MQTT thread starvation ("sticky" buttons)** → `client->yield()` runs in its own background
  thread so manual commands are always serviced without slowing the 50 Hz control loop.

---

## 9. Project Requirements Mapping (EE2405 Final Project)
| Requirement | Where it's implemented |
|-------------|------------------------|
| Complete BB Car navigation task | `BB_Car/main.cpp` FSM |
| Use B‑L4S5I‑IOT01A **and** DISCO_F769NI | `BB_Car/` + `F769/` |
| Use MQTT, PixyCam | MQTT `car/control` topic; Pixy2 line/barcode |
| Straightaways, curves, branches, turns | PD line follower + `SEEK_LINE` re-acquisition |
| Barcodes for turn/straight/stop markers | 4 dynamically-read barcodes (codes 0–3) |
| MQTT remote control from F769 | LVGL GUI publisher (`F769/main.cpp`) |
| F769 GUI for remote control | Auto/Manual switch + directional D‑pad |
| No manual repositioning of the car | Offline autonomous fallback keeps it self-driving |

> The written report additionally proposes a **LaserPING obstacle-avoidance** special task.
> That behavior is *described in the report* but is **not present in the source in this repo** —
> the committed `BB_Car/main.cpp` implements line following, barcode handling, line seeking,
> stop, and manual override.

[![Video Title](https://img.youtube.com/vi/4gJ33trj6C8/0.jpg)](https://www.youtube.com/watch?v=4gJ33trj6C8)



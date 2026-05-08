# Agri3D Serial Commands & Bitmask Guide

The Agri3D firmware runs on a highly optimized version of GRBL, adapted specifically for TMC2209 stepper drivers and agricultural gantry robots. It communicates via serial commands sent from the ESP32 (or a PC).

## Supported G-Codes & M-Codes
Because Agri3D is an optimized gantry system, unnecessary CNC bloat (like arcs, probing, tool offsets, and spindle controls) has been completely stripped to save memory. 

### Motion & Coordinate G-Codes
- **`G0`**: Rapid linear motion (seek).
- **`G1`**: Linear motion at feed rate (`F`).
- **`G4 P<seconds>`**: Dwell / Pause for `<seconds>`.
- **`G20`**: Set units to inches.
- **`G21`**: Set units to millimeters (Default).
- **`G28` / `G30`**: Go to predefined position 1 or 2.
- **`G28.1` / `G30.1`**: Save current machine position as predefined position 1 or 2.
- **`G53`**: Move in absolute machine coordinates (ignores `$130-$132` soft limits temporarily).
- **`G80`**: Cancel active motion mode.
- **`G90`**: Absolute distance mode (Default).
- **`G91`**: Incremental distance mode.
- **`G92 X.. Y.. Z..`**: Set temporary coordinate offset (Zero the workspace).
- **`G92.1`**: Reset coordinate offsets.
- **`G93`**: Inverse time feed rate mode.
- **`G94`**: Units per minute feed rate mode (Default).

### Native Agri3D Hardware M-Codes
- **`M0` / `M1`**: Pause program execution.
- **`M2` / `M30`**: End program execution.
- **`M100`**: Water Relay ON
- **`M101`**: Water Relay OFF
- **`M102`**: Fertigation Relay ON
- **`M103`**: Fertigation Relay OFF
- **`M104`**: Weeder Relay ON
- **`M105`**: Weeder Relay OFF

---

## Core GRBL Commands

### `$$` (View/Edit EEPROM Settings)
Sending `$$` tells the firmware to print out all of its stored EEPROM settings. To change a setting, type the variable name, an equals sign, and the new value (e.g., `$132=500`).

Here is a complete table of all the EEPROM settings for Agri3D:

| Setting | Description | Units / Type |
| --- | --- | --- |
| **`$0`** | Step pulse time | Microseconds |
| **`$1`** | Step idle delay | Milliseconds (255 = Always On) |
| **`$2`** | Step port invert mask | Bitmask |
| **`$3`** | Direction port invert mask | Bitmask |
| **`$4`** | Step enable invert | Boolean (0 or 1) |
| **`$5`** | Limit pins invert | Boolean (0 or 1) |
| **`$6`** | Probe pin invert | Boolean (0 or 1) |
| **`$10`** | Status report mask | Bitmask |
| **`$11`** | Junction deviation | Millimeters |
| **`$12`** | Arc tolerance | Millimeters |
| **`$13`** | Report inches | Boolean (0 or 1) |
| **`$20`** | Soft limits enable | Boolean (0 or 1) |
| **`$21`** | Hard limits enable | Boolean (0 or 1) |
| **`$22`** | Homing cycle enable | Boolean (0 or 1) |
| **`$23`** | Homing dir invert mask | Bitmask |
| **`$24`** | Homing feed rate | mm/min |
| **`$25`** | Homing seek rate | mm/min |
| **`$26`** | Homing debounce delay | Milliseconds |
| **`$27`** | Homing pull-off | Millimeters |
| **`$100, $101, $102`** | X, Y, Z steps per mm | Steps/mm |
| **`$110, $111, $112`** | X, Y, Z max rate | mm/min |
| **`$120, $121, $122`** | X, Y, Z acceleration | mm/sec² |
| **`$130, $131, $132`** | X, Y, Z max travel | Millimeters |
| **`$140, $141, $142`** | X, Y, Z StallGuard thresholds | TMC2209 SGTHRS (0-255) |

#### Bitmask Settings (`$2`, `$3`, `$23`)
Several settings use "bitmasks" to target specific axes. 
The axes map to bits as follows:
* **Bit 0**: X-Axis (Value = 1)
* **Bit 1**: Y-Axis (Value = 2)
* **Bit 2**: Z-Axis (Value = 4)

To invert multiple axes, you add their values together. For example, to target X (1) and Z (4), the setting value is `5`.

- **`$2` (Step Port Invert Mask)**: Inverts the step signal logic. (0 = Active High, 1 = Active Low).
- **`$3` (Direction Port Invert Mask)**: Inverts the direction of motor rotation. If an axis moves the wrong way during manual jogging, add its bit value to `$3`.
- **`$23` (Homing Direction Invert Mask)**: Determines which way the axis seeks during the first phase of homing.
  - **0**: Seeks Positive (Up / Right)
  - **1**: Seeks Negative (Down / Left)
  - *Example:* If Z (Value 4) retracts downwards instead of upwards during Phase 0 homing, add `4` to `$23` to flip its seek direction.

---

### `?` (Status Report)
Requests an immediate, real-time status update. 
Returns: `<Idle|MPos:0.000,0.000,0.000|FS:0,0>`
- **State**: `Idle`, `Run`, `Hold`, `Jog`, `Alarm`, or `Homing`.
- **MPos**: The current Machine Position (X, Y, Z) in millimeters.
- **FS**: Feed rate and Spindle speed.

---

## Homing & Auto-Dimensioning Commands

### `$H` (Full Homing Cycle)
Initiates the complete, safe homing and dimensioning sequence:
1. **Phase 0:** Retracts Z-axis to the top safe height.
2. **Phase 1:** Auto-dimensions X and Y axes (bounces between both ends using StallGuard).
3. **Phase 2:** Auto-dimensions the Z-axis (dives to the bottom switch and returns to the top).

### Single-Axis Homing (`$HX`, `$HY`, `$HZ`)
- **`$HX` and `$HY`**: Executes the auto-dimensioning bounce sequence for only the X or Y axis.
- **`$HZ`**: Executes a standard retract to the top switch, followed immediately by the dive-to-bottom auto-dimensioning sequence for Z.

---

## Agri3D Custom TMC2209 Commands

### `$TMC` (TMC Diagnostics)
Prints a human-readable diagnostic report for all four stepper drivers (X, Y1, Y2, Z). Decodes the internal registers to tell you communication status, over-temperature warnings, short circuits, and current StallGuard thresholds.

### `$TRAWX`, `$TRAWY`, `$TRAWW`, `$TRAWZ` (Raw Register Read)
Performs a raw byte dump directly from the TMC2209 silicon. 
- **`W`** represents the **Y2** motor (secondary Y-axis gantry motor).

Prints raw hex bytes for `GSTAT` and `DRV_STATUS` registers.

#### GSTAT (Global Status Register) Bits:
- **Bit 0 (reset)**: 1 = The IC has been reset since the last read.
- **Bit 1 (drv_err)**: 1 = Driver has been shut down due to an error (short circuit or over-temperature).
- **Bit 2 (uv_cp)**: 1 = Charge pump undervoltage.

#### DRV_STATUS (Driver Status Register) Bits:
- **Bit 0 (otpw)**: Overtemperature pre-warning (Warning flag).
- **Bit 1 (ot)**: Overtemperature flag (Driver disabled).
- **Bit 2 (s2ga)**: Short to ground on phase A.
- **Bit 3 (s2gb)**: Short to ground on phase B.
- **Bit 4 (s2vsa)**: Short to supply on phase A.
- **Bit 5 (s2vsb)**: Short to supply on phase B.
- **Bit 6 (ola)**: Open load on phase A.
- **Bit 7 (olb)**: Open load on phase B.
- **Bits 16-25 (sg_result)**: 10-bit StallGuard mechanical load measurement.

### `$TLOG` (Continuous StallGuard Stream)
Puts the firmware into a continuous logging mode where it rapidly prints the current mechanical load (`SG_RESULT`) on all four motors every ~250ms. 
- A low number means high resistance/crashing.
- A high number (near 511) means the motor is moving freely.
- Sending **any** character to the serial console will stop the stream.

---

## Alarm Codes

When the machine encounters a critical safety violation or failure, it will lock the system and report an Alarm code. Sending `$X` (Unlock) will clear the alarm state, but you should resolve the issue first.

| Code | Alarm Type | Description |
| --- | --- | --- |
| **`ALARM:1`** | Hard Limit | A physical limit switch or StallGuard sensor was triggered outside of a homing cycle. The machine has halted to prevent a crash. |
| **`ALARM:2`** | Soft Limit | The requested motion exceeds the machine's maximum travel boundaries (`$130-$132`). The motion was rejected. |
| **`ALARM:3`** | Abort Cycle | A system reset or abort command was issued while the machine was actively moving. Position may be lost. |
| **`ALARM:6`** | Homing Fail (Reset) | A system reset was issued during the homing cycle, interrupting it. |
| **`ALARM:7`** | Homing Fail (Door) | A safety door was opened during the homing cycle (rarely used in Agri3D). |
| **`ALARM:8`** | Homing Fail (Pull-off) | The machine failed to pull off the limit switch after triggering it. The switch may be stuck or broken. |
| **`ALARM:9`** | Homing Fail (Approach) | The machine moved its maximum configured distance but never found the limit switch. |

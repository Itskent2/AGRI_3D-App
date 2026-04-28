/*
  Agri3D Custom Grbl Test Sketch

  Grbl is a complete native C program that hijacks the standard Arduino
  main() loop to execute highly deterministic, microsecond-accurate stepper
  timing. Because of this, the Arduino setup() and loop() functions below
  are completely bypassed and ignored.

  This .ino file acts as the "Carrier" strictly to compile and upload
  the Agri3D firmware to your Arduino Nano.

  ---------------------------------------------------------------------------
  HOW TO TEST YOUR CUSTOM GRBL FIRMWARE:
  ---------------------------------------------------------------------------
  1. Select your Board (Arduino Nano) and COM Port.
  2. Click 'Upload' to flash the firmware to the Nano.
  3. Open the Arduino IDE Serial Monitor (Ctrl+Shift+M).
  4. Set Baud Rate to **115200**.
  5. Set Line Ending to **"Carriage return"** or **"Both NL & CR"**.

  Once connected, you should see the Grbl Boot Message: `Grbl 1.1h ['$']`

  ---------------------------------------------------------------------------
  TEST PROTOCOL (Run these commands directly in the Serial Monitor):
  ---------------------------------------------------------------------------

  [1] TEST CUSTOM RELAYS:
  Type `M3` and press Enter -> Water Pump Relay (PB1 / D9) should turn ON.
  Type `M4` and press Enter -> Fertilizer Pump Relay (PB2 / D10) should turn ON.
  Type `M5` and press Enter -> Water & Fertilizer Relays should instantly turn
  OFF. Type `M8` and press Enter -> Weeder Motor (PB3 / D11) should turn ON.
  Type `M9` and press Enter -> Weeder Motor and all other Relays should turn OFF
  (Safety Halt).

  [2] TEST STEPPER MOTORS:
  Type `$X` to unlock the machine if it boots in Alarm state.
  Type `G91` to set Relative Positioning mode.
  Type `G1 X10 F500` -> The X-Axis stepper should move 10mm at a rate of
  500mm/min. Type `G1 Y-10 F500` -> The Y-Axis stepper should move -10mm. Type
  `G1 Z5 F500` -> The Z-Axis stepper should move 5mm.

  [3] TEST TELEMETRY (REPORTING):
  Type `?` and press Enter -> You should receive a status report containing
  machine position, work position, and our custom relay state!
  Example: `<Idle|MPos:0.000,0.000,0.000|FS:0,0|Relays:0>`
*/

#include <grbl-agri3d.h>

void setup() {
  // Grbl is fully native C and bypasses Arduino setup hooks.
}

void loop() {
  // Grbl executes in its own internal high-speed infinite loop.
}

// TMC2209 Simulator for Arduino Nano
// Simulates a TMC2209 stepper driver to monitor STEP, DIR, and PDN_UART
// from a GRBL master controller.

#include <SoftwareSerial.h>

// --- Pin Definitions ---
const int STEP_PIN = 2;  // External Interrupt 0
const int DIR_PIN = 3;   // External Interrupt 1 (or just digital read)
const int SIM_PIN_A = 4; // Edge case selector bit 1
const int SIM_PIN_B = 5; // Edge case selector bit 0
const int UART_RX_PIN = 10;
const int UART_TX_PIN = 11;

// --- State Variables ---
volatile long stepCount = 0;
volatile int currentDir = 1;
volatile uint16_t microstepCounter = 0; // 0-1023 for a full electrical cycle

// 0: Normal, 1: Overtemp Error, 2: Overtemp Warning, 3: Short to Ground
int simState = 0;
int lastSimState = -1;

// Phase state visualization helpers
const char *phases[4] = {"A1:+, B1:+", "A1:-, B1:+", "A1:-, B1:-",
                         "A1:+, B1:-"};

// SoftwareSerial for PDN_UART
SoftwareSerial tmcSerial(UART_RX_PIN, UART_TX_PIN);

// --- Initialization ---
void setup() {
  Serial.begin(115200); // Hardware serial for Serial Monitor debugging

  // Set up pins
  pinMode(STEP_PIN, INPUT_PULLUP);
  pinMode(DIR_PIN, INPUT_PULLUP);
  pinMode(SIM_PIN_A, INPUT_PULLUP);
  pinMode(SIM_PIN_B, INPUT_PULLUP);

  // Attach interrupt for STEP (Rising edge typically)
  attachInterrupt(digitalPinToInterrupt(STEP_PIN), onStep, RISING);

  // Initialize SoftwareSerial for TMC UART communication at typical baud rate
  tmcSerial.begin(19200); // Grbl usually defaults to 19200 or 115200 for TMC

  Serial.println("TMC2209 Simulator Ready!");
  Serial.println(
      "Listening on STEP (D2), DIR (D3), UART_RX (D10), UART_TX (D11)");

  Serial.println("\n--- Edge Case Simulation Configuration ---");
  Serial.println("Use Physical Pins D4 (Bit 1) and D5 (Bit 0) connected to GND "
                 "to select state:");
  Serial.println("  [D4:OPEN, D5:OPEN] -> '0': Normal Status");
  Serial.println("  [D4:OPEN, D5:GND ] -> '1': Overtemperature Error (OT)");
  Serial.println("  [D4:GND , D5:OPEN] -> '2': Overtemperature Warning (OTPW)");
  Serial.println("  [D4:GND , D5:GND ] -> '3': Short to Ground (S2G)");
  Serial.println("------------------------------------------");
}

// --- Interrupt Service Routine for STEP ---
void onStep() {
  // Read direction exactly when step occurs
  currentDir = digitalRead(DIR_PIN) == HIGH ? 1 : -1;
  stepCount += currentDir;

  // TMC2209 MSCNT goes from 0 to 1023 representing a full electrical wave
  // Each full step advances the phase by 256 microsteps (assuming 1/256 internal interp)
  if (currentDir > 0) {
    microstepCounter = (microstepCounter + 256) % 1024;
  } else {
    microstepCounter = (microstepCounter < 256) ? (1024 - 256) : (microstepCounter - 256);
  }
}

// --- UART Protocol Helpers ---
uint8_t calcTmcCrc(uint8_t *datagram, uint8_t datagramLength) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < datagramLength; i++) {
    uint8_t currentByte = datagram[i];
    for (uint8_t j = 0; j < 8; j++) {
      if ((crc >> 7) ^ (currentByte & 0x01)) {
        crc = (crc << 1) ^ 0x07;
      } else {
        crc = (crc << 1);
      }
      currentByte = currentByte >> 1;
    }
  }
  return crc;
}

// --- Main Loop ---
unsigned long lastPrintTime = 0;
long printedStepCount = 0;

void loop() {
  // 1. Process Hardware Pins for Edge Case Simulation
  // Using active-low logic (pull to GND = 1)
  int bit1 = digitalRead(SIM_PIN_A) == LOW ? 1 : 0;
  int bit0 = digitalRead(SIM_PIN_B) == LOW ? 1 : 0;

  simState = (bit1 << 1) | bit0;

  if (simState != lastSimState) {
    if (simState == 0)
      Serial.println("--> Edge Case State: NORMAL STATUS");
    else if (simState == 1)
      Serial.println("--> Edge Case State: OVERTEMPERATURE ERROR (OT)");
    else if (simState == 2)
      Serial.println("--> Edge Case State: OVERTEMPERATURE WARNING (OTPW)");
    else if (simState == 3)
      Serial.println("--> Edge Case State: SHORT TO GROUND (S2G)");
    lastSimState = simState;
  }

  // 2. Process UART incoming requests from GRBL Master
  if (tmcSerial.available()) {
    // Read datagram: sync(1) + addr(1) + reg(1) + [data(4) if write] + crc(1)
    // For simplicity, let's just read bytes and try to identify basic commands.
    uint8_t sync = tmcSerial.read();
    if (sync == 0x05) { // Sync byte
      // Wait up to 15ms for the remaining 3 bytes of the read request
      unsigned long startWait = millis();
      while(tmcSerial.available() < 3 && millis() - startWait < 15) {
         delay(1);
      }

      if (tmcSerial.available() >= 3) {
        uint8_t slaveAddr = tmcSerial.read();
        uint8_t regAddr = tmcSerial.read();

        // Assume this Simulator acts ONLY as the X-Axis (Address 0x00)
        // If GRBL is asking for Y or Z, remain completely silent so we don't
        // accidentally reply over them and corrupt the UART lines!
        if (slaveAddr != 0x00) {
           return; 
        }

        uint8_t isWrite = (regAddr & 0x80) ? 1 : 0;
        regAddr = regAddr & 0x7F;

        if (isWrite) {
          if (tmcSerial.available() >= 5) { // 4 data + 1 CRC
            uint32_t data = 0;
            data |= ((uint32_t)tmcSerial.read() << 24);
            data |= ((uint32_t)tmcSerial.read() << 16);
            data |= ((uint32_t)tmcSerial.read() << 8);
            data |= tmcSerial.read();
            uint8_t crc = tmcSerial.read();

            Serial.print("[UART] WRITE to Reg 0x");
            Serial.print(regAddr, HEX);
            Serial.print(" Value: 0x");
            Serial.println(data, HEX);
          }
        } else {
          uint8_t crc = tmcSerial.read(); // Read CRC of read request
          Serial.print("[UART] READ request to Reg 0x");
          Serial.println(regAddr, HEX);

          // Simulate a reply datagram
          // Sync, MasterAddr(0xFF), Reg, Data(4), CRC
          uint8_t reply[8];
          reply[0] = 0x05;
          reply[1] = 0xFF;
          reply[2] = regAddr;

          uint32_t replyData = 0;
          if (regAddr == 0x00) {        // GCONF
            replyData = 0x000001C0;     // Some default
          } else if (regAddr == 0x01) { // GSTAT
            if (simState == 1 || simState == 3) {
              replyData |= (1 << 1); // drv_err flag
            }
          } else if (regAddr == 0x6A) { // MSCNT (Microstep Counter)
            replyData = microstepCounter & 0x3FF; // 10-bit MSCNT
          } else if (regAddr == 0x6F) { // DRV_STATUS
            replyData = 0x80000000;     // Standstill

            // Add simulated errors (Bitmasks are approximations dependent on
            // TMC variant, but Grbl usually checks specific bits like
            // 120C/143C/150C/157C or s2ga/s2gb)
            if (simState == 1) {
              // Simulate Overtemp Error
              // TMCStepper typically checks bit 24 or similar depending on the
              // class, for TMC2209 bits 21-23 are temp prewarnings, bit 20 is
              // temp error sometimes, or just GSTAT drv_err is sufficient.
              // We'll set a few high bits to trigger generic DRIVER ERROR
              replyData |= (1UL << 20); // 157C Overtemp
            } else if (simState == 2) {
              // Simulate Overtemp Warning
              replyData |= (1UL << 21); // 150C Warning (or 143C/120C)
            } else if (simState == 3) {
              // Simulate Short to Ground
              replyData |= (1UL << 27) | (1UL << 26); // s2ga and s2gb
            }
          }

          reply[3] = (replyData >> 24) & 0xFF;
          reply[4] = (replyData >> 16) & 0xFF;
          reply[5] = (replyData >> 8) & 0xFF;
          reply[6] = replyData & 0xFF;
          reply[7] = calcTmcCrc(reply, 7);

          tmcSerial.write(reply, 8);
          Serial.print("       -> Replica sent (Data: 0x");
          Serial.print(replyData, HEX);
          Serial.println(")");
        }
      }
    }
  }

  // 3. Periodically print STEP/DIR and Coil states to Serial Monitor
  // Print if there was a change and 500ms have passed (to avoid spamming)
  if (stepCount != printedStepCount && millis() - lastPrintTime > 200) {
    noInterrupts();
    long currentSteps = stepCount;
    int dir = currentDir;
    uint16_t phase = microstepCounter;
    interrupts();

    uint16_t phaseIdx = (phase / 256) % 4;

    Serial.print("Position: ");
    Serial.print(currentSteps);
    Serial.print("\tDIR: ");
    Serial.print(dir > 0 ? "FORWARD  " : "REVERSE  ");
    Serial.print("\tMSCNT Bits: ");
    Serial.print(phase);
    Serial.print("\tPhase: ");
    Serial.println(phases[phaseIdx]);

    printedStepCount = currentSteps;
    lastPrintTime = millis();
  }
}

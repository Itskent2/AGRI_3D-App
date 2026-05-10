#include <Arduino.h>

// Bring in the GRBL TMC hardware API
#include "grbl-agri3d.h"
#include "tmc2209.h"
#include "tmc_uart.h"
#include "tmc_config.h"

// ---------------------------------------------------------
// 17HS8401 Motor Specifications
// ---------------------------------------------------------
// Rated Current: 1.7A Peak (approx 1.2A RMS)
// We will configure the TMC2209 IRUN for 1.2A RMS
// Rsense = 0.11 ohm on generic TMC2209 modules.
// CS = 32 * (I_rms * sqrt(2) * 0.11 + 0.02) / 0.325 - 1
// So an IRUN of ~23 is very safe and strong for this motor.
// ---------------------------------------------------------
#define MOTOR_IRUN  23
#define MOTOR_IHOLD 12 // Half current on standby

// X-Axis Pins from cpu_map.h
#define X_STEP_PIN    2
#define X_DIR_PIN     5
#define EN_PIN       A0

bool is_moving = false;
unsigned long last_step_time = 0;
int step_delay_us = 1000; // 1ms = fairly slow speed

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  // 1. Setup Arduino Pins
  pinMode(X_STEP_PIN, OUTPUT);
  pinMode(X_DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  
  digitalWrite(X_DIR_PIN, LOW); // Set arbitrary direction
  digitalWrite(EN_PIN, HIGH);   // Disable driver during boot
  
  Serial.println(F("\n\n--- TMC2209 17HS8401 Motor Test ---"));
  Serial.println(F("Initializing UART..."));
  
  // 2. Boot TMC UART subsystem
  tmc_uart_init();
  
  // 3. Initialize baseline TMC defaults (StallGuard, StealthChop, etc.)
  tmc_config_init_all();
  
  // 4. OVERRIDE X-Axis current for 17HS8401 (Address 0)
  Serial.println(F("Configuring IRUN=23 (1.2A RMS) for 17HS8401..."));
  
  uint32_t ihold_irun_val = 0;
  ihold_irun_val |= (MOTOR_IHOLD << 0);       // IHOLD
  ihold_irun_val |= ((uint32_t)MOTOR_IRUN << 8); // IRUN
  ihold_irun_val |= (1UL << 16);              // IHOLDDELAY (1)
  tmc_uart_write_reg(TMC_ADDR_X, TMC2209_REG_IHOLD_IRUN, ihold_irun_val);
  
  // Enable driver
  digitalWrite(EN_PIN, LOW);  // Active LOW Enable
  
  Serial.println(F("\nReady! Commands:"));
  Serial.println(F("  'm' - Start moving motor"));
  Serial.println(F("  's' - Stop motor"));
  Serial.println(F("  'p' - Print TMC driver stats"));
  Serial.println(F("  'd' - Toggle direction"));
  Serial.println(F("-----------------------------------"));
}

void loop() {
  // --- Serial Command Handling ---
  if (Serial.available()) {
    char c = Serial.read();
    
    if (c == 'm' || c == 'M') {
      is_moving = true;
      Serial.println(F("Motor: RUNNING"));
    } 
    else if (c == 's' || c == 'S') {
      is_moving = false;
      Serial.println(F("Motor: STOPPED"));
    }
    else if (c == 'd' || c == 'D') {
      bool current_dir = digitalRead(X_DIR_PIN);
      digitalWrite(X_DIR_PIN, !current_dir);
      Serial.print(F("Motor: DIRECTION "));
      Serial.println(!current_dir ? "HIGH" : "LOW");
    }
    else if (c == 'p' || c == 'P') {
      // Print Health Details
      Serial.println(F("\n--- TMC2209 Details ---"));
      tmc_report_details();
      Serial.println(F("-----------------------\n"));
    }
  }

  // --- Motor Movement Loop ---
  if (is_moving) {
    if (micros() - last_step_time >= step_delay_us) {
      last_step_time = micros();
      
      // Send a step pulse
      digitalWrite(X_STEP_PIN, HIGH);
      delayMicroseconds(2); // Short pulse
      digitalWrite(X_STEP_PIN, LOW);
    }
  }
}

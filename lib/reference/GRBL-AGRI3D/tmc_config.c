#include "tmc_config.h"
#include "grbl-agri3d.h"

#include "eeprom.h"
#include "tmc2209.h"
#include "tmc_uart.h"
#include <avr/io.h>
#include <util/delay.h>
#include "settings.h"

// ---------------------------------------------------------------------------
// Live EEPROM Updater
// ---------------------------------------------------------------------------
void tmc_update_sgthrs_live(uint8_t axis, uint8_t val) {
  uint8_t addrs[4] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2, TMC_ADDR_Z};
  if (axis == X_AXIS) tmc_uart_write_reg(addrs[0], TMC2209_REG_SGTHRS, val);
  if (axis == Y_AXIS) {
    tmc_uart_write_reg(addrs[1], TMC2209_REG_SGTHRS, val);
    tmc_uart_write_reg(addrs[2], TMC2209_REG_SGTHRS, val);
  }
  if (axis == Z_AXIS) tmc_uart_write_reg(addrs[3], TMC2209_REG_SGTHRS, val);
}

// Initialize driver parameters on boot
void tmc_config_init_all(void) {
  uint8_t addrs[4] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2, TMC_ADDR_Z};
  uint32_t iholds[4] = {TMC_X_IHOLD_IRUN, TMC_Y_IHOLD_IRUN, TMC_Y_IHOLD_IRUN,
                        TMC_Z_IHOLD_IRUN};

  // Iterate through all 4 configured driver addresses
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t addr = addrs[i];

    tmc_uart_write_reg(addr, TMC2209_REG_GCONF, TMC_DEFAULT_GCONF);

    tmc_uart_write_reg(addr, TMC2209_REG_CHOPCONF, TMC_DEFAULT_CHOPCONF);
    tmc_uart_write_reg(addr, TMC2209_REG_IHOLD_IRUN, iholds[i]);
    
    // Map driver index to axis for SGTHRS
    uint8_t axis = X_AXIS;
    if (i == 1 || i == 2) axis = Y_AXIS;
    else if (i == 3) axis = Z_AXIS;
    tmc_uart_write_reg(addr, TMC2209_REG_SGTHRS, settings.sgthrs[axis]);

    // Enable StallGuard DIAG for X/Y only. Z relies on physical limit switch.
    if (addr == TMC_ADDR_Z) {
      tmc_uart_write_reg(addr, TMC2209_REG_TCOOLTHRS, 0x00000000);
      tmc_uart_write_reg(addr, TMC2209_REG_TPWMTHRS, 0x00000000); 

    } else {
      tmc_uart_write_reg(addr, TMC2209_REG_TCOOLTHRS, 0x00002000); // Disable StallGuard below ~68 mm/min to prevent ALARM:8 false positive at end of pull-off

      // EXPLANATION: Hybrid SpreadCycle/StealthChop
      // TPWMTHRS determines the speed threshold where the driver switches from
      // silent StealthChop into high-torque SpreadCycle.
      // CRITICAL FIX: StallGuard ONLY works in StealthChop! If TPWMTHRS is too low,
      // homing speeds will switch to SpreadCycle and StallGuard will go blind!
      // Setting to 0 disables SpreadCycle switching (locks to StealthChop).
      tmc_uart_write_reg(addr, TMC2209_REG_TPWMTHRS, 0x00000000);

      // EXPLANATION: Dynamic Current Scaling (CoolStep)
      // COOLCONF configures StallGuard to dynamically increase motor current on
      // high torque demand and lower it when idling.
      // CRITICAL FIX: Disable CoolStep (0x00000000) so current stays constant and StallGuard thresholds don't fluctuate randomly.
      tmc_uart_write_reg(addr, TMC2209_REG_COOLCONF, 0x00000000);
    }
  }

  // Silence StallGuard DIAG output on X/Y immediately after init.
  // SGTHRS was written above for CoolStep calibration reference, but DIAG must
  // stay LOW during normal operation to prevent false hard-limit interrupts.
  // tmc_stallguard_enable() is called only when homing begins.
  tmc_stallguard_disable();

  // Configure Timer2 for time-based TMC polling.
  // At 16MHz, prescaler /1024 -> counter ticks at 15625 Hz.
  // Timer2 overflows every 256 ticks = ~16.38ms per overflow.
  TCCR2A = 0x00;                                    // Normal mode
  TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20); // Prescaler /1024
  // No interrupt needed. We'll poll TIFR2 manually.
}

// ---------------------------------------------------------------------------
// Software UART Enable / Disable
// ---------------------------------------------------------------------------
static bool steppers_are_enabled = true; // Assume enabled after tmc_config_init_all

void tmc_enable_steppers(void) {
  if (steppers_are_enabled) return; // Prevent UART spam and CHOPCONF resets
  steppers_are_enabled = true;

  uint8_t addrs[4] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2, TMC_ADDR_Z};
  for (uint8_t i = 0; i < 4; i++) {
    tmc_uart_write_reg(addrs[i], TMC2209_REG_CHOPCONF, TMC_DEFAULT_CHOPCONF);
  }
}

void tmc_disable_steppers(void) {
  if (!steppers_are_enabled) return; // Prevent UART spam
  steppers_are_enabled = false;

  uint8_t addrs[4] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2, TMC_ADDR_Z};
  // Set TOFF to 0 to disable
  uint32_t disabled_chopconf = TMC_DEFAULT_CHOPCONF & 0xFFFFFFF0;
  for (uint8_t i = 0; i < 4; i++) {
    tmc_uart_write_reg(addrs[i], TMC2209_REG_CHOPCONF, disabled_chopconf);
  }
}

// ---------------------------------------------------------------------------
// StallGuard DIAG Arming / Disarming
// ---------------------------------------------------------------------------
// The X and Y StallGuard DIAG pins are physically wired to the hard-limit
// PCINT pins. If SGTHRS > 0, any motor motion causes SG_RESULT to fluctuate
// which toggles DIAG, which fires the Pin Change ISR, which calls mc_reset().
// Solution: keep SGTHRS=0 during normal operation (silences DIAG), and only
// arm it just before homing begins.

void tmc_stallguard_enable(void) {
  uint8_t addrs_xy[3] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2};
  uint8_t axes_xy[3]  = {X_AXIS, Y_AXIS, Y_AXIS};
  for (uint8_t i = 0; i < 3; i++) {
    tmc_uart_write_reg(addrs_xy[i], TMC2209_REG_SGTHRS, settings.sgthrs[axes_xy[i]]);
  }
}

void tmc_stallguard_disable(void) {
  uint8_t addrs_xy[3] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2};
  for (uint8_t i = 0; i < 3; i++) {
    tmc_uart_write_reg(addrs_xy[i], TMC2209_REG_SGTHRS, 0);
  }
}

// ---------------------------------------------------------------------------
// StallGuard Auto-Calibration
// Call this while the motor is moving at a slow, known speed (e.g. during
// a calibration move commanded by the ESP32).
//
// How it works:
//   SG_RESULT is a 10-bit load indicator (511 = no load, 0 = stall/max load).
//   SGTHRS sets the stall threshold: stall triggers when SG_RESULT <= 2*SGTHRS.
//   We sample SG_RESULT N times, find the minimum (worst-case load in normal
//   motion), then divide by (2 * sensitivity) to set SGTHRS.
//
// sensitivity_divisor: 2 = triggers at ~25% extra load above baseline.
//                      3 = triggers at ~33% extra load (recommended).
//                      4 = triggers at ~50% extra load (less sensitive).
//
// Returns: new SGTHRS value written, or 0 if communication failed.
// ---------------------------------------------------------------------------
uint8_t tmc_calibrate_sgthrs(uint8_t driver_addr, uint8_t sensitivity_divisor) {
  uint16_t sg_min = 0x1FF; // Start at max (511 = no load)
  uint8_t valid_count = 0;

  for (uint8_t i = 0; i < 16; i++) {
    uint32_t raw = 0;
    if (tmc_uart_read_reg_async(driver_addr, TMC2209_REG_SG_RESULT, &raw)) {
      uint16_t sg_val = (uint16_t)(raw & 0x1FF); // 10-bit SG_RESULT
      if (sg_val < sg_min)
        sg_min = sg_val; // Track worst-case load
      valid_count++;
    }
    _delay_ms(10); // Space out readings across motion
  }

  if (valid_count == 0)
    return 0; // Communication failed

  // Avoid divide-by-zero and enforce minimum threshold
  if (sensitivity_divisor < 1)
    sensitivity_divisor = 1;
  uint8_t new_sgthrs = (uint8_t)(sg_min / (2 * sensitivity_divisor));
  if (new_sgthrs < 1)
    new_sgthrs = 1; // Never zero (would disable stall detection)
  if (new_sgthrs > 255)
    new_sgthrs = 255;

  tmc_uart_write_reg(driver_addr, TMC2209_REG_SGTHRS, (uint32_t)new_sgthrs);
  return new_sgthrs;
}

// ---------------------------------------------------------------------------
// Global state trackers for individual drivers [X, Y1, Y2, Z]
// 0 = OK, 1 = Disconnected, 2 = OTPW, 3 = OT Shutdown, 4 = Open Load, 5 = Short
// Circuit
// ---------------------------------------------------------------------------
uint8_t tmc_state[4] = {1, 1, 1, 1}; // Start as 1 to allow initial connect
                                     // broadcast if we want, or leave as 1.

// ---------------------------------------------------------------------------
// TMC Health Poll — called from protocol_main_loop() every iteration.
// We poll ONE driver every ~250ms (taking ~1 second for a full 4-driver cycle)
// to prevent starving GRBL's motion planner.
// ---------------------------------------------------------------------------
void tmc_config_poll_state(void) {
  static uint8_t ovf_count = 0;
  static uint8_t last_tcnt2 = 0;
  static uint8_t driver_idx =
      0; // Tracks which driver is being polled this tick

// Debounce: Reduced to 3 since we are polling slower
#define TMC_CONFIRM_COUNT 3
  static uint8_t confirm_state[4] = {1, 1, 1, 1};
  static uint8_t confirm_count[4] = {0, 0, 0, 0};

  uint8_t current_tcnt2 = TCNT2;
  if (current_tcnt2 < last_tcnt2) {
    ovf_count++;
  }
  last_tcnt2 = current_tcnt2;

  // 15 overflows at 16.38ms = ~245ms delay between polling each driver
  if (ovf_count < 15)
    return;
  ovf_count = 0;

  uint8_t addrs[4] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2, TMC_ADDR_Z};
  const char *const labels[4] = {"X ", "Y1", "Y2", "Z "};

  uint8_t i = driver_idx;
  uint32_t drv_status = 0;
  uint8_t new_state = 0;
  bool read_success = false;

  static uint8_t retry_count = 0;

  // Perform a SINGLE asynchronous read (does not contain delays).
  // If it fails, we will try again on the next TCNT2 overflow tick (~16ms).
  if (tmc_uart_read_reg_async(addrs[i], TMC2209_REG_DRV_STATUS, &drv_status)) {
    read_success = true;
    retry_count = 0;
  } else {
    retry_count++;
    if (retry_count < 3) {
      ovf_count = 14; // Force the next poll to happen in 1 overflow (~16ms) instead of 15
      return;
    }
    retry_count = 0; // Exhausted retries, report failure and move to next driver
  }

  // Parse state if read was successful
  if (read_success) {
    if (drv_status & (1UL << 1)) {
      new_state = 3; // Over-temp Shutdown (ot)
    } else if ((drv_status & 0x3C) && !(drv_status & (1UL << 31))) {
      // Short circuit (s2g/s2vs) — BUT only trust this while the motor is
      // actively moving! TMC2209 falsely asserts s2g at standstill due to
      // chopper current regulation, especially with high IHOLD values.
      // Bit 31 (stst) = 1 means standstill, so we IGNORE shorts at standstill.
      new_state = 5;
    } else if (drv_status & (0xC0)) {
      new_state = 4; // Open Load
    } else if (drv_status & (1UL << 0)) {
      new_state = 2; // Over-temp Pre-warning
    } else {
      new_state = 0; // Everything OK
    }
  } else {
    new_state = 1; // CRC consistently failed -> UART Disconnected
  }

  // --- Debounce Logic ---
  if (new_state == confirm_state[i]) {
    if (confirm_count[i] < 255)
      confirm_count[i]++;
  } else {
    confirm_state[i] = new_state;
    confirm_count[i] = 1;
  }

  // Apply state change if confirmed
  if (confirm_count[i] >= TMC_CONFIRM_COUNT && new_state != tmc_state[i]) {
    tmc_state[i] = new_state;
    confirm_count[i] = 0;

    printPgmString(PSTR("E:"));
    printString((char *)labels[i]);
    serial_write(':');
    print_uint8_base10(new_state);
    printPgmString(PSTR("\r\n"));
  }

  // --- Crash handler ---
  if (tmc_state[i] == 3 || tmc_state[i] == 5) {
    if (eeprom_get_char(EEPROM_ADDR_CRASH_LOG) == 0 &&
        eeprom_get_char(EEPROM_ADDR_CRASH_LOG2) == 0) {
      eeprom_put_char(EEPROM_ADDR_CRASH_LOG,
                      (tmc_state[0] & 0x0F) | (tmc_state[1] << 4));
      eeprom_put_char(EEPROM_ADDR_CRASH_LOG2,
                      (tmc_state[2] & 0x0F) | (tmc_state[3] << 4));
    }

    printPgmString(PSTR("[CRASH:"));
    printString((char *)labels[i]);
    serial_write(':');
    print_uint8_base10(tmc_state[i]);
    printPgmString(PSTR("]\r\n"));

    sys_rt_exec_alarm = EXEC_ALARM_HARD_LIMIT;

  }

  // Move to the next driver for the next polling tick
  driver_idx++;
  if (driver_idx >= 4)
    driver_idx = 0;
}
// Helper to overcome GRBL timer interrupt jitter during diagnostic reads
static bool read_with_retry(uint8_t addr, uint8_t reg, uint32_t *val) {
  for (uint8_t retry = 0; retry < 4; retry++) {
    if (tmc_uart_read_reg(addr, reg, val))
      return true;
    _delay_ms(2); // Brief pause before retry
  }
  return false;
}

// ---------------------------------------------------------------------------
// TMC2209 Diagnostic Reporter — called by $TMC serial command
// Reads GSTAT, DRV_STATUS, TSTEP, IOIN, MSCURACT for each driver and
// prints them to the serial port in a readable format.
// Example output:
//   [TMC:X  GSTAT:00 DRV:00140C03 TSTEP:FFFFFFFF IOIN:00000040 CURACT:0100FF00]
//   [TMC:Y1 n/a]
// ---------------------------------------------------------------------------
void tmc_report_details(void) {
  const uint8_t addrs[4] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2, TMC_ADDR_Z};
  const char *const labels[4] = {"X ", "Y1", "Y2", "Z "};

  uint32_t val;

  for (uint8_t i = 0; i < 4; i++) {
    printPgmString(PSTR("[TMC:"));
    printString((char *)labels[i]);
    serial_write(' ');

    if (read_with_retry(addrs[i], TMC2209_REG_GSTAT, &val)) {
      printPgmString(PSTR("GSTAT:"));
      print_uint32_base16_padded(val);
      serial_write(' ');

      if (read_with_retry(addrs[i], TMC2209_REG_DRV_STATUS, &val)) {
        // Temperature Thresholds (Bits 8-11)
        printPgmString(PSTR("T:"));
        if (val & (1UL << 11))
          printPgmString(PSTR(">157C "));
        else if (val & (1UL << 10))
          printPgmString(PSTR(">150C "));
        else if (val & (1UL << 9))
          printPgmString(PSTR(">143C "));
        else if (val & (1UL << 8))
          printPgmString(PSTR(">120C "));
        else
          printPgmString(PSTR("OK "));

        // CS_ACTUAL (Bits 16-20)
        printPgmString(PSTR("CS:"));
        print_uint8_base10((val >> 16) & 0x1F);
        serial_write(' ');

        // Operating mode / Status flags
        if (val & (1UL << 31))
          printPgmString(PSTR("STST ")); // Standstill (Bit 31)
        if (val & (1UL << 30))
          printPgmString(PSTR("STEALTH ")); // StealthChop mode (Bit 30)

        // Error flags
        if (val & (1UL << 7))
          printPgmString(PSTR("OLB ")); // Open load B (Bit 7)
        if (val & (1UL << 6))
          printPgmString(PSTR("OLA ")); // Open load A (Bit 6)
        if (val & (1UL << 5))
          printPgmString(PSTR("S2VSB ")); // Short to supply B (Bit 5)
        if (val & (1UL << 4))
          printPgmString(PSTR("S2VSA ")); // Short to supply A (Bit 4)
        if (val & (1UL << 3))
          printPgmString(PSTR("S2GB ")); // Short to ground B (Bit 3)
        if (val & (1UL << 2))
          printPgmString(PSTR("S2GA ")); // Short to ground A (Bit 2)
        if (val & (1UL << 1))
          printPgmString(PSTR("OT ")); // Over temp shutdown (Bit 1)
        if (val & (1UL << 0))
          printPgmString(PSTR("OTPW ")); // Over temp pre-warning (Bit 0)
      }

      if (read_with_retry(addrs[i], TMC2209_REG_TSTEP, &val)) {
        printPgmString(PSTR("TSTEP:"));
        print_uint32_base16_padded(val);
        serial_write(' ');
      }

      if (read_with_retry(addrs[i], TMC2209_REG_IOIN, &val)) {
        printPgmString(PSTR("IOIN:"));
        print_uint32_base16_padded(val);
        serial_write(' ');
      }

      if (read_with_retry(addrs[i], TMC2209_REG_MSCURACT, &val)) {
        printPgmString(PSTR("CURACT:"));
        print_uint32_base16_padded(val);
      }
    } else {
      printPgmString(PSTR("n/a"));
    }

    printPgmString(PSTR("]\r\n"));
  }
}
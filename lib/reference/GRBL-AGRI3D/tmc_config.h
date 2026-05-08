#ifndef TMC_CONFIG_H
#define TMC_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

// =========================================================
// Motor-specific IHOLD_IRUN Configurations
// =========================================================
// This 32-bit register controls motor current.
// Format: 0x00(IHOLDDELAY)(IRUN)(IHOLD)
// - IHOLD (Bits 0-4): Idle Holding Current (0-31). '10' Hex = 16.
// - IRUN (Bits 8-12): Running Current (0-31). '1F' Hex = 31 (Max allowed by
// VREF).
// - IHOLDDELAY (Bits 16-19): Clock cycles before powering down. '1' = Smooth
// power down.

// Profile A (High Current Mode - using 700W ATX PSU)
// 42HS40-2004 / 42HS48-2004 (2.0A MAX)
// IRUN=31 (1F), IHOLD=16 (10)
#define TMC_X_IHOLD_IRUN 0x00011F10
#define TMC_Y_IHOLD_IRUN 0x00011F10
// Z has IHOLD=20 (14 Hex) to hold the heavy tool head against gravity!
#define TMC_Z_IHOLD_IRUN 0x00011F14

// Profile B (Test/Safe Mode - Reduced current)
// #define TMC_X_IHOLD_IRUN 0x00010A05
// #define TMC_Y_IHOLD_IRUN 0x00010A05
// #define TMC_Z_IHOLD_IRUN 0x00010A05
// =========================================================

// CHOPCONF config: TBL=2, CHM=0, TOFF=4, MRES=4 (16 microstep), INTPOL=1,
// vsense=0 What this hex means: 0x1....... = Bit 28 (INTPOL) = 1 (Hardware 256
// Microstep Interpolation ENABLED) 0x.4...... = Bits 24-27 (MRES) = 4 (Standard
// 16 Microsteps) 0x..2..... = Bit 17 (vsense) = 1 (High Sensitivity, limits
// current to safe ~1.2A levels to prevent motor core saturation!) 0x......53 = Bits 0-7 (TOFF, HSTRT) = Standard
// TMC decay timings for NEMA17 high-torque
#define TMC_DEFAULT_CHOPCONF 0x14020053

// GCONF config: Global configuration flags
// What this hex means: (0x40 + 0x80 = 0xC0)
// Bit 2 (en_spreadCycle) = 0 (StealthChop2 mode — REQUIRED for StallGuard!)
// Bit 6 (pdn_disable) = 1 (ENABLE UART communication, disable legacy mode)
// Bit 7 (mstep_reg_select) = 1 (Microsteps set by UART MRES, ignore physical MS1/MS2 pins!)
#define TMC_DEFAULT_GCONF 0x000000C0

// GCONF Alternate config: en_spreadCycle = 0 (Use StealthChop2)
// Use this if you want silent operation (0x80 + 0x40 + 0x00 = 0xC0)
// #define TMC_DEFAULT_GCONF 0x000000C0

// SGTHRS (StallGuard Threshold for Homing)
// Value depends on mechanics, usually between 50 and 150
#define TMC_DEFAULT_SGTHRS 0x00000040

// TMC state tracker (0 = OK, 1 = OverTemp Warning, 2 = CRC Fail / Disconnected)
// Indices correspond to [X, Y1, Y2, Z]
extern uint8_t tmc_state[4];

void tmc_config_init_all(void);
void tmc_enable_steppers(void);
void tmc_disable_steppers(void);
void tmc_report_details(
    void); // Called by $TMC command — dumps key registers to serial

// Auto-calibrate StallGuard threshold for one driver while motor is moving.
// sensitivity_divisor: 2=sensitive, 3=recommended, 4=loose.
// Returns new SGTHRS written (0 = comms failure).
uint8_t tmc_calibrate_sgthrs(uint8_t driver_addr, uint8_t sensitivity_divisor);

// Polling routine designed to be called in grbl main loop.
// Uses Timer0 overflow counting for real ~1-second poll rate per driver.
void tmc_config_poll_state(void);

// StallGuard DIAG arming for homing. MUST be called before/after homing
// to prevent DIAG pin from triggering hard-limit ISR during normal motion.
// enable: writes configured SGTHRS to X/Y drivers (arms DIAG for stall detection)
// disable: writes SGTHRS=0 to X/Y drivers (silences DIAG pin completely)
void tmc_stallguard_enable(void);
void tmc_stallguard_disable(void);

#endif // TMC_CONFIG_H

/*
  config.h - compile time configuration for Agri3D
  All legacy CNC bloat (Laser, Spindle, Coolant, Overrides) purged.
*/

#ifndef config_h
#define config_h
#include "grbl-agri3d.h"

// =====================================================================
// DRIVER SELECTION: Dedicated TMC2209 Build
// Uses software bit-bang UART, StealthChop2, StallGuard2.
// =====================================================================

#define CPU_MAP_ATMEGA328P // Arduino Nano CPU

// Serial baud rate
#define BAUD_RATE 115200

// Realtime Commands
#define CMD_RESET 0x18 // ctrl-x.
#define CMD_STATUS_REPORT '?'
#define CMD_CYCLE_START '~'
#define CMD_FEED_HOLD '!'
#define CMD_SAFETY_DOOR 0x84
#define CMD_JOG_CANCEL 0x85
#define CMD_DEBUG_REPORT 0x86

// Core Feed & Rapid Overrides (Spindle/Coolant Overrides Purged)
#define CMD_FEED_OVR_RESET 0x90
#define CMD_FEED_OVR_COARSE_PLUS 0x91
#define CMD_FEED_OVR_COARSE_MINUS 0x92
#define CMD_FEED_OVR_FINE_PLUS 0x93
#define CMD_FEED_OVR_FINE_MINUS 0x94
#define CMD_RAPID_OVR_RESET 0x95
#define CMD_RAPID_OVR_MEDIUM 0x96
#define CMD_RAPID_OVR_LOW 0x97

// Homing Cycle (Z first, then X/Y)
#define HOMING_INIT_LOCK // Comment to disable
#define HOMING_SINGLE_AXIS_COMMANDS // Allows $HX, $HY, and $HZ
#define HOMING_CYCLE_0 (1 << Z_AXIS)
#define HOMING_CYCLE_1 ((1 << X_AXIS) | (1 << Y_AXIS))
#define N_HOMING_LOCATE_CYCLE 1

#define N_STARTUP_LINE 2

// Forced to Metric (Imperial support removed to save space)
#define N_DECIMAL_COORDVALUE_MM 3
#define N_DECIMAL_RATEVALUE_MM 0
#define N_DECIMAL_SETTINGVALUE 3

// Limit & Safety Checks
#define CHECK_LIMITS_AT_INIT

// Override Limits
#define DEFAULT_FEED_OVERRIDE 100
#define MAX_FEED_RATE_OVERRIDE 200
#define MIN_FEED_RATE_OVERRIDE 10
#define FEED_OVERRIDE_COARSE_INCREMENT 10
#define FEED_OVERRIDE_FINE_INCREMENT 1
#define DEFAULT_RAPID_OVERRIDE 100
#define RAPID_OVERRIDE_MEDIUM 50
#define RAPID_OVERRIDE_LOW 25
#define RESTORE_OVERRIDES_AFTER_PROGRAM_END

// Reporting Configuration
#define REPORT_FIELD_BUFFER_STATE
#define REPORT_FIELD_PIN_STATE
#define REPORT_FIELD_CURRENT_FEED_SPEED
#define REPORT_FIELD_WORK_COORD_OFFSET
#define REPORT_FIELD_OVERRIDES
#define REPORT_FIELD_LINE_NUMBERS

#define REPORT_OVR_REFRESH_BUSY_COUNT 20
#define REPORT_OVR_REFRESH_IDLE_COUNT 10
#define REPORT_WCO_REFRESH_BUSY_COUNT 30
#define REPORT_WCO_REFRESH_IDLE_COUNT 10

// Motion Planning Limits
#define ACCELERATION_TICKS_PER_SECOND 100
#define ADAPTIVE_MULTI_AXIS_STEP_SMOOTHING
#define MINIMUM_JUNCTION_SPEED 0.0 // (mm/min)
#define MINIMUM_FEED_RATE 1.0      // (mm/min)
#define N_ARC_CORRECTION 12
#define ARC_ANGULAR_TRAVEL_EPSILON 5E-7
#define DWELL_TIME_STEP 50 // ms

// EEPROM Protections
#define ENABLE_RESTORE_EEPROM_WIPE_ALL         // '$RST=*'
#define ENABLE_RESTORE_EEPROM_DEFAULT_SETTINGS // '$RST=$'
#define ENABLE_RESTORE_EEPROM_CLEAR_PARAMETERS // '$RST=#'
#define ENABLE_BUILD_INFO_WRITE_COMMAND        // '$I='
#define FORCE_BUFFER_SYNC_DURING_EEPROM_WRITE
#define FORCE_BUFFER_SYNC_DURING_WCO_CHANGE

#endif
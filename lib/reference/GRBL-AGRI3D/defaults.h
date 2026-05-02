/*
  defaults.h - defaults settings configuration file for Agri3D
  All legacy CNC profiles (Shapeoko, X-Carve, etc.) have been purged.
*/

#ifndef defaults_h
#define defaults_h

// Farmbot Ver3 Default Settings
#define DEFAULT_X_STEPS_PER_MM 250.0
#define DEFAULT_Y_STEPS_PER_MM 250.0
#define DEFAULT_Z_STEPS_PER_MM 250.0
#define DEFAULT_X_MAX_RATE 5000.0               // mm/min
#define DEFAULT_Y_MAX_RATE 5000.0               // mm/min
#define DEFAULT_Z_MAX_RATE 500.0                // mm/min
#define DEFAULT_X_ACCELERATION (50.0 * 60 * 60) // mm/sec^2 to mm/min^2
#define DEFAULT_Y_ACCELERATION (50.0 * 60 * 60)
#define DEFAULT_Z_ACCELERATION (10.0 * 60 * 60)
#define DEFAULT_X_MAX_TRAVEL 900   // mm (Adjust for your Farmbot size)
#define DEFAULT_Y_MAX_TRAVEL 900   // mm (Adjust for your Farmbot size)
#define DEFAULT_Z_MAX_TRAVEL 500.0 // mm (Adjust for your Farmbot size)

#define DEFAULT_STEP_PULSE_MICROSECONDS 10
#define DEFAULT_STEPPING_INVERT_MASK 0
#define DEFAULT_DIRECTION_INVERT_MASK 0
#define DEFAULT_STEPPER_IDLE_LOCK_TIME                                         \
  255 // 255 keeps steppers enabled (Important for heavy Z-axis)
#define DEFAULT_STATUS_REPORT_MASK 1    // MPos enabled
#define DEFAULT_JUNCTION_DEVIATION 0.01 // mm
#define DEFAULT_ARC_TOLERANCE 0.002     // mm
#define DEFAULT_REPORT_INCHES 0         // 0 = mm (Farmbot native)
#define DEFAULT_INVERT_ST_ENABLE 0      // false
#define DEFAULT_INVERT_LIMIT_PINS 0     // false
#define DEFAULT_SOFT_LIMIT_ENABLE 0     // false (requires homing first)
#define DEFAULT_HARD_LIMIT_ENABLE                                              \
  1 // TRUE — Required! StallGuard DIAG fires via limit ISR.
#define DEFAULT_INVERT_PROBE_PIN 0     // false
#define DEFAULT_HOMING_ENABLE 1        // True (Farmbots usually need homing)
#define DEFAULT_HOMING_DIR_MASK 3      // Adjust based on limit switch placement
#define DEFAULT_HOMING_FEED_RATE 50.0  // mm/min
#define DEFAULT_HOMING_SEEK_RATE 500.0 // mm/min
#define DEFAULT_HOMING_DEBOUNCE_DELAY 250 // msec
#define DEFAULT_HOMING_PULLOFF 5.0        // mm

#endif
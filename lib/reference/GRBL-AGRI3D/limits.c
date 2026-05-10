/*
  limits.c - code pertaining to limit-switches and performing the homing cycle
  Optimized for Agri3D TMC2209 StallGuard Interrupts via PORTC.
*/

#include "grbl-agri3d.h"
#include <avr/wdt.h>

// Homing axis search distance multiplier. Computed by this value times the
// cycle travel.
#define HOMING_AXIS_SEARCH_SCALAR                                              \
  1.5 // Must be > 1 to ensure limit switch will be engaged.
#define HOMING_AXIS_LOCATE_SCALAR                                              \
  5.0 // Must be > 1 to ensure limit switch is cleared.

void limits_init() {
  LIMIT_DDR &= ~(LIMIT_MASK); // Set as input pins
  // EXPLICITLY disable internal pull-ups for X and Y (TMC2209 DIAG pins)
  LIMIT_PORT &= ~((1 << X_LIMIT_BIT) | (1 << Y_LIMIT_BIT));
  // ENABLE internal pull-up for Z (Physical limit switch)
  LIMIT_PORT |= (1 << Z_LIMIT_BIT);

  if (bit_istrue(settings.flags, BITFLAG_HARD_LIMIT_ENABLE)) {
    LIMIT_PCMSK |=
        LIMIT_MASK; // Enable specific pins of the Pin Change Interrupt
    PCICR |= (1 << LIMIT_INT); // Enable Pin Change Interrupt
  } else {
    limits_disable();
  }
}

// Disables hard limits.
void limits_disable() {
  LIMIT_PCMSK &=
      ~LIMIT_MASK; // Disable specific pins of the Pin Change Interrupt
  PCICR &= ~(1 << LIMIT_INT); // Disable Pin Change Interrupt
}

// Returns limit state as a bit-wise uint8 variable. Each bit indicates an axis
// limit, where triggered is 1 and not triggered is 0. Invert mask is applied.
// Axes are defined by their number in bit position, i.e. Z_AXIS is (1<<2) or
// bit 2, and Y_AXIS is (1<<1) or bit 1.
uint8_t limits_get_state() {
  uint8_t limit_state = 0;
  uint8_t raw_pin = (LIMIT_PIN & LIMIT_MASK);

  // --- X and Y: TMC2209 StallGuard DIAG pins (Active-HIGH = stall detected) ---
  // These pins are driven directly by the TMC2209 DIAG output.
  // HIGH means a stall was detected. No inversion needed, ever.
  // The $5 (invert limit pins) setting must NOT affect these.
  uint8_t xy_mask = (1 << X_LIMIT_BIT) | (1 << Y_LIMIT_BIT);
  uint8_t xy_triggered = raw_pin & xy_mask; // Active-HIGH, read directly

  // --- Z: Physical limit switch (Normally-Open to GND, pulled HIGH) ---
  // When the switch is triggered (pressed), the pin goes LOW.
  // When not pressed, the internal pull-up keeps it HIGH.
  // We MUST invert this so that LOW = triggered (1).
  // We hardcode this inversion so it works perfectly regardless of the $5 EEPROM setting.
  uint8_t z_mask = (1 << Z_LIMIT_BIT);
  uint8_t z_triggered = (~raw_pin) & z_mask; // Always invert Z

  uint8_t pin = xy_triggered | z_triggered;

  if (pin) {
    uint8_t idx;
    for (idx = 0; idx < N_AXIS; idx++) {
      if (pin & get_limit_pin_mask(idx)) {
        limit_state |= (1 << idx);
      }
    }
  }
  return (limit_state);
}

// This is the Limit Pin Change Interrupt, which handles the hard limit feature.
// A bouncing limit switch can cause a lot of problems, like false readings and
// multiple interrupt calls. If a switch is triggered at all, something bad has
// happened and treat it as such, regardless if a limit switch is being
// disengaged. It's impossible to reliably tell the state of a bouncing pin
// because the Arduino microcontroller does not retain any state information
// when detecting a pin change. If we poll the pins in the ISR, you can miss the
// correct reading if the switch is bouncing.
ISR(LIMIT_INT_vect) // DEFAULT: Limit pin change interrupt process.
{
  // Disabled per user request: The limit switches are now strictly for auto-dimming (homing).
  // If triggered during normal operation, they will be ignored, allowing Soft Limits to govern boundaries.
}

// Homes the specified cycle axes, sets the machine position, and performs a
// pull-off motion after completing. Homing is a special motion case, which
// involves rapid uncontrolled stops to locate the trigger point of the limit
// switches. The rapid stops are handled by a system level axis lock mask, which
// prevents the stepper algorithm from executing step pulses. Homing motions
// typically circumvent the processes for executing motions in normal operation.
// NOTE: Only the abort realtime command can interrupt this process.
// TODO: Move limit pin-specific calls to a general function for portability.
void limits_go_home(uint8_t cycle_mask) {
  if (sys.abort) {
    return;
  } // Block if system reset has been issued.

  // Initialize plan data struct for homing motion.
  // Ensure Water and Fertigation relays are turned off during homing.
  // Active Low relays -> Set HIGH to turn OFF.
  // Leave Weeder relay as is per user request.
  RELAY_PORT |= (1 << WATER_PUMP_BIT) | (1 << FERT_PUMP_BIT);

  uint8_t is_auto_dim = (cycle_mask & (1 << 3));
  uint8_t target_axes = (cycle_mask & 0x07); // Mask out flags, keep only X, Y, Z

  uint8_t standard_mask = is_auto_dim ? 0 : target_axes;
  uint8_t auto_dim_mask = is_auto_dim ? target_axes : 0;

  // =========================================================================
  // Standard GRBL Homing (For Z axis)
  // =========================================================================
  if (standard_mask) {
    plan_line_data_t plan_data;
    plan_line_data_t *pl_data = &plan_data;
    memset(pl_data, 0, sizeof(plan_line_data_t));
    pl_data->condition =
        (PL_COND_FLAG_SYSTEM_MOTION | PL_COND_FLAG_NO_FEED_OVERRIDE);

    // Initialize variables used for homing computations.
    uint8_t n_cycle = (2 * N_HOMING_LOCATE_CYCLE + 1);
    uint8_t step_pin[N_AXIS];

    float target[N_AXIS];
    float max_travel = 0.0;
    uint8_t idx;
    for (idx = 0; idx < N_AXIS; idx++) {
      // Initialize step pin masks
      step_pin[idx] = get_step_pin_mask(idx);

      if (bit_istrue(standard_mask, bit(idx))) {
        // Set target based on max_travel setting. Ensure homing switches
        // engaged with search scalar. NOTE: settings.max_travel[] is stored as
        // a negative value.
        max_travel = max(max_travel, (-HOMING_AXIS_SEARCH_SCALAR) *
                                         settings.max_travel[idx]);
      }
    }

    // Set search mode with approach at seek rate to quickly engage the
    // specified cycle_mask limit switches.
    bool approach = true;
    float homing_rate = settings.homing_seek_rate;

    // Agri3D: Apply 1/2 speed reduction for Z-axis homing as requested by user.
    if (standard_mask == (1 << Z_AXIS)) {
      homing_rate /= 2.0;
    }

    uint8_t limit_state, axislock, n_active_axis;
    do {

      system_convert_array_steps_to_mpos(target, sys_position);

      // Initialize and declare variables needed for homing routine.
      axislock = 0;
      n_active_axis = 0;
      for (idx = 0; idx < N_AXIS; idx++) {
        // Set target location for active axes and setup computation for homing
        // rate.
        if (bit_istrue(standard_mask, bit(idx))) {
          n_active_axis++;
          sys_position[idx] = 0;

          // Set target direction based on cycle mask and homing cycle approach
          // state. NOTE: This happens to compile smaller than any other
          // implementation tried.
          uint8_t dir_mask = settings.homing_dir_mask;
          if (standard_mask == (1 << Z_AXIS)) {
            dir_mask &= ~(1 << Z_AXIS); // Clear Z bit to force seeking positive (UP) for Phase 0
          }

          if (bit_istrue(dir_mask, bit(idx))) {
            if (approach) {
              target[idx] = -max_travel;
            } else {
              target[idx] = max_travel;
            }
          } else {
            if (approach) {
              target[idx] = max_travel;
            } else {
              target[idx] = -max_travel;
            }
          }
          // Apply axislock to the step port pins active in this cycle.
          axislock |= step_pin[idx];
        }
      }
      homing_rate *= sqrt(n_active_axis); // [sqrt(N_AXIS)] Adjust so individual
                                          // axes all move at homing rate.
      sys.homing_axis_lock = axislock;

      // Perform homing cycle. Planner buffer should be empty, as required to
      // initiate the homing cycle.
      pl_data->feed_rate = homing_rate; // Set current homing rate.
      plan_buffer_line(
          target,
          pl_data); // Bypass mc_line(). Directly plan homing motion.

      sys.step_control =
          STEP_CONTROL_EXECUTE_SYS_MOTION; // Set to execute homing motion and
                                           // clear existing flags.
      st_prep_buffer(); // Prep and fill segment buffer from newly planned
                        // block.
      st_wake_up();     // Initiate motion
      do {
        if (approach) {
          // Check limit state. Lock out cycle axes when they change.
          limit_state = limits_get_state();
          for (idx = 0; idx < N_AXIS; idx++) {
            if (axislock & step_pin[idx]) {
              if (limit_state & (1 << idx)) {
                axislock &= ~(step_pin[idx]);
              }
            }
          }
          sys.homing_axis_lock = axislock;
        }

        st_prep_buffer(); // Check and prep segment buffer. NOTE: Should take no
                          // longer than 200us.

        wdt_reset(); // Feed the watchdog during long homing moves!

        // Exit routines: No time to run protocol_execute_realtime() in this
        // loop.
        if (sys_rt_exec_state &
            (EXEC_SAFETY_DOOR | EXEC_RESET | EXEC_CYCLE_STOP)) {
          uint8_t rt_exec = sys_rt_exec_state;
          // Homing failure condition: Reset issued during cycle.
          if (rt_exec & EXEC_RESET) {
            system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
          }
          // Homing failure condition: Safety door was opened.
          if (rt_exec & EXEC_SAFETY_DOOR) {
            system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_DOOR);
          }
          // Homing failure condition: Limit switch still engaged after pull-off
          // motion
          if (!approach && (limits_get_state() & standard_mask)) {
            printPgmString(PSTR(
                "[MSG:ALARM 8 - Pull-off Failed. Switch still engaged!]\r\n"));
            system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_PULLOFF);
          }
          // Homing failure condition: Limit switch not found during approach.
          if (approach && (rt_exec & EXEC_CYCLE_STOP)) {
            printPgmString(PSTR("[MSG:ALARM 9 - Approach Failed. Switch never "
                                "triggered!]\r\n"));
            system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_APPROACH);
          }
          if (sys_rt_exec_alarm) {
            mc_reset(); // Stop motors, if they are running.
            protocol_execute_realtime();
            return;
          } else {
            // Pull-off motion complete. Disable CYCLE_STOP from executing.
            system_clear_exec_state_flag(EXEC_CYCLE_STOP);
            break;
          }
        }

      } while (STEP_MASK & axislock);

      st_reset(); // Immediately force kill steppers and reset step segment
                  // buffer.
      delay_ms(settings.homing_debounce_delay); // Delay to allow transient
                                                // dynamics to dissipate.

      // Reverse direction and reset homing rate for locate cycle(s).
      approach = !approach;

      // After first cycle, homing enters locating phase. Shorten search to
      // pull-off distance.
      if (approach) {
        max_travel = settings.homing_pulloff * HOMING_AXIS_LOCATE_SCALAR;
        homing_rate = settings.homing_feed_rate;
      } else {
        max_travel = settings.homing_pulloff;
        homing_rate = settings.homing_seek_rate;
      }

      // Agri3D: Apply 1/2 speed reduction for Z-axis homing.
      if (standard_mask == (1 << Z_AXIS)) {
        homing_rate /= 2.0;
      }

    } while (n_cycle-- > 0);

    int32_t set_axis_position;
    // Set machine positions for homed limit switches. Don't update non-homed
    // axes.
    for (idx = 0; idx < N_AXIS; idx++) {
      // NOTE: settings.max_travel[] is stored as a negative value.
      if (standard_mask & bit(idx)) {
        if (bit_istrue(settings.homing_dir_mask, bit(idx))) {
          // Agri3D: If homing to the negative direction (e.g. bottom for Z), set position to +pulloff
          // so that the home position is treated as the origin (approx 0) and moving away goes positive.
          set_axis_position =
              lround(settings.homing_pulloff * settings.steps_per_mm[idx]);
        } else {
          set_axis_position =
              lround(-settings.homing_pulloff * settings.steps_per_mm[idx]);
        }

        sys_position[idx] = set_axis_position;
      }
    }
  }

  // =========================================================================
  // Agri3D Auto-Dimension Calculation Sequence (For X and Y axes only)
  // =========================================================================
  if (auto_dim_mask) {
    float target[N_AXIS];
    system_convert_array_steps_to_mpos(target, sys_position);

    plan_line_data_t dim_plan_data;
    memset(&dim_plan_data, 0, sizeof(plan_line_data_t));
    dim_plan_data.condition =
        (PL_COND_FLAG_SYSTEM_MOTION | PL_COND_FLAG_NO_FEED_OVERRIDE);
    dim_plan_data.feed_rate = settings.homing_seek_rate;

    // Agri3D: Apply 1/2 speed reduction for Z-axis during auto-dimensioning.
    if (auto_dim_mask & (1 << Z_AXIS)) {
      dim_plan_data.feed_rate /= 2.0;
    }

    uint8_t measure_axislock = 0;
    uint8_t idx;

    // === Step 1: Seek to the END (opposite of homing_dir_mask) ===
    for (idx = 0; idx < N_AXIS; idx++) {
      if (auto_dim_mask & bit(idx)) {
        // Drive to the opposite end (10 meters absolute maximum safety distance
        // to guarantee StallGuard triggers)
        if (bit_istrue(settings.homing_dir_mask, bit(idx))) {
          target[idx] +=
              10000.0; // Homed at negative end, drive positive to find END
        } else {
          target[idx] -=
              10000.0; // Homed at positive end, drive negative to find END
        }
        measure_axislock |= get_step_pin_mask(idx);
      }
    }

    sys.homing_axis_lock = measure_axislock;
    plan_buffer_line(target, &dim_plan_data);
    sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION;
    st_prep_buffer();
    st_wake_up();

    // Wait until StallGuard (opposite limit) is hit
    do {
      uint8_t limit_state = limits_get_state();
      for (idx = 0; idx < N_AXIS; idx++) {
        if (measure_axislock & get_step_pin_mask(idx)) {
          if (limit_state & (1 << idx)) {
            measure_axislock &= ~(get_step_pin_mask(idx));
          }
        }
      }
      sys.homing_axis_lock = measure_axislock;
      st_prep_buffer();

      wdt_reset(); // FEED THE WATCHDOG to prevent firmware reset!

      if (sys_rt_exec_state &
          (EXEC_SAFETY_DOOR | EXEC_RESET | EXEC_CYCLE_STOP)) {
        if (sys_rt_exec_state & EXEC_RESET) {
          system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
        }

        if (sys_rt_exec_state & EXEC_CYCLE_STOP) {
          printPgmString(
              PSTR("[MSG:Auto-Dim Failed - Switch never triggered!]\r\n"));
          system_set_exec_alarm(EXEC_ALARM_HARD_LIMIT);
        }

        if (sys_rt_exec_alarm) {
          mc_reset();
          protocol_execute_realtime();
          return;
        }
      }
    } while (STEP_MASK & measure_axislock);

    st_reset(); // Immediately force kill steppers upon StallGuard trigger
    delay_ms(settings.homing_debounce_delay); // Dissipate kinetic energy

    // === Step 2: Pull off from END ===
    system_convert_array_steps_to_mpos(target, sys_position);
    measure_axislock = 0;
    for (idx = 0; idx < N_AXIS; idx++) {
      if (auto_dim_mask & bit(idx)) {
        // Move away from the END switch
        if (bit_istrue(settings.homing_dir_mask, bit(idx))) {
          target[idx] -= settings.homing_pulloff;
        } else {
          target[idx] += settings.homing_pulloff;
        }
        measure_axislock |= get_step_pin_mask(idx);
      }
    }

    sys.homing_axis_lock = measure_axislock;
    plan_buffer_line(target, &dim_plan_data);
    sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION;
    st_prep_buffer();
    st_wake_up();

    do {
      st_prep_buffer();
      wdt_reset();
      if (sys_rt_exec_state & (EXEC_RESET | EXEC_CYCLE_STOP)) {
        if (sys_rt_exec_state & EXEC_RESET) {
          system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
          mc_reset();
          protocol_execute_realtime();
          return;
        }
        system_clear_exec_state_flag(EXEC_CYCLE_STOP);
        break;
      }
    } while (1);

    st_reset();
    delay_ms(settings.homing_debounce_delay);

    // === Step 3: Seek to BEGIN (Home) ===
    system_convert_array_steps_to_mpos(target, sys_position);
    int32_t start_pos[N_AXIS];
    measure_axislock = 0;

    for (idx = 0; idx < N_AXIS; idx++) {
      start_pos[idx] =
          sys_position[idx]; // Record pulled-off END position for measuring!
      if (auto_dim_mask & bit(idx)) {
        // Drive towards home switch (10km safety max)
        if (bit_istrue(settings.homing_dir_mask, bit(idx))) {
          target[idx] -= 10000.0; // Home is in the negative direction
        } else {
          target[idx] += 10000.0; // Home is in the positive direction
        }
        measure_axislock |= get_step_pin_mask(idx);
      }
    }

    sys.homing_axis_lock = measure_axislock;
    plan_buffer_line(target, &dim_plan_data);
    sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION;
    st_prep_buffer();
    st_wake_up();

    // Wait for StallGuard to trigger at the home switch
    do {
      uint8_t limit_state = limits_get_state();
      for (idx = 0; idx < N_AXIS; idx++) {
        if (measure_axislock & get_step_pin_mask(idx)) {
          if (limit_state & (1 << idx)) {
            measure_axislock &= ~(get_step_pin_mask(idx));
          }
        }
      }
      sys.homing_axis_lock = measure_axislock;
      st_prep_buffer();
      wdt_reset();

      if (sys_rt_exec_state & (EXEC_RESET | EXEC_CYCLE_STOP)) {
        if (sys_rt_exec_state & EXEC_RESET) {
          system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
          mc_reset();
          protocol_execute_realtime();
          return;
        }
        if (sys_rt_exec_state & EXEC_CYCLE_STOP) {
          system_clear_exec_state_flag(EXEC_CYCLE_STOP);
        }
        break;
      }
    } while (STEP_MASK & measure_axislock);

    st_reset();
    delay_ms(settings.homing_debounce_delay);

    // === Step 4: Calculate Dimension ===
    for (idx = 0; idx < N_AXIS; idx++) {
      if (auto_dim_mask & bit(idx)) {

        // Calculate absolute step count traveled from the END to BEGIN
        int32_t step_dist = sys_position[idx] - start_pos[idx];
        if (step_dist < 0) {
          step_dist = -step_dist;
        }

        float measured_length = ((float)step_dist / settings.steps_per_mm[idx]);
        // We already did ONE pull-off (at the end). So total measured distance
        // is the distance between limits minus one pull-off. We will subtract
        // the homing_pulloff again to leave room on the begin side.
        measured_length -= settings.homing_pulloff;

        if (measured_length > 0) {
          // Write parameter ($130 for X, $131 for Y) direct to EEPROM
          settings_store_global_setting(130 + idx, measured_length);
        }
      }
    }

    // === Step 5: Pull off BEGIN ===
    system_convert_array_steps_to_mpos(target, sys_position);
    measure_axislock = 0;
    for (idx = 0; idx < N_AXIS; idx++) {
      if (auto_dim_mask & bit(idx)) {
        // Pull off away from home, into positive workspace
        if (bit_istrue(settings.homing_dir_mask, bit(idx))) {
          target[idx] +=
              settings.homing_pulloff; // Home negative, pull off positive
        } else {
          target[idx] -=
              settings.homing_pulloff; // Home positive, pull off negative
        }
        measure_axislock |= get_step_pin_mask(idx);
      }
    }

    sys.homing_axis_lock = measure_axislock;
    plan_buffer_line(target, &dim_plan_data);
    sys.step_control = STEP_CONTROL_EXECUTE_SYS_MOTION;
    st_prep_buffer();
    st_wake_up();

    do {
      st_prep_buffer();
      wdt_reset();
      if (sys_rt_exec_state & (EXEC_RESET | EXEC_CYCLE_STOP)) {
        if (sys_rt_exec_state & EXEC_RESET) {
          system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
          mc_reset();
          protocol_execute_realtime();
          return;
        }
        system_clear_exec_state_flag(EXEC_CYCLE_STOP);
        break;
      }
    } while (1);

    st_reset();

    // === Step 6: Set mpos to +pulloff (home origin, positive workspace) ===
    for (idx = 0; idx < N_AXIS; idx++) {
      if (auto_dim_mask & bit(idx)) {
        sys_position[idx] =
            lround(settings.homing_pulloff * settings.steps_per_mm[idx]);
      }
    }
  }

  // Homing sequence complete. Clear any persistent crash logs since the machine
  // is now in a known healthy state.
  eeprom_put_char(EEPROM_ADDR_CRASH_LOG, 0);
  eeprom_put_char(EEPROM_ADDR_CRASH_LOG2, 0);

  sys.step_control =
      STEP_CONTROL_NORMAL_OP; // Return step control to normal operation.
}

// Performs a soft limit check. Called from mc_line() only. Assumes the machine
// has been homed, the workspace volume is in all negative space, and the system
// is in normal operation. NOTE: Used by jogging to limit travel within
// soft-limit volume.
void limits_soft_check(float *target) {
  if (system_check_travel_limits(target)) {
    sys.soft_limit = true;
    // Force feed hold if cycle is active. All buffered blocks are guaranteed to
    // be within workspace volume so just come to a controlled stop so position
    // is not lost. When complete enter alarm mode.
    if (sys.state == STATE_CYCLE) {
      system_set_exec_state_flag(EXEC_FEED_HOLD);
      do {
        protocol_execute_realtime();
        if (sys.abort) {
          return;
        }
      } while (sys.state != STATE_IDLE);
    }
    mc_reset(); // Issue system reset and ensure spindle and coolant are
                // shutdown.
    system_set_exec_alarm(
        EXEC_ALARM_SOFT_LIMIT);  // Indicate soft limit critical event
    protocol_execute_realtime(); // Execute to enter critical event loop and
                                 // system abort
    return;
  }
}

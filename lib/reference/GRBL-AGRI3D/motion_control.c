/*
  motion_control.c - high level interface for issuing motion commands
  Part of Grbl

  Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
  Copyright (c) 2009-2011 Simen Svale Skogsrud

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "grbl-agri3d.h"
#include "tmc_config.h"
#include <util/delay.h>

// Execute linear motion in absolute millimeter coordinates. Feed rate given in
// millimeters/second unless invert_feed_rate is true. Then the feed_rate means
// that the motion should be completed in (1 minute)/feed_rate time. NOTE: This
// is the primary gateway to the grbl planner. All line motions, including arc
// line segments, must pass through this routine before being passed to the
// planner. The seperation of mc_line and plan_buffer_line is done primarily to
// place non-planner-type functions from being in the planner and to let
// backlash compensation or canned cycle integration simple and direct.
void mc_line(float *target, plan_line_data_t *pl_data) {
  // If enabled, check for soft limit violations. Placed here all line motions
  // are picked up from everywhere in Grbl.
  if (bit_istrue(settings.flags, BITFLAG_SOFT_LIMIT_ENABLE)) {
    // NOTE: Block jog state. Jogging is a special case and soft limits are
    // handled independently.
    if (sys.state != STATE_JOG) {
      limits_soft_check(target);
    }
  }

  // If in check gcode mode, prevent motion by blocking planner. Soft limits
  // still work.
  if (sys.state == STATE_CHECK_MODE) {
    return;
  }

  // NOTE: Backlash compensation may be installed here. It will need direction
  // info to track when to insert a backlash line motion(s) before the intended
  // line motion and will require its own plan_check_full_buffer() and check for
  // system abort loop. Also for position reporting backlash steps will need to
  // be also tracked, which will need to be kept at a system level. There are
  // likely some other things that will need to be tracked as well. However, we
  // feel that backlash compensation should NOT be handled by Grbl itself,
  // because there are a myriad of ways to implement it and can be effective or
  // ineffective for different CNC machines. This would be better handled by the
  // interface as a post-processor task, where the original g-code is translated
  // and inserts backlash motions that best suits the machine. NOTE: Perhaps as
  // a middle-ground, all that needs to be sent is a flag or special command
  // that indicates to Grbl what is a backlash compensation motion, so that Grbl
  // executes the move but doesn't update the machine position values. Since the
  // position values used by the g-code parser and planner are separate from the
  // system machine positions, this is doable.

  // If the buffer is full: good! That means we are well ahead of the robot.
  // Remain in this loop until there is room in the buffer.
  do {
    protocol_execute_realtime(); // Check for any run-time commands
    if (sys.abort) {
      return;
    } // Bail, if system abort.
    if (plan_check_full_buffer()) {
      protocol_auto_cycle_start();
    } // Auto-cycle start when buffer is full.
    else {
      break;
    }
  } while (1);

  // Plan and queue motion into planner buffer
  plan_buffer_line(target, pl_data);
}

// Arcs have been explicitly stripped from Agri3D firmware.

// Execute dwell in seconds.
void mc_dwell(float seconds) {
  if (sys.state == STATE_CHECK_MODE) {
    return;
  }
  protocol_buffer_synchronize();
  delay_sec(seconds, DELAY_MODE_DWELL);
}

// Perform homing cycle to locate and set machine zero. Only '$H' executes this
// command. NOTE: There should be no motions in the buffer and Grbl must be in
// an idle state before executing the homing cycle. This prevents incorrect
// buffered plans after homing.
void mc_homing_cycle(uint8_t cycle_mask) {
// Check and abort homing cycle, if hard limits are already enabled. Helps
// prevent problems with machines with limits wired on both ends of travel to
// one limit pin.
// TODO: Move the pin-specific LIMIT_PIN call to limits.c as a function.
#ifdef LIMITS_TWO_SWITCHES_ON_AXES
  if (limits_get_state()) {
    mc_reset(); // Issue system reset and ensure spindle and coolant are
                // shutdown.
    system_set_exec_alarm(EXEC_ALARM_HARD_LIMIT);
    return;
  }
#endif

  limits_disable(); // Disable hard limits pin change register for cycle
                    // duration

  // Arm StallGuard DIAG output on X/Y so homing can detect stalls.
  // MUST happen AFTER limits_disable() to prevent a race condition where
  // the DIAG pin toggles and fires the ISR before it's masked out.
  tmc_stallguard_enable();

  // -------------------------------------------------------------------------------------
  // Perform homing routine. NOTE: Special motion case. Only system reset works.

#ifdef HOMING_SINGLE_AXIS_COMMANDS
  if (cycle_mask) {
    if (cycle_mask == HOMING_CYCLE_Z) {
      limits_go_home(HOMING_CYCLE_0); // Phase 0: Standard Homing (Retract Z)
      limits_go_home(HOMING_CYCLE_2); // Phase 2: Auto-dim Z
    } else {
      // For $HX or $HY, trigger the auto-dimensioning flag
      limits_go_home(cycle_mask | (1 << 3));
    }
  } // Perform homing cycle based on mask.
  else
#endif
  {
    // Search to engage all axes limit switches at faster homing seek rate.
    limits_go_home(HOMING_CYCLE_0); // Homing cycle 0
#ifdef HOMING_CYCLE_1
    limits_go_home(HOMING_CYCLE_1); // Homing cycle 1
#endif
#ifdef HOMING_CYCLE_2
    limits_go_home(HOMING_CYCLE_2); // Homing cycle 2
#endif
  }

  protocol_execute_realtime(); // Check for reset and set system abort.
  if (sys.abort) {
    return;
  } // Did not complete. Alarm state set by mc_alarm.

  // Homing cycle complete! Setup system for normal operation.
  // -------------------------------------------------------------------------------------

  // Sync gcode parser and planner positions to homed position.
  gc_sync_position();
  plan_sync_position();

  // Disarm StallGuard DIAG output BEFORE re-enabling hard limit interrupts.
  // With SGTHRS=0, the DIAG pins will stay LOW and won't trigger false alarms
  // during normal motion. Z still uses its physical endstop via the same ISR.
  tmc_stallguard_disable();
  _delay_ms(5); // Let DIAG line settle to LOW before interrupt is re-armed

  // If hard limits feature enabled, re-enable hard limits pin change register
  // after homing cycle.
  limits_init();
}

// Probing cycles have been explicitly stripped from Agri3D firmware.

// Plans and executes the single special motion case for parking. Independent of
// main planner buffer. NOTE: Uses the always free planner ring buffer head to
// store motion parameters for execution.
#ifdef PARKING_ENABLE
void mc_parking_motion(float *parking_target, plan_line_data_t *pl_data) {
  if (sys.abort) {
    return;
  } // Block during abort.

  uint8_t plan_status = plan_buffer_line(parking_target, pl_data);

  if (plan_status) {
    bit_true(sys.step_control, STEP_CONTROL_EXECUTE_SYS_MOTION);
    bit_false(sys.step_control,
              STEP_CONTROL_END_MOTION); // Allow parking motion to execute, if
                                        // feed hold is active.
    st_parking_setup_buffer(); // Setup step segment buffer for special parking
                               // motion case
    st_prep_buffer();
    st_wake_up();
    do {
      protocol_exec_rt_system();
      if (sys.abort) {
        return;
      }
    } while (sys.step_control & STEP_CONTROL_EXECUTE_SYS_MOTION);
    st_parking_restore_buffer(); // Restore step segment buffer to normal run
                                 // state.
  } else {
    bit_false(sys.step_control, STEP_CONTROL_EXECUTE_SYS_MOTION);
    protocol_exec_rt_system();
  }
}
#endif

#ifdef ENABLE_PARKING_OVERRIDE_CONTROL
void mc_override_ctrl_update(uint8_t override_state) {
  // Finish all queued commands before altering override control state
  protocol_buffer_synchronize();
  if (sys.abort) {
    return;
  }
  sys.override_ctrl = override_state;
}
#endif

// Method to ready the system to reset by setting the realtime reset command and
// killing any active processes in the system. This also checks if a system
// reset is issued while Grbl is in a motion state. If so, kills the steppers
// and sets the system alarm to flag position lost, since there was an abrupt
// uncontrolled deceleration. Called at an interrupt level by realtime abort
// command and hard limits. So, keep to a minimum.
void mc_reset() {
  // Only this function can set the system reset. Helps prevent multiple kill
  // calls.
  if (bit_isfalse(sys_rt_exec_state, EXEC_RESET)) {
    system_set_exec_state_flag(EXEC_RESET);

    // Kill Native Agri3D relays
    relays_stop_all();

    // Kill steppers only if in any motion state, i.e. cycle, actively holding,
    // or homing. NOTE: If steppers are kept enabled via the step idle delay
    // setting, this also keeps the steppers enabled by avoiding the go_idle
    // call altogether, unless the motion state is violated, by which, all bets
    // are off.
    if ((sys.state & (STATE_CYCLE | STATE_HOMING | STATE_JOG)) ||
        (sys.step_control &
         (STEP_CONTROL_EXECUTE_HOLD | STEP_CONTROL_EXECUTE_SYS_MOTION))) {
      if (sys.state == STATE_HOMING) {
        if (!sys_rt_exec_alarm) {
          system_set_exec_alarm(EXEC_ALARM_HOMING_FAIL_RESET);
        }
      } else {
        system_set_exec_alarm(EXEC_ALARM_ABORT_CYCLE);
      }
      st_go_idle(); // Force kill steppers. Position has likely been lost.
    }
  }
}

/*
  system.c - Handles system level commands and real-time processes
  Part of Grbl

  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

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
#include "tmc2209.h"
#include "tmc_uart.h"

// system_init(), system_control_get_state(), system_check_safety_door_ajar()
// removed: Agri3D has no physical control panel. Reset is wired to RST pin.
// All real-time commands come from the ESP32 via hardware UART (serial.c ISR).

// Executes user startup script, if stored.
void system_execute_startup(char *line) {
  uint8_t n;
  for (n = 0; n < N_STARTUP_LINE; n++) {
    if (!(settings_read_startup_line(n, line))) {
      line[0] = 0;
      report_execute_startup_message(line, STATUS_SETTING_READ_FAIL);
    } else {
      if (line[0] != 0) {
        uint8_t status_code = gc_execute_line(line);
        report_execute_startup_message(line, status_code);
      }
    }
  }
}

// Directs and executes one line of formatted input from protocol_process. While
// mostly incoming streaming g-code blocks, this also executes Grbl internal
// commands, such as settings, initiating the homing cycle, and toggling switch
// states. This differs from the realtime command module by being susceptible to
// when Grbl is ready to execute the next line during a cycle, so for switches
// like block delete, the switch only effects the lines that are processed
// afterward, not necessarily real-time during a cycle, since there are motions
// already stored in the buffer. However, this 'lag' should not be an issue,
// since these commands are not typically used during a cycle.
uint8_t system_execute_line(char *line) {
  uint8_t char_counter = 1;
  uint8_t helper_var = 0; // Helper variable
  float parameter, value;
  switch (line[char_counter]) {
  case 0:
    report_grbl_help();
    break;
  case 'T': // $TMC or $TRAW — TMC2209 diagnostics
    if (sys.state & (STATE_CYCLE | STATE_HOLD)) {
      return (STATUS_IDLE_ERROR);
    }
    // $TMC  — high-level decoded register dump
    if (line[2] == 'M' && line[3] == 'C' && line[4] == 0) {
      tmc_report_details();
    }
    // $TRAWX / $TRAWY / $TRAWZ / $TRAWW — raw byte dump for debugging
    else if (line[2] == 'R' && line[3] == 'A' && line[4] == 'W') {
      uint8_t addr;
      const char *axis_label;
      switch (line[5]) {
      case 'X': addr = TMC_ADDR_X;  axis_label = "X";  break;
      case 'Y': addr = TMC_ADDR_Y1; axis_label = "Y1"; break;
      case 'W': addr = TMC_ADDR_Y2; axis_label = "Y2"; break;
      case 'Z': addr = TMC_ADDR_Z;  axis_label = "Z";  break;
      default:  return (STATUS_INVALID_STATEMENT);
      }
      if (line[6] != 0) return (STATUS_INVALID_STATEMENT);

      // Dump raw bytes for GSTAT and DRV_STATUS
      uint8_t regs[2]       = {TMC2209_REG_GSTAT, TMC2209_REG_DRV_STATUS};
      const char *rnames[2] = {"GSTAT", "DRV  "};
      uint8_t buf[8];

      for (uint8_t r = 0; r < 2; r++) {
        printPgmString(PSTR("[TRAW:"));
        printString((char *)axis_label);
        serial_write(' ');
        printString((char *)rnames[r]);
        serial_write(' ');

        uint8_t got = tmc_uart_read_raw(addr, regs[r], buf);
        for (uint8_t b = 0; b < 8; b++) {
          if (b < got) {
            // Print byte as 2 hex digits
            serial_write("0123456789ABCDEF"[buf[b] >> 4]);
            serial_write("0123456789ABCDEF"[buf[b] & 0x0F]);
          } else {
            printPgmString(PSTR("??"));
          }
          serial_write(' ');
        }

        // CRC check
        if (got == 8) {
          uint8_t crc_expected = tmc_crc8(buf, 7);
          if (buf[7] == crc_expected) {
            printPgmString(PSTR("CRC:OK"));
          } else {
            printPgmString(PSTR("CRC:FAIL(exp="));
            serial_write("0123456789ABCDEF"[crc_expected >> 4]);
            serial_write("0123456789ABCDEF"[crc_expected & 0x0F]);
            serial_write(')');
          }
        } else {
          printPgmString(PSTR("TIMEOUT"));
        }
        printPgmString(PSTR("]\r\n"));

        // Give PDN_UART line time to return to idle before next request
        delay_ms(2);
      }

    } 
    // $TLOG — continuous StallGuard load stream
    else if (line[2] == 'L' && line[3] == 'O' && line[4] == 'G' && line[5] == 0) {
      printPgmString(PSTR("[TLOG ACTIVE - SEND ANY CHAR TO STOP]\r\n"));
      const uint8_t addrs[4] = {TMC_ADDR_X, TMC_ADDR_Y1, TMC_ADDR_Y2, TMC_ADDR_Z};
      const char *const labels[4] = {"X:", " Y1:", " Y2:", " Z:"};
      
      // Loop until ANY character is received in the serial buffer
      while(serial_read() == SERIAL_NO_DATA) {
        printPgmString(PSTR("[LOAD]"));
        for (uint8_t i = 0; i < 4; i++) {
          printString((char *)labels[i]);
          uint32_t val = 0;
          if (tmc_uart_read_reg_async(addrs[i], TMC2209_REG_SG_RESULT, &val)) {
            print_uint32_base10(val & 0x1FF); // 10-bit SG_RESULT
          } else {
            printPgmString(PSTR("---"));
          }
        }
        printPgmString(PSTR("\r\n"));
        
        // Wait ~250ms before next sample, while allowing steppers to move
        for(uint8_t w=0; w<25; w++) {
           delay_ms(10);
        }
      }
      printPgmString(PSTR("[TLOG STOPPED]\r\n"));
      
    } else {
      return (STATUS_INVALID_STATEMENT);
    }
    break;

  case 'J': // Jogging
    // Execute only if in IDLE or JOG states.
    if (sys.state != STATE_IDLE && sys.state != STATE_JOG) {
      return (STATUS_IDLE_ERROR);
    }
    if (line[2] != '=') {
      return (STATUS_INVALID_STATEMENT);
    }
    return (gc_execute_line(line)); // NOTE: $J= is ignored inside g-code parser
                                    // and used to detect jog motions.
    break;
  case '$':
  case 'G':
  case 'C':
  case 'X':
    if (line[2] != 0) {
      return (STATUS_INVALID_STATEMENT);
    }
    switch (line[1]) {
    case '$': // Prints Grbl settings
      if (sys.state & (STATE_CYCLE | STATE_HOLD)) {
        return (STATUS_IDLE_ERROR);
      } // Block during cycle. Takes too long to print.
      else {
        report_grbl_settings();
      }
      break;
    case 'G': // Prints gcode parser state
      // TODO: Move this to realtime commands for GUIs to request this data
      // during suspend-state.
      report_gcode_modes();
      break;
    case 'C': // Set check g-code mode [IDLE/CHECK]
      // Perform reset when toggling off. Check g-code mode should only work if
      // Grbl is idle and ready, regardless of alarm locks. This is mainly to
      // keep things simple and consistent.
      if (sys.state == STATE_CHECK_MODE) {
        mc_reset();
        report_feedback_message(MESSAGE_DISABLED);
      } else {
        if (sys.state) {
          return (STATUS_IDLE_ERROR);
        } // Requires no alarm mode.
        sys.state = STATE_CHECK_MODE;
        report_feedback_message(MESSAGE_ENABLED);
      }
      break;
    case 'X': // Disable alarm lock [ALARM]
      if (sys.state == STATE_ALARM) {
        // Block if safety door is ajar.
        // Safety door removed for Agri3D (open field farming robot).
        report_feedback_message(MESSAGE_ALARM_UNLOCK);
        sys.state = STATE_IDLE;
        // Don't run startup script. Prevents stored moves in startup from
        // causing accidents.
      } // Otherwise, no effect.
      break;
    }
    break;
  default:
    // Block any system command that requires the state as IDLE/ALARM. (i.e.
    // EEPROM, homing)
    if (!(sys.state == STATE_IDLE || sys.state == STATE_ALARM)) {
      return (STATUS_IDLE_ERROR);
    }
    switch (line[1]) {
    case 'H': // Perform homing cycle [IDLE/ALARM]
      if (bit_isfalse(settings.flags, BITFLAG_HOMING_ENABLE)) {
        return (STATUS_SETTING_DISABLED);
      }
      // Safety door removed for Agri3D.
      sys.state = STATE_HOMING; // Set system state variable
      if (line[2] == 0) {
        mc_homing_cycle(HOMING_CYCLE_ALL);
#ifdef HOMING_SINGLE_AXIS_COMMANDS
      } else if (line[3] == 0) {
        switch (line[2]) {
        case 'X':
          mc_homing_cycle(HOMING_CYCLE_X);
          break;
        case 'Y':
          mc_homing_cycle(HOMING_CYCLE_Y);
          break;
        case 'Z':
          mc_homing_cycle(HOMING_CYCLE_Z);
          break;
        default:
          return (STATUS_INVALID_STATEMENT);
        }
#endif
      } else {
        return (STATUS_INVALID_STATEMENT);
      }
      if (!sys.abort) { // Execute startup scripts after successful homing.
        sys.state = STATE_IDLE; // Set to IDLE when complete.
        st_go_idle(); // Set steppers to the settings idle state before
                      // returning.
        if (line[2] == 0) {
          system_execute_startup(line);
        }
      }
      break;
    case 'S': // Puts Grbl to sleep [IDLE/ALARM]
      if ((line[2] != 'L') || (line[3] != 'P') || (line[4] != 0)) {
        return (STATUS_INVALID_STATEMENT);
      }
      system_set_exec_state_flag(
          EXEC_SLEEP); // Set to execute sleep mode immediately
      break;
    case 'I': // Print or store build info. [IDLE/ALARM]
      if (line[++char_counter] == 0) {
        settings_read_build_info(line);
        report_build_info(line);
#ifdef ENABLE_BUILD_INFO_WRITE_COMMAND
      } else { // Store startup line [IDLE/ALARM]
        if (line[char_counter++] != '=') {
          return (STATUS_INVALID_STATEMENT);
        }
        helper_var = char_counter; // Set helper variable as counter to start of
                                   // user info line.
        do {
          line[char_counter - helper_var] = line[char_counter];
        } while (line[char_counter++] != 0);
        settings_store_build_info(line);
#endif
      }
      break;
    case 'R': // Restore defaults [IDLE/ALARM]
      if ((line[2] != 'S') || (line[3] != 'T') || (line[4] != '=') ||
          (line[6] != 0)) {
        return (STATUS_INVALID_STATEMENT);
      }
      switch (line[5]) {
#ifdef ENABLE_RESTORE_EEPROM_DEFAULT_SETTINGS
      case '$':
        settings_restore(SETTINGS_RESTORE_DEFAULTS);
        break;
#endif
#ifdef ENABLE_RESTORE_EEPROM_CLEAR_PARAMETERS
      case '#':
        settings_restore(SETTINGS_RESTORE_PARAMETERS);
        break;
#endif
#ifdef ENABLE_RESTORE_EEPROM_WIPE_ALL
      case '*':
        settings_restore(SETTINGS_RESTORE_ALL);
        break;
#endif
      default:
        return (STATUS_INVALID_STATEMENT);
      }
      report_feedback_message(MESSAGE_RESTORE_DEFAULTS);
      mc_reset(); // Force reset to ensure settings are initialized correctly.
      break;
    case 'N':                          // Startup lines. [IDLE/ALARM]
      if (line[++char_counter] == 0) { // Print startup lines
        for (helper_var = 0; helper_var < N_STARTUP_LINE; helper_var++) {
          if (!(settings_read_startup_line(helper_var, line))) {
            report_status_message(STATUS_SETTING_READ_FAIL);
          } else {
            report_startup_line(helper_var, line);
          }
        }
        break;
      } else { // Store startup line [IDLE Only] Prevents motion during ALARM.
        if (sys.state != STATE_IDLE) {
          return (STATUS_IDLE_ERROR);
        } // Store only when idle.
        helper_var = true; // Set helper_var to flag storing method.
        // No break. Continues into default: to read remaining command
        // characters.
      }
    default: // Storing setting methods [IDLE/ALARM]
      if (!read_float(line, &char_counter, &parameter)) {
        return (STATUS_BAD_NUMBER_FORMAT);
      }
      if (line[char_counter++] != '=') {
        return (STATUS_INVALID_STATEMENT);
      }
      if (helper_var) { // Store startup line
        // Prepare sending gcode block to gcode parser by shifting all
        // characters
        helper_var = char_counter; // Set helper variable as counter to start of
                                   // gcode block
        do {
          line[char_counter - helper_var] = line[char_counter];
        } while (line[char_counter++] != 0);
        // Execute gcode block to ensure block is valid.
        helper_var =
            gc_execute_line(line); // Set helper_var to returned status code.
        if (helper_var) {
          return (helper_var);
        } else {
          helper_var =
              trunc(parameter); // Set helper_var to int value of parameter
          settings_store_startup_line(helper_var, line);
        }
      } else { // Store global setting.
        if (!read_float(line, &char_counter, &value)) {
          return (STATUS_BAD_NUMBER_FORMAT);
        }
        if ((line[char_counter] != 0) || (parameter > 255)) {
          return (STATUS_INVALID_STATEMENT);
        }
        return (settings_store_global_setting((uint8_t)parameter, value));
      }
    }
  }
  return (STATUS_OK); // If '$' command makes it to here, then everything's ok.
}

void system_flag_wco_change() {
#ifdef FORCE_BUFFER_SYNC_DURING_WCO_CHANGE
  protocol_buffer_synchronize();
#endif
  sys.report_wco_counter = 0;
}

// Returns machine position of axis 'idx'. Must be sent a 'step' array.
// NOTE: If motor steps and machine position are not in the same coordinate
// frame, this function
//   serves as a central place to compute the transformation.
float system_convert_axis_steps_to_mpos(int32_t *steps, uint8_t idx) {
  float pos;
#ifdef COREXY
  if (idx == X_AXIS) {
    pos = (float)system_convert_corexy_to_x_axis_steps(steps) /
          settings.steps_per_mm[idx];
  } else if (idx == Y_AXIS) {
    pos = (float)system_convert_corexy_to_y_axis_steps(steps) /
          settings.steps_per_mm[idx];
  } else {
    pos = steps[idx] / settings.steps_per_mm[idx];
  }
#else
  pos = steps[idx] / settings.steps_per_mm[idx];
#endif
  return (pos);
}

void system_convert_array_steps_to_mpos(float *position, int32_t *steps) {
  uint8_t idx;
  for (idx = 0; idx < N_AXIS; idx++) {
    position[idx] = system_convert_axis_steps_to_mpos(steps, idx);
  }
  return;
}

// CoreXY kinematics removed: Agri3D is a standard Cartesian (XYZ) system.

// Checks and reports if target array exceeds machine travel limits.
uint8_t system_check_travel_limits(float *target) {
  uint8_t idx;
  for (idx = 0; idx < N_AXIS; idx++) {
#ifdef HOMING_FORCE_SET_ORIGIN
    // When homing forced set origin is enabled, soft limits checks need to
    // account for directionality. NOTE: max_travel is stored as negative
    if (bit_istrue(settings.homing_dir_mask, bit(idx))) {
      if (target[idx] < 0 || target[idx] > -settings.max_travel[idx]) {
        return (true);
      }
    } else {
      if (target[idx] > 0 || target[idx] < settings.max_travel[idx]) {
        return (true);
      }
    }
#else
    // NOTE: max_travel is stored as negative
    if (target[idx] > 0 || target[idx] < settings.max_travel[idx]) {
      return (true);
    }
#endif
  }
  return (false);
}

// Special handlers for setting and clearing Grbl's real-time execution flags.
void system_set_exec_state_flag(uint8_t mask) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_state |= (mask);
  SREG = sreg;
}

void system_clear_exec_state_flag(uint8_t mask) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_state &= ~(mask);
  SREG = sreg;
}

void system_set_exec_alarm(uint8_t code) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_alarm = code;
  SREG = sreg;
}

void system_clear_exec_alarm() {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_alarm = 0;
  SREG = sreg;
}

void system_set_exec_motion_override_flag(uint8_t mask) {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_motion_override |= (mask);
  SREG = sreg;
}

void system_clear_exec_motion_overrides() {
  uint8_t sreg = SREG;
  cli();
  sys_rt_exec_motion_override = 0;
  SREG = sreg;
}

// system_set_exec_accessory_override_flag() and
// system_clear_exec_accessory_overrides() removed: spindle/coolant accessories
// are not used in Agri3D.

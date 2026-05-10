/*
  report.c - reporting and messaging methods for Agri3D ESP32 Bridge

  Sends G-code feedback, alarms, and real-time status telemetry (including
  TMC2209 driver diagnostics) back to the ESP32. Unnecessary #ifdefs for
  coolants, PWM spindles, and inch-conversion have been stripped.
*/

#include "grbl-agri3d.h"
#include "tmc_config.h"
#include "eeprom.h"

// Internal report utilities to reduce flash with repetitive tasks turned into
// functions.
void report_util_setting_prefix(uint8_t n) {
  serial_write('$');
  print_uint8_base10(n);
  serial_write('=');
}
static void report_util_line_feed() { printPgmString(PSTR("\r\n")); }
static void report_util_feedback_line_feed() {
  serial_write(']');
  report_util_line_feed();
}
static void report_util_gcode_modes_G() { printPgmString(PSTR(" G")); }
static void report_util_gcode_modes_M() { printPgmString(PSTR(" M")); }

static void report_util_axis_values(float *axis_value) {
  uint8_t idx;
  for (idx = 0; idx < N_AXIS; idx++) {
    printFloat_CoordValue(axis_value[idx]);
    if (idx < (N_AXIS - 1)) {
      serial_write(',');
    }
  }
}

static void report_util_uint8_setting(uint8_t n, int val) {
  report_util_setting_prefix(n);
  print_uint8_base10(val);
  report_util_line_feed();
}
static void report_util_float_setting(uint8_t n, float val, uint8_t n_decimal) {
  report_util_setting_prefix(n);
  printFloat(val, n_decimal);
  report_util_line_feed();
}

void report_status_message(uint8_t status_code) {
  switch (status_code) {
  case STATUS_OK: // STATUS_OK
    printPgmString(PSTR("ok\r\n"));
    break;
  default:
    printPgmString(PSTR("error:"));
    print_uint8_base10(status_code);
    report_util_line_feed();
  }
}

void report_alarm_message(uint8_t alarm_code) {
  printPgmString(PSTR("ALARM:"));
  print_uint8_base10(alarm_code);
  report_util_line_feed();
  delay_ms(500); // Force delay to ensure message clears serial write buffer.
}

void report_feedback_message(uint8_t message_code) {
  printPgmString(PSTR("[MSG:"));
  switch (message_code) {
  case MESSAGE_CRITICAL_EVENT:
    printPgmString(PSTR("Reset to continue"));
    break;
  case MESSAGE_ALARM_LOCK:
    printPgmString(PSTR("'$H'|'$X' to unlock"));
    break;
  case MESSAGE_ALARM_UNLOCK:
    printPgmString(PSTR("Caution: Unlocked"));
    break;
  case MESSAGE_ENABLED:
    printPgmString(PSTR("Enabled"));
    break;
  case MESSAGE_DISABLED:
    printPgmString(PSTR("Disabled"));
    break;
  case MESSAGE_SAFETY_DOOR_AJAR:
    printPgmString(PSTR("Check Door"));
    break;
  case MESSAGE_CHECK_LIMITS:
    printPgmString(PSTR("Check Limits"));
    break;
  case MESSAGE_PROGRAM_END:
    printPgmString(PSTR("Pgm End"));
    break;
  case MESSAGE_RESTORE_DEFAULTS:
    printPgmString(PSTR("Restoring defaults"));
    break;
  case MESSAGE_SPINDLE_RESTORE:
    printPgmString(PSTR("Restoring spindle"));
    break;
  case MESSAGE_SLEEP_MODE:
    printPgmString(PSTR("Sleeping"));
    break;
  }
  report_util_feedback_line_feed();
}

void report_init_message() {
  printPgmString(PSTR("\r\nGrbl-Agri3D " GRBL_VERSION " ['$' for help]\r\n"));
  
  // Read the 2-byte crash log. If either byte is non-zero, a previous crash occurred.
  // Byte 1022: (X_state & 0x0F) | (Y1_state << 4)
  // Byte 1023: (Y2_state & 0x0F) | (Z_state  << 4)
  uint8_t b1 = eeprom_get_char(EEPROM_ADDR_CRASH_LOG);
  uint8_t b2 = eeprom_get_char(EEPROM_ADDR_CRASH_LOG2);

  if (b1 != 0 || b2 != 0) {
    uint8_t states[4];
    states[0] =  b1       & 0x0F; // X
    states[1] = (b1 >> 4) & 0x0F; // Y1
    states[2] =  b2       & 0x0F; // Y2
    states[3] = (b2 >> 4) & 0x0F; // Z
    const char *const labels[4] = {"X", "Y1", "Y2", "Z"};

    // Emit [PREVIOUS_CRASH:X:5,Y1:3] — informational only, no system lock.
    // The farmbot resumes autonomously; this is for remote logging/review only.
    printPgmString(PSTR("[PREVIOUS_CRASH:"));
    uint8_t first = 1;
    for (uint8_t i = 0; i < 4; i++) {
      if (states[i] != 0) {
        if (!first) serial_write(',');
        printString((char *)labels[i]);
        serial_write(':');
        print_uint8_base10(states[i]);
        first = 0;
      }
    }
    printPgmString(PSTR("]\r\n"));

    // Do not clear the crash log on boot. It will be cleared inside limits_go_home() 
    // after a successful auto-calibration / homing maneuver, or by a custom command.
    // eeprom_put_char(EEPROM_ADDR_CRASH_LOG,  0);
    // eeprom_put_char(EEPROM_ADDR_CRASH_LOG2, 0);
  }
}


void report_grbl_help() {
  printPgmString(PSTR("[HLP:$$ $# $G $I $N $x=val $Nx=line $J=line $SLP $C $X "
                      "$H ~ ! ? ctrl-x]\r\n"));
}

void report_grbl_settings() {
  report_util_uint8_setting(0, settings.pulse_microseconds);
  report_util_uint8_setting(1, settings.stepper_idle_lock_time);
  report_util_uint8_setting(2, settings.step_invert_mask);
  report_util_uint8_setting(3, settings.dir_invert_mask);
  report_util_uint8_setting(
      4, bit_istrue(settings.flags, BITFLAG_INVERT_ST_ENABLE));
  report_util_uint8_setting(
      5, bit_istrue(settings.flags, BITFLAG_INVERT_LIMIT_PINS));
  report_util_uint8_setting(
      6, bit_istrue(settings.flags, BITFLAG_INVERT_PROBE_PIN));
  report_util_uint8_setting(10, settings.status_report_mask);
  report_util_float_setting(11, settings.junction_deviation,
                            N_DECIMAL_SETTINGVALUE);
  report_util_float_setting(12, settings.arc_tolerance, N_DECIMAL_SETTINGVALUE);
  report_util_uint8_setting(13,
                            bit_istrue(settings.flags, BITFLAG_REPORT_INCHES));
  report_util_uint8_setting(
      20, bit_istrue(settings.flags, BITFLAG_SOFT_LIMIT_ENABLE));
  report_util_uint8_setting(
      21, bit_istrue(settings.flags, BITFLAG_HARD_LIMIT_ENABLE));
  report_util_uint8_setting(22,
                            bit_istrue(settings.flags, BITFLAG_HOMING_ENABLE));
  report_util_uint8_setting(23, settings.homing_dir_mask);
  report_util_float_setting(24, settings.homing_feed_rate,
                            N_DECIMAL_SETTINGVALUE);
  report_util_float_setting(25, settings.homing_seek_rate,
                            N_DECIMAL_SETTINGVALUE);
  report_util_uint8_setting(26, settings.homing_debounce_delay);
  report_util_float_setting(27, settings.homing_pulloff,
                            N_DECIMAL_SETTINGVALUE);
  // Settings $30, $31, $32 strictly removed (Spindle/Laser logic disabled for
  // Agri3D)

  uint8_t idx, set_idx;
  uint8_t val = AXIS_SETTINGS_START_VAL;
  for (set_idx = 0; set_idx < AXIS_N_SETTINGS; set_idx++) {
    for (idx = 0; idx < N_AXIS; idx++) {
      switch (set_idx) {
      case 0:
        report_util_float_setting(val + idx, settings.steps_per_mm[idx],
                                  N_DECIMAL_SETTINGVALUE);
        break;
      case 1:
        report_util_float_setting(val + idx, settings.max_rate[idx],
                                  N_DECIMAL_SETTINGVALUE);
        break;
      case 2:
        report_util_float_setting(val + idx,
                                  settings.acceleration[idx] / (60 * 60),
                                  N_DECIMAL_SETTINGVALUE);
        break;
      case 3:
        report_util_float_setting(val + idx, -settings.max_travel[idx],
                                  N_DECIMAL_SETTINGVALUE);
        break;
      case 4:
        report_util_float_setting(val + idx, settings.sgthrs[idx], 0);
        break;
      }
    }
    val += AXIS_SETTINGS_INCREMENT;
  }
}

// Probe string reporting explicitly stripped.\n\nvoid report_ngc_parameters()
// {\n  float coord_data[N_AXIS];\n  \n  if
// (settings_read_coord_data(SETTING_INDEX_G28, coord_data)) {\n
// printPgmString(PSTR("[G28:"));\n    report_util_axis_values(coord_data);\n
// report_util_feedback_line_feed();\n  }\n  if
// (settings_read_coord_data(SETTING_INDEX_G30, coord_data)) {\n
// printPgmString(PSTR("[G30:"));\n    report_util_axis_values(coord_data);\n
// report_util_feedback_line_feed();\n  }\n\n  printPgmString(PSTR("[G92:"));\n
// report_util_axis_values(gc_state.coord_offset);\n
// report_util_feedback_line_feed();\n}

void report_gcode_modes() {
  printPgmString(PSTR("[GC:G"));
  print_uint8_base10(gc_state.modal.motion);

  // WCS and Plane Modes explicitly stripped

  report_util_gcode_modes_G();
  print_uint8_base10(21 - gc_state.modal.units);

  report_util_gcode_modes_G();
  print_uint8_base10(gc_state.modal.distance + 90);

  report_util_gcode_modes_G();
  print_uint8_base10(94 - gc_state.modal.feed_rate);

  if (gc_state.modal.program_flow) {
    report_util_gcode_modes_M();
    if (gc_state.modal.program_flow == PROGRAM_FLOW_PAUSED)
      serial_write('0');
    else
      print_uint8_base10(gc_state.modal.program_flow);
  }

  // Agri3D Custom Relay status reporting
  if (gc_state.modal.custom_relay) {
    report_util_gcode_modes_M();
    print_uint8_base10(gc_state.modal.custom_relay);
  }

  printPgmString(PSTR(" F"));
  printFloat_RateValue(gc_state.feed_rate);

  report_util_feedback_line_feed();
}

void report_startup_line(uint8_t n, char *line) {
  printPgmString(PSTR("$N"));
  print_uint8_base10(n);
  serial_write('=');
  printString(line);
  report_util_line_feed();
}

void report_execute_startup_message(char *line, uint8_t status_code) {
  serial_write('>');
  printString(line);
  serial_write(':');
  report_status_message(status_code);
}

void report_build_info(char *line) {
  printPgmString(PSTR("[VER:" GRBL_VERSION "." GRBL_VERSION_BUILD ":"));
  printString(line);
  report_util_feedback_line_feed();
  printPgmString(PSTR("[OPT:H,15,128")); // Hardcoded for Agri3D specs
  report_util_feedback_line_feed();
}

void report_echo_line_received(char *line) {
  printPgmString(PSTR("[echo: "));
  printString(line);
  report_util_feedback_line_feed();
}

// REALTIME STATUS GENERATOR (The core telemetry heartbeat for ESP32)
void report_realtime_status() {
  uint8_t idx;
  int32_t current_position[N_AXIS];
  memcpy(current_position, sys_position, sizeof(sys_position));
  float print_position[N_AXIS];
  system_convert_array_steps_to_mpos(print_position, current_position);

  serial_write('<');
  switch (sys.state) {
  case STATE_IDLE:
    printPgmString(PSTR("Idle"));
    break;
  case STATE_CYCLE:
    printPgmString(PSTR("Run"));
    break;
  case STATE_HOLD:
    printPgmString(PSTR("Hold"));
    break;
  case STATE_JOG:
    printPgmString(PSTR("Jog"));
    break;
  case STATE_HOMING:
    printPgmString(PSTR("Home"));
    break;
  case STATE_ALARM:
    printPgmString(PSTR("Alarm"));
    break;
  case STATE_CHECK_MODE:
    printPgmString(PSTR("Check"));
    break;
  case STATE_SAFETY_DOOR:
    printPgmString(PSTR("Door"));
    break;
  case STATE_SLEEP:
    printPgmString(PSTR("Sleep"));
    break;
  }

  // Report strictly MPos for FarmBot mapping
  printPgmString(PSTR("|MPos:"));
  report_util_axis_values(print_position);

  // Buffer state
  printPgmString(PSTR("|Bf:"));
  print_uint8_base10(plan_get_block_buffer_available());
  serial_write(',');
  print_uint8_base10(serial_get_rx_buffer_available());

  // Feed Speed
  printPgmString(PSTR("|F:"));
  printFloat_RateValue(st_get_realtime_rate());

  // Agri3D TMC2209 Telemetry [X, Y1, Y2, Z]
  // 0 = OK, 1 = OverTemp, 2 = Disconnected
  printPgmString(PSTR("|TMC:"));
  print_uint8_base10(tmc_state[0]);
  serial_write(',');
  print_uint8_base10(tmc_state[1]);
  serial_write(',');
  print_uint8_base10(tmc_state[2]);
  serial_write(',');
  print_uint8_base10(tmc_state[3]);

  serial_write('>');
  report_util_line_feed();
}

/*
  gcode.c - rs274/ngc parser.
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

// NOTE: Max line number is defined by the g-code standard to be 99999. It seems
// to be an arbitrary value, and some GUIs may require more. So we increased it
// based on a max safe value when converting a float (7.2 digit precision)s to
// an integer.
#define MAX_LINE_NUMBER 10000000
#define MAX_TOOL_NUMBER 255 // Limited by max unsigned 8-bit value

#define AXIS_COMMAND_NONE 0
#define AXIS_COMMAND_NON_MODAL 1
#define AXIS_COMMAND_MOTION_MODE 2
#define AXIS_COMMAND_TOOL_LENGTH_OFFSET 3 // *Undefined but required

// Declare gc extern struct
parser_state_t gc_state;
parser_block_t gc_block;

#define FAIL(status) return (status);

void gc_init() { memset(&gc_state, 0, sizeof(parser_state_t)); }

// Sets g-code parser position in mm. Input in steps. Called by the system abort
// and hard limit pull-off routines.
void gc_sync_position() {
  system_convert_array_steps_to_mpos(gc_state.position, sys_position);
}

// Executes one line of 0-terminated G-Code. The line is assumed to contain only
// uppercase characters and signed floating point values (no whitespace).
// Comments and block delete characters have been removed. In this function, all
// units and positions are converted and exported to grbl's internal functions
// in terms of (mm, mm/min) and absolute machine coordinates, respectively.
uint8_t gc_execute_line(char *line) {
  /* -------------------------------------------------------------------------------------
     STEP 1: Initialize parser block struct and copy current g-code state modes.
     The parser updates these modes and commands as the block line is parser and
     will only be used and executed after successful error-checking. The parser
     block struct also contains a block values struct, word tracking variables,
     and a non-modal commands tracker for the new block. This struct contains
     all of the necessary information to execute the block. */

  memset(&gc_block, 0,
         sizeof(parser_block_t)); // Initialize the parser block struct.
  memcpy(&gc_block.modal, &gc_state.modal,
         sizeof(gc_modal_t)); // Copy current modes

  uint8_t axis_command = AXIS_COMMAND_NONE;
  uint8_t axis_0, axis_1, axis_linear;
  uint8_t coord_select = 0; // Tracks G10 P coordinate selection for execution

  // Initialize bitflag tracking variables for axis indices compatible
  // operations.
  uint8_t axis_words = 0; // XYZ tracking

  // Initialize command and value words and parser flags variables.
  uint16_t command_words =
      0; // Tracks G and M command words. Also used for modal group violations.
  uint16_t value_words = 0; // Tracks value words.
  uint8_t gc_parser_flags = GC_PARSER_NONE;

  // Determine if the line is a jogging motion or a normal g-code block.
  if (line[0] ==
      '$') { // NOTE: `$J=` already parsed when passed to this function.
    // Set G1 and G94 enforced modes to ensure accurate error checks.
    gc_parser_flags |= GC_PARSER_JOG_MOTION;
    gc_block.modal.motion = MOTION_MODE_LINEAR;
    gc_block.modal.feed_rate = FEED_RATE_MODE_UNITS_PER_MIN;
#ifdef USE_LINE_NUMBERS
    gc_block.values.n =
        JOG_LINE_NUMBER; // Initialize default line number reported during jog.
#endif
  }

  /* -------------------------------------------------------------------------------------
     STEP 2: Import all g-code words in the block line. A g-code word is a
     letter followed by a number, which can either be a 'G'/'M' command or
     sets/assigns a command value. Also, perform initial error-checks for
     command word modal group violations, for any repeated words, and for
     negative values set for the value words F, N, P, T, and S. */

  uint8_t word_bit; // Bit-value for assigning tracking variables
  uint8_t char_counter;
  char letter;
  float value;
  uint8_t int_value = 0;
  uint16_t mantissa = 0;
  if (gc_parser_flags & GC_PARSER_JOG_MOTION) {
    char_counter = 3;
  } // Start parsing after `$J=`
  else {
    char_counter = 0;
  }

  while (line[char_counter] != 0) { // Loop until no more g-code words in line.

    // Import the next g-code word, expecting a letter followed by a value.
    // Otherwise, error out.
    letter = line[char_counter];
    if ((letter < 'A') || (letter > 'Z')) {
      FAIL(STATUS_EXPECTED_COMMAND_LETTER);
    } // [Expected word letter]
    char_counter++;
    if (!read_float(line, &char_counter, &value)) {
      FAIL(STATUS_BAD_NUMBER_FORMAT);
    } // [Expected word value]

    // Convert values to smaller uint8 significand and mantissa values for
    // parsing this word. NOTE: Mantissa is multiplied by 100 to catch
    // non-integer command values. This is more accurate than the NIST gcode
    // requirement of x10 when used for commands, but not quite accurate enough
    // for value words that require integers to within 0.0001. This should be a
    // good enough comprimise and catch most all non-integer errors. To make it
    // compliant, we would simply need to change the mantissa to int16, but this
    // add compiled flash space. Maybe update this later.
    int_value = trunc(value);
    mantissa = round(
        100 * (value - int_value)); // Compute mantissa for Gxx.x commands.
    // NOTE: Rounding must be used to catch small floating point errors.

    // Check if the g-code word is supported or errors due to modal group
    // violations or has been repeated in the g-code block. If ok, update the
    // command or record its value.
    switch (letter) {

      /* 'G' and 'M' Command Words: Parse commands and check for modal group
         violations. NOTE: Modal group numbers are defined in Table 4 of NIST
         RS274-NGC v3, pg.20 */

    case 'G':
      // Determine 'G' command and its modal group
      switch (int_value) {
      case 28:
      case 30:
      case 92:
        if (mantissa == 0) {
          if (axis_command) {
            FAIL(STATUS_GCODE_AXIS_COMMAND_CONFLICT);
          }
          axis_command = AXIS_COMMAND_NON_MODAL;
        }
        // No break. Continues to next line.
      case 4:
      case 53:
        word_bit = MODAL_GROUP_G0;
        gc_block.non_modal_command = int_value;
        if ((int_value == 28) || (int_value == 30) || (int_value == 92)) {
          if (!((mantissa == 0) || (mantissa == 10))) {
            FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND);
          }
          gc_block.non_modal_command += mantissa;
          mantissa = 0;
        }
        break;
      case 0:
      case 1:
        if (axis_command) {
          FAIL(STATUS_GCODE_AXIS_COMMAND_CONFLICT);
        }
        axis_command = AXIS_COMMAND_MOTION_MODE;
        // No break. Continues to next line.
      case 80:
        word_bit = MODAL_GROUP_G1;
        gc_block.modal.motion = int_value;
        break;
      case 90:
      case 91:
        if (mantissa == 0) {
          word_bit = MODAL_GROUP_G3;
          gc_block.modal.distance = int_value - 90;
        } else {
          FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND);
        }
        break;
      case 93:
      case 94:
        word_bit = MODAL_GROUP_G5;
        gc_block.modal.feed_rate = 94 - int_value;
        break;
      case 20:
      case 21:
        word_bit = MODAL_GROUP_G6;
        gc_block.modal.units = 21 - int_value;
        break;
      default:
        FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND);
      }
      if (mantissa > 0) {
        FAIL(STATUS_GCODE_COMMAND_VALUE_NOT_INTEGER);
      } // [Unsupported or invalid Gxx.x command]
      // Check for more than one command per modal group violations in the
      // current block NOTE: Variable 'word_bit' is always assigned, if the
      // command is valid.
      if (bit_istrue(command_words, bit(word_bit))) {
        FAIL(STATUS_GCODE_MODAL_GROUP_VIOLATION);
      }
      command_words |= bit(word_bit);
      break;

    case 'M':
      if (mantissa > 0) {
        FAIL(STATUS_GCODE_COMMAND_VALUE_NOT_INTEGER);
      }
      switch (int_value) {
      case 0:
      case 1:
      case 2:
      case 30:
        word_bit = MODAL_GROUP_M4;
        switch (int_value) {
        case 0:
          gc_block.modal.program_flow = PROGRAM_FLOW_PAUSED;
          break;
        case 1:
          break;
        default:
          gc_block.modal.program_flow = int_value;
        }
        break;
      case 100:
      case 101:
      case 102:
      case 103:
      case 104:
      case 105:
      case 106:
        word_bit = MODAL_GROUP_AGRI3D;
        gc_block.modal.custom_relay = int_value;
        break;
#ifdef ENABLE_PARKING_OVERRIDE_CONTROL
      case 56:
        word_bit = MODAL_GROUP_M9;
        gc_block.modal.override = OVERRIDE_PARKING_MOTION;
        break;
#endif
      default:
        FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND);
      }

      // Check for more than one command per modal group violations in the
      // current block NOTE: Variable 'word_bit' is always assigned, if the
      // command is valid.
      if (bit_istrue(command_words, bit(word_bit))) {
        FAIL(STATUS_GCODE_MODAL_GROUP_VIOLATION);
      }
      command_words |= bit(word_bit);
      break;

    // NOTE: All remaining letters assign values.
    default:

      switch (letter) {
      case 'F':
        word_bit = WORD_F;
        gc_block.values.f = value;
        break;
      case 'N':
        word_bit = WORD_N;
        gc_block.values.n = trunc(value);
        break;
      case 'P':
        word_bit = WORD_P;
        gc_block.values.p = value;
        break;
      case 'X':
        word_bit = WORD_X;
        gc_block.values.xyz[X_AXIS] = value;
        axis_words |= (1 << X_AXIS);
        break;
      case 'Y':
        word_bit = WORD_Y;
        gc_block.values.xyz[Y_AXIS] = value;
        axis_words |= (1 << Y_AXIS);
        break;
      case 'Z':
        word_bit = WORD_Z;
        gc_block.values.xyz[Z_AXIS] = value;
        axis_words |= (1 << Z_AXIS);
        break;
      default:
        FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND);
      }

      if (bit_istrue(value_words, bit(word_bit))) {
        FAIL(STATUS_GCODE_WORD_REPEATED);
      }
      if (bit(word_bit) & (bit(WORD_F) | bit(WORD_N) | bit(WORD_P))) {
        if (value < 0.0) {
          FAIL(STATUS_NEGATIVE_VALUE);
        }
      }
      value_words |= bit(word_bit); // Flag to indicate parameter assigned.
    }
  }
  // Parsing complete!

  /* -------------------------------------------------------------------------------------
     STEP 3: Error-check all commands and values passed in this block. This step
     ensures all of the commands are valid for execution and follows the NIST
     standard as closely as possible. If an error is found, all commands and
     values in this block are dumped and will not update the active system
     g-code modes. If the block is ok, the active system g-code modes will be
     updated based on the commands of this block, and signal for it to be
     executed.

     Also, we have to pre-convert all of the values passed based on the modes
     set by the parsed block. There are a number of error-checks that require
     target information that can only be accurately calculated if we convert
     these values in conjunction with the error-checking. This relegates the
     next execution step as only updating the system g-code modes and performing
     the programmed actions in order. The execution step should not require any
     conversion calculations and would only require minimal checks necessary to
     execute.
  */

  /* NOTE: At this point, the g-code block has been parsed and the block line
     can be freed. NOTE: It's also possible, at some future point, to break up
     STEP 2, to allow piece-wise parsing of the block on a per-word basis,
     rather than the entire block. This could remove the need for maintaining a
     large string variable for the entire block and free up some memory. To do
     this, this would simply need to retain all of the data in STEP 1, such as
     the new block data struct, the modal group and value bitflag tracking
     variables, and axis array indices compatible variables. This data contains
     all of the information necessary to error-check the new g-code block when
     the EOL character is received. However, this would break Grbl's startup
     lines in how it currently works and would require some refactoring to make
     it compatible.
  */

  // [0. Non-specific/common error-checks and miscellaneous setup]:

  // Determine implicit axis command conditions. Axis words have been passed,
  // but no explicit axis command has been sent. If so, set axis command to
  // current motion mode.
  if (axis_words) {
    if (!axis_command) {
      axis_command = AXIS_COMMAND_MOTION_MODE;
    } // Assign implicit motion-mode
  }

  // Check for valid line number N value.
  if (bit_istrue(value_words, bit(WORD_N))) {
    // Line number value cannot be less than zero (done) or greater than max
    // line number.
    if (gc_block.values.n > MAX_LINE_NUMBER) {
      FAIL(STATUS_GCODE_INVALID_LINE_NUMBER);
    } // [Exceeds max line number]
  }
  // bit_false(value_words,bit(WORD_N)); // NOTE: Single-meaning value word. Set
  // at end of error-checking.

  // Track for unused words at the end of error-checking.
  // NOTE: Single-meaning value words are removed all at once at the end of
  // error-checking, because they are always used when present. This was done to
  // save a few bytes of flash. For clarity, the single-meaning value words may
  // be removed as they are used. Also, axis words are treated in the same way.
  // If there is an explicit/implicit axis command, XYZ words are always used
  // and are are removed at the end of error-checking.

  // [1. Comments ]: MSG's NOT SUPPORTED. Comment handling performed by
  // protocol.

  // [2. Set feed rate mode ]: G93 F word missing with G1,G2/3 active,
  // implicitly or explicitly. Feed rate
  //   is not defined after switching to G94 from G93.
  // NOTE: For jogging, ignore prior feed rate mode. Enforce G94 and check for
  // required F word.
  if (gc_parser_flags & GC_PARSER_JOG_MOTION) {
    if (bit_isfalse(value_words, bit(WORD_F))) {
      FAIL(STATUS_GCODE_UNDEFINED_FEED_RATE);
    }
    if (gc_block.modal.units == UNITS_MODE_INCHES) {
      gc_block.values.f *= MM_PER_INCH;
    }
  } else {
    if (gc_block.modal.feed_rate == FEED_RATE_MODE_INVERSE_TIME) { // = G93
      // NOTE: G38 can also operate in inverse time, but is undefined as an
      // error. Missing F word check added here.
      if (axis_command == AXIS_COMMAND_MOTION_MODE) {
        if ((gc_block.modal.motion != MOTION_MODE_NONE) &&
            (gc_block.modal.motion != MOTION_MODE_SEEK)) {
          if (bit_isfalse(value_words, bit(WORD_F))) {
            FAIL(STATUS_GCODE_UNDEFINED_FEED_RATE);
          } // [F word missing]
        }
      }
      // NOTE: It seems redundant to check for an F word to be passed after
      // switching from G94 to G93. We would accomplish the exact same thing if
      // the feed rate value is always reset to zero and undefined after each
      // inverse time block, since the commands that use this value already
      // perform undefined checks. This would also allow other commands,
      // following this switch, to execute and not error out needlessly. This
      // code is combined with the above feed rate mode and the below set feed
      // rate error-checking.

      // [3. Set feed rate ]: F is negative (done.)
      // - In inverse time mode: Always implicitly zero the feed rate value
      // before and after block completion. NOTE: If in G93 mode or switched
      // into it from G94, just keep F value as initialized zero or passed F
      // word value in the block. If no F word is passed with a motion command
      // that requires a feed rate, this will error out in the motion modes
      // error-checking. However, if no F word is passed with NO motion command
      // that requires a feed rate, we simply move on and the state feed rate
      // value gets updated to zero and remains undefined.
    } else { // = G94
      // - In units per mm mode: If F word passed, ensure value is in mm/min,
      // otherwise push last state value.
      if (gc_state.modal.feed_rate ==
          FEED_RATE_MODE_UNITS_PER_MIN) { // Last state is also G94
        if (bit_istrue(value_words, bit(WORD_F))) {
          if (gc_block.modal.units == UNITS_MODE_INCHES) {
            gc_block.values.f *= MM_PER_INCH;
          }
        } else {
          gc_block.values.f = gc_state.feed_rate; // Push last state feed rate
        }
      } // Else, switching to G94 from G93, so don't push last state feed rate.
        // Its undefined or the passed F word value.
    }
  }
// bit_false(value_words,bit(WORD_F)); // NOTE: Single-meaning value word. Set
// at end of error-checking.

// [4. Set spindle speed ]: N/A
// [5. Select tool ]: N/A
// [6. Change tool ]: N/A
// [7. Spindle control ]: N/A
// [8. Coolant control ]: N/A
// [9. Override control ]: Not supported except for a Grbl-only parking motion
// override control.
#ifdef ENABLE_PARKING_OVERRIDE_CONTROL
  if (bit_istrue(command_words,
                 bit(MODAL_GROUP_M9))) { // Already set as enabled in parser.
    if (bit_istrue(value_words, bit(WORD_P))) {
      if (gc_block.values.p == 0.0) {
        gc_block.modal.override = OVERRIDE_DISABLED;
      }
      bit_false(value_words, bit(WORD_P));
    }
  }
#endif

  // [10. Dwell ]: P value missing. P is negative (done.) NOTE: See below.
  if (gc_block.non_modal_command == NON_MODAL_DWELL) {
    if (bit_isfalse(value_words, bit(WORD_P))) {
      FAIL(STATUS_GCODE_VALUE_WORD_MISSING);
    } // [P word missing]
    bit_false(value_words, bit(WORD_P));
  }

  // [11. Set active plane ]: N/A

  // [12. Set length units ]: N/A
  uint8_t idx;
  if (gc_block.modal.units == UNITS_MODE_INCHES) {
    for (idx = 0; idx < N_AXIS; idx++) {
      if (bit_istrue(axis_words, bit(idx))) {
        gc_block.values.xyz[idx] *= MM_PER_INCH;
      }
    }
  }

  // [16. Set path control mode ]: N/A. Only G61. G61.1 and G64 NOT SUPPORTED.
  // [17. Set distance mode ]: N/A. Only G91.1. G90.1 NOT SUPPORTED.
  // [18. Set retract mode ]: NOT SUPPORTED.

  // [19. Remaining non-modal actions ]: Check go to predefined position, set
  // G10, or set axis offsets. NOTE: We need to separate the non-modal commands
  // that are axis word-using (G10/G28/G30/G92), as these commands all treat
  // axis words differently. G10 as absolute offsets or computes current
  // position as the axis value, G92 similarly to G10 L20, and G28/30 as an
  // intermediate target position that observes all the current coordinate
  // system and G92 offsets.
  switch (gc_block.non_modal_command) {
  case NON_MODAL_SET_COORDINATE_OFFSET:
    // [G92 Errors]: No axis words.
    if (!axis_words) {
      FAIL(STATUS_GCODE_NO_AXIS_WORDS);
    }

    // Update axes defined only in block.
    for (idx = 0; idx < N_AXIS; idx++) {
      if (bit_istrue(axis_words, bit(idx))) {
        // G92 = MPos - WPos
        gc_block.values.xyz[idx] =
            gc_state.position[idx] - gc_block.values.xyz[idx];
      } else {
        gc_block.values.xyz[idx] = gc_state.coord_offset[idx];
      }
    }
    break;

  default:

    // At this point, the rest of the explicit axis commands treat the axis
    // values as the traditional target position with the coordinate system
    // offsets, G92 offsets, absolute override, and distance modes applied. This
    // includes the motion mode commands. We can now pre-compute the target
    // position. NOTE: Tool offsets may be appended to these conversions when/if
    // this feature is added.
    if (axis_words) {
      for (idx = 0; idx < N_AXIS; idx++) {
        if (bit_isfalse(axis_words, bit(idx))) {
          gc_block.values.xyz[idx] = gc_state.position[idx];
        } else {
          if (gc_block.non_modal_command != NON_MODAL_ABSOLUTE_OVERRIDE) {
            if (gc_block.modal.distance == DISTANCE_MODE_ABSOLUTE) {
              gc_block.values.xyz[idx] += gc_state.coord_offset[idx];
            } else {
              gc_block.values.xyz[idx] += gc_state.position[idx];
            }
          }
        }
      }
    }

    // Check remaining non-modal commands for errors.
    switch (gc_block.non_modal_command) {
    case NON_MODAL_GO_HOME_0: // G28
    case NON_MODAL_GO_HOME_1: // G30
      // [G28/30 Errors]: Cutter compensation is enabled.
      // Retreive G28/30 go-home position data (in machine coordinates) from
      // EEPROM NOTE: Store parameter data in IJK values. By rule, they are not
      // in use with this command.
      if (gc_block.non_modal_command == NON_MODAL_GO_HOME_0) {
        if (!settings_read_coord_data(SETTING_INDEX_G28, gc_block.values.ijk)) {
          FAIL(STATUS_SETTING_READ_FAIL);
        }
      } else { // == NON_MODAL_GO_HOME_1
        if (!settings_read_coord_data(SETTING_INDEX_G30, gc_block.values.ijk)) {
          FAIL(STATUS_SETTING_READ_FAIL);
        }
      }
      if (axis_words) {
        // Move only the axes specified in secondary move.
        for (idx = 0; idx < N_AXIS; idx++) {
          if (!(axis_words & (1 << idx))) {
            gc_block.values.ijk[idx] = gc_state.position[idx];
          }
        }
      } else {
        axis_command =
            AXIS_COMMAND_NONE; // Set to none if no intermediate motion.
      }
      break;
    case NON_MODAL_SET_HOME_0: // G28.1
    case NON_MODAL_SET_HOME_1: // G30.1
      // [G28.1/30.1 Errors]: Cutter compensation is enabled.
      // NOTE: If axis words are passed here, they are interpreted as an
      // implicit motion mode.
      break;
    case NON_MODAL_RESET_COORDINATE_OFFSET:
      // NOTE: If axis words are passed here, they are interpreted as an
      // implicit motion mode.
      break;
    case NON_MODAL_ABSOLUTE_OVERRIDE:
      // [G53 Errors]: G0 and G1 are not active. Cutter compensation is enabled.
      // NOTE: All explicit axis word commands are in this modal group. So no
      // implicit check necessary.
      if (!(gc_block.modal.motion == MOTION_MODE_SEEK ||
            gc_block.modal.motion == MOTION_MODE_LINEAR)) {
        FAIL(STATUS_GCODE_G53_INVALID_MOTION_MODE); // [G53 G0/1 not active]
      }
      break;
    }
  }

  // [20. Motion modes ]:
  if (gc_block.modal.motion == MOTION_MODE_NONE) {
    // [G80 Errors]: Axis word are programmed while G80 is active.
    // NOTE: Even non-modal commands or TLO that use axis words will throw this
    // strict error.
    if (axis_words) {
      FAIL(STATUS_GCODE_AXIS_WORDS_EXIST);
    } // [No axis words allowed]

    // Check remaining motion modes, if axis word are implicit (exist and not
    // used by G10/28/30/92), or was explicitly commanded in the g-code block.
  } else if (axis_command == AXIS_COMMAND_MOTION_MODE) {

    if (gc_block.modal.motion == MOTION_MODE_SEEK) {
      // [G0 Errors]: Axis letter not configured or without real value (done.)
      // Axis words are optional. If missing, set axis command flag to ignore
      // execution.
      if (!axis_words) {
        axis_command = AXIS_COMMAND_NONE;
      }

      // All remaining motion modes (all but G0 and G80), require a valid feed
      // rate value. In units per mm mode, the value must be positive. In
      // inverse time mode, a positive value must be passed with each block.
    } else {
      // Check if feed rate is defined for the motion modes that require it.
      if (gc_block.values.f == 0.0) {
        FAIL(STATUS_GCODE_UNDEFINED_FEED_RATE);
      } // [Feed rate undefined]

      switch (gc_block.modal.motion) {
      case MOTION_MODE_LINEAR:
        // [G1 Errors]: Feed rate undefined. Axis letter not configured or
        // without real value. Axis words are optional. If missing, set axis
        // command flag to ignore execution.
        if (!axis_words) {
          axis_command = AXIS_COMMAND_NONE;
        }
        break;
        // Arcs and Probing have been completely stripped from the parser.
      }
    }
  }

  // [21. Program flow ]: No error checks required.

  // [0. Non-specific error-checks]: Complete unused value words check, i.e. IJK
  // used when in arc radius mode, or axis words that aren't used in the block.
  if (gc_parser_flags & GC_PARSER_JOG_MOTION) {
    bit_false(value_words, (bit(WORD_N) | bit(WORD_F)));
  } else {
    bit_false(value_words, (bit(WORD_N) | bit(WORD_F)));
  }
  if (axis_command) {
    bit_false(value_words, (bit(WORD_X) | bit(WORD_Y) | bit(WORD_Z)));
  } // Remove axis words.
  if (value_words) {
    FAIL(STATUS_GCODE_UNUSED_WORDS);
  } // [Unused words]

  /* -------------------------------------------------------------------------------------
     STEP 4: EXECUTE!!
     Assumes that all error-checking has been completed and no failure modes
     exist. We just need to update the state and execute the block according to
     the order-of-execution.
  */

  // Initialize planner data struct for motion blocks.
  plan_line_data_t plan_data;
  plan_line_data_t *pl_data = &plan_data;
  memset(pl_data, 0, sizeof(plan_line_data_t)); // Zero pl_data struct

  // Intercept jog commands and complete error checking for valid jog commands
  // and execute. NOTE: G-code parser state is not updated, except the position
  // to ensure sequential jog targets are computed correctly. The final parser
  // position after a jog is updated in protocol_execute_realtime() when jogging
  // completes or is canceled.
  if (gc_parser_flags & GC_PARSER_JOG_MOTION) {
    // Only distance and unit modal commands and G53 absolute override command
    // are allowed. NOTE: Feed rate word and axis word checks have already been
    // performed in STEP 3.
    if (command_words &
        ~(bit(MODAL_GROUP_G3) | bit(MODAL_GROUP_G6) | bit(MODAL_GROUP_G0))) {
      FAIL(STATUS_INVALID_JOG_COMMAND)
    };
    if (!(gc_block.non_modal_command == NON_MODAL_ABSOLUTE_OVERRIDE ||
          gc_block.non_modal_command == NON_MODAL_NO_ACTION)) {
      FAIL(STATUS_INVALID_JOG_COMMAND);
    }

    // Initialize planner data.
    plan_data.condition = 0;

    uint8_t status = jog_execute(&plan_data, &gc_block);
    if (status == STATUS_OK) {
      memcpy(gc_state.position, gc_block.values.xyz,
             sizeof(gc_block.values.xyz));
    }
    return (status);
  }

  // Laser mode explicitly stripped.

  // [0. Non-specific/common error-checks and miscellaneous setup]:
  // NOTE: If no line number is present, the value is zero.
  gc_state.line_number = gc_block.values.n;
#ifdef USE_LINE_NUMBERS
  pl_data->line_number = gc_state.line_number; // Record data for planner use.
#endif

  // [1. Comments feedback ]:  NOT SUPPORTED

  // [2. Set feed rate mode ]:
  gc_state.modal.feed_rate = gc_block.modal.feed_rate;
  if (gc_state.modal.feed_rate) {
    pl_data->condition |= PL_COND_FLAG_INVERSE_TIME;
  } // Set condition flag for planner use.

  // [3. Set feed rate ]:
  gc_state.feed_rate =
      gc_block.values
          .f; // Always copy this value. See feed rate error-checking.
  pl_data->feed_rate = gc_state.feed_rate; // Record data for planner use.

  // [4-6. Spindle / Tool ]: N/A

  // [7-8. Agri3D Custom Native Relays ]:
  if (gc_block.modal.custom_relay) {
    protocol_buffer_synchronize(); // Complete all buffered movements before
                                   // toggling.
    switch (gc_block.modal.custom_relay) {
    case 100:
      relay_water(1);
      break; // M100
    case 101:
      relay_water(0);
      break; // M101
    case 102:
      relay_fert(1);
      break; // M102
    case 103:
      relay_fert(0);
      break; // M103
    case 104:
      relay_weeder(1);
      break; // M104
    case 105:
      relay_weeder(0);
      break; // M105
    }
    // Update state to match for [GC:] report strings if needed.
    gc_state.modal.custom_relay = gc_block.modal.custom_relay;
  }

// [9. Override control ]: Not supported except for a Grbl-only parking control.
#ifdef ENABLE_PARKING_OVERRIDE_CONTROL
  if (gc_state.modal.override != gc_block.modal.override) {
    \n gc_state.modal.override = gc_block.modal.override;
    \n mc_override_ctrl_update(gc_state.modal.override);
    \n
  }
#endif

  // [10. Dwell ]:
  if (gc_block.non_modal_command == NON_MODAL_DWELL) {
    mc_dwell(gc_block.values.p);
  }

  // [11-15. Setup Actions stripped]: N/A
  gc_state.modal.units = gc_block.modal.units;

  // [16. Set path control mode ]: G61.1/G64 NOT SUPPORTED
  // gc_state.modal.control = gc_block.modal.control; // NOTE: Always default.

  // [17. Set distance mode ]:
  gc_state.modal.distance = gc_block.modal.distance;

  // [18. Set retract mode ]: NOT SUPPORTED

  // [19. Go to predefined position, or Set axis offsets ]:
  switch (gc_block.non_modal_command) {
  case NON_MODAL_GO_HOME_0:
  case NON_MODAL_GO_HOME_1:
    // Move to intermediate position before going home. Obeys current coordinate
    // system and offsets and absolute and incremental modes.
    pl_data->condition |=
        PL_COND_FLAG_RAPID_MOTION; // Set rapid motion condition flag.
    if (axis_command) {
      mc_line(gc_block.values.xyz, pl_data);
    }
    mc_line(gc_block.values.ijk, pl_data);
    memcpy(gc_state.position, gc_block.values.ijk, N_AXIS * sizeof(float));
    break;
  case NON_MODAL_SET_HOME_0:
    settings_write_coord_data(SETTING_INDEX_G28, gc_state.position);
    break;
  case NON_MODAL_SET_HOME_1:
    settings_write_coord_data(SETTING_INDEX_G30, gc_state.position);
    break;
  case NON_MODAL_SET_COORDINATE_OFFSET:
    memcpy(gc_state.coord_offset, gc_block.values.xyz,
           sizeof(gc_block.values.xyz));
    system_flag_wco_change();
    break;
  case NON_MODAL_RESET_COORDINATE_OFFSET:
    clear_vector(
        gc_state.coord_offset); // Disable G92 offsets by zeroing offset vector.
    system_flag_wco_change();
    break;
  }

  // [20. Motion modes ]:
  // NOTE: Commands G10,G28,G30,G92 lock out and prevent axis words from use in
  // motion modes. Enter motion modes only if there are axis words or a motion
  // mode command word in the block.
  gc_state.modal.motion = gc_block.modal.motion;
  if (gc_state.modal.motion != MOTION_MODE_NONE) {
    if (axis_command == AXIS_COMMAND_MOTION_MODE) {
      if (gc_state.modal.motion == MOTION_MODE_LINEAR) {
        mc_line(gc_block.values.xyz, pl_data);
      } else if (gc_state.modal.motion == MOTION_MODE_SEEK) {
        pl_data->condition |=
            PL_COND_FLAG_RAPID_MOTION; // Set rapid motion condition flag.
        mc_line(gc_block.values.xyz, pl_data);
      }

      // As far as the parser is concerned, the position is now == target.
      memcpy(gc_state.position, gc_block.values.xyz,
             sizeof(gc_block.values
                        .xyz)); // gc_state.position[] = gc_block.values.xyz[]
    }
  }

  // [21. Program flow ]:
  // M0,M1,M2,M30: Perform non-running program flow actions. During a program
  // pause, the buffer may refill and can only be resumed by the cycle start
  // run-time command.
  gc_state.modal.program_flow = gc_block.modal.program_flow;
  if (gc_state.modal.program_flow) {
    protocol_buffer_synchronize(); // Sync and finish all remaining buffered
                                   // motions before moving on.
    if (gc_state.modal.program_flow == PROGRAM_FLOW_PAUSED) {
      if (sys.state != STATE_CHECK_MODE) {
        system_set_exec_state_flag(
            EXEC_FEED_HOLD);         // Use feed hold for program pause.
        protocol_execute_realtime(); // Execute suspend.
      }
    } else { // == PROGRAM_FLOW_COMPLETED
      // Upon program complete, only a subset of g-codes reset to certain
      // defaults, according to LinuxCNC's program end descriptions and testing.
      // Only modal groups [G-code 1,2,3,5,7,12] and [M-code 7,8,9] reset to
      // [G1,G17,G90,G94,G40,G54,M5,M9,M48]. The remaining modal groups [G-code
      // 4,6,8,10,13,14,15] and [M-code 4,5,6] and the modal words [F,S,T,H] do
      // not reset.
      gc_state.modal.motion = MOTION_MODE_LINEAR;
      gc_state.modal.distance = DISTANCE_MODE_ABSOLUTE;
      gc_state.modal.feed_rate = FEED_RATE_MODE_UNITS_PER_MIN;
      gc_state.modal.custom_relay = 0; // Reset tracking
#ifdef ENABLE_PARKING_OVERRIDE_CONTROL
#ifdef DEACTIVATE_PARKING_UPON_INIT
      gc_state.modal.override = OVERRIDE_DISABLED;
#else
      gc_state.modal.override = OVERRIDE_PARKING_MOTION;
#endif
#endif

#ifdef RESTORE_OVERRIDES_AFTER_PROGRAM_END
      sys.f_override = DEFAULT_FEED_OVERRIDE;
      sys.r_override = DEFAULT_RAPID_OVERRIDE;
#endif

      // Execute coordinate change and spindle/coolant stop.
      if (sys.state != STATE_CHECK_MODE) {
        system_flag_wco_change(); // Set to refresh immediately just in case
                                  // something altered.
        // Stop all relays at program end.
        relay_water(0);
        relay_fert(0);
        relay_weeder(0);
      }
      report_feedback_message(MESSAGE_PROGRAM_END);
    }
    gc_state.modal.program_flow = PROGRAM_FLOW_RUNNING; // Reset program flow.
  }

  // TODO: % to denote start of program.

  return (STATUS_OK);
}

/*
  Not supported:

  - Canned cycles
  - Tool radius compensation
  - A,B,C-axes
  - Evaluation of expressions
  - Variables
  - Override control (TBD)
  - Tool changes
  - Switches

   (*) Indicates optional parameter, enabled through config.h and re-compile
   group 0 = {G92.2, G92.3} (Non modal: Cancel and re-enable G92 offsets)
   group 1 = {G81 - G89} (Motion modes: Canned cycles)
   group 4 = {M1} (Optional stop, ignored)
   group 6 = {M6} (Tool change)
   group 7 = {G41, G42} cutter radius compensation (G40 is supported)
   group 8 = {G43} tool length offset (G43.1/G49 are supported)
   group 8 = {M7*} enable mist coolant (* Compile-option)
   group 9 = {M48, M49, M56*} enable/disable override switches (*
  Compile-option) group 10 = {G98, G99} return mode canned cycles group 13 =
  {G61.1, G64} path control mode (G61 is supported)
*/

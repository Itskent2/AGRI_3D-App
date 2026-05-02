/*
  gcode.h - rs274/ngc parser.
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

#ifndef gcode_h
#define gcode_h

// Define modal group internal numbers for checking multiple command violations
// and tracking the type of command that is called in the block. A modal group
// is a group of g-code commands that are mutually exclusive, or cannot exist on
// the same line, because they each toggle a state or execute a unique motion.
// These are defined in the NIST RS274-NGC v3 g-code standard, available online,
// and are similar/identical to other g-code interpreters by manufacturers
// (Haas,Fanuc,Mazak,etc). NOTE: Modal group define values must be sequential
// and starting from zero.
#define MODAL_GROUP_G0 0 // [G4,G10,G28,G28.1,G30,G30.1,G53,G92,G92.1] Non-modal
#define MODAL_GROUP_G1 1 // [G0,G1,G80] Motion
#define MODAL_GROUP_G3 3 // [G90,G91] Distance mode
#define MODAL_GROUP_G5 5 // [G93,G94] Feed rate mode
#define MODAL_GROUP_G6 6 // [G20,G21] Units

#define MODAL_GROUP_M4 11     // [M0,M1,M2,M30] Stopping
#define MODAL_GROUP_M9 14     // [M56] Override control
#define MODAL_GROUP_AGRI3D 15 // [M100-M105] Custom Relays

// Define command actions for within execution-type modal groups (motion,
// stopping, non-modal). Used internally by the parser to know which command to
// execute. NOTE: Some macro values are assigned specific values to make g-code
// state reporting and parsing compile a litte smaller. Necessary due to being
// completely out of flash on the 328p. Although not ideal, just be careful with
// values that state 'do not alter' and check both report.c and gcode.c to see
// how they are used, if you need to alter them.

// Modal Group G0: Non-modal actions
#define NON_MODAL_NO_ACTION 0                 // (Default: Must be zero)
#define NON_MODAL_DWELL 4                     // G4 (Do not alter value)
#define NON_MODAL_GO_HOME_0 28                // G28 (Do not alter value)
#define NON_MODAL_SET_HOME_0 38               // G28.1 (Do not alter value)
#define NON_MODAL_GO_HOME_1 30                // G30 (Do not alter value)
#define NON_MODAL_SET_HOME_1 40               // G30.1 (Do not alter value)
#define NON_MODAL_ABSOLUTE_OVERRIDE 53        // G53 (Do not alter value)
#define NON_MODAL_SET_COORDINATE_OFFSET 92    // G92 (Do not alter value)
#define NON_MODAL_RESET_COORDINATE_OFFSET 102 // G92.1 (Do not alter value)

// Modal Group G1: Motion modes
#define MOTION_MODE_SEEK 0   // G0 (Default: Must be zero)
#define MOTION_MODE_LINEAR 1 // G1 (Do not alter value)
#define MOTION_MODE_NONE 80  // G80 (Do not alter value)

// Modal Group G3: Distance mode
#define DISTANCE_MODE_ABSOLUTE 0    // G90 (Default: Must be zero)
#define DISTANCE_MODE_INCREMENTAL 1 // G91 (Do not alter value)

// Modal Group M4: Program flow
#define PROGRAM_FLOW_RUNNING 0 // (Default: Must be zero)
#define PROGRAM_FLOW_PAUSED 3  // M0
#define PROGRAM_FLOW_OPTIONAL_STOP                                             \
  1 // M1 NOTE: Not supported, but valid and ignored.
#define PROGRAM_FLOW_COMPLETED_M2 2   // M2 (Do not alter value)
#define PROGRAM_FLOW_COMPLETED_M30 30 // M30 (Do not alter value)

// Modal Group G5: Feed rate mode
#define FEED_RATE_MODE_UNITS_PER_MIN 0 // G94 (Default: Must be zero)
#define FEED_RATE_MODE_INVERSE_TIME 1  // G93 (Do not alter value)

// Modal Group G6: Units mode
#define UNITS_MODE_MM 0     // G21 (Default: Must be zero)
#define UNITS_MODE_INCHES 1 // G20 (Do not alter value)

// Modal Group Custom: Agri3D Relays
#define RELAY_WATER_ON 100
#define RELAY_WATER_OFF 101
#define RELAY_FERT_ON 102
#define RELAY_FERT_OFF 103
#define RELAY_WEEDER_ON 104
#define RELAY_WEEDER_OFF 105

// Modal Group M9: Override control
#ifdef DEACTIVATE_PARKING_UPON_INIT
#define OVERRIDE_DISABLED 0       // (Default: Must be zero)
#define OVERRIDE_PARKING_MOTION 1 // M56
#else
#define OVERRIDE_PARKING_MOTION 0 // M56 (Default: Must be zero)
#define OVERRIDE_DISABLED 1       // Parking disabled.
#endif

// Define parameter word mapping.
#define WORD_F 0
#define WORD_N 5
#define WORD_P 6
#define WORD_X 10
#define WORD_Y 11
#define WORD_Z 12

// Define g-code parser position updating flags
#define GC_UPDATE_POS_TARGET 0 // Must be zero
#define GC_UPDATE_POS_SYSTEM 1
#define GC_UPDATE_POS_NONE 2
#define GC_UPDATE_POS_TARGET 0 // Restored for Probe fallback

// Define gcode parser flags for handling special cases.
#define GC_PARSER_NONE 0 // Must be zero.
#define GC_PARSER_JOG_MOTION bit(0)
#define GC_PARSER_CHECK_MANTISSA bit(1)
#define GC_PARSER_LASER_FORCE_SYNC bit(5)
#define GC_PARSER_LASER_DISABLE bit(6)
#define GC_PARSER_LASER_ISMOTION bit(7)

// NOTE: When this struct is zeroed, the above defines set the defaults for the
// system.
typedef struct {
  uint8_t motion;       // {G0,G1,G80}
  uint8_t feed_rate;    // {G93,G94}
  uint8_t units;        // {G20,G21}
  uint8_t distance;     // {G90,G91}
  uint8_t program_flow; // {M0,M1,M2,M30}
  uint8_t custom_relay; // {M100-M105}
  uint8_t override;     // {M56}
} gc_modal_t;

typedef struct {
  float f;      // Feed
  float ijk[3]; // Storage for G28/G30
  int32_t n;    // Line number
  float p;      // Dwell parameters
  float xyz[3]; // X,Y,Z Translational axes
} gc_values_t;

typedef struct {
  gc_modal_t modal;

  float feed_rate;     // Millimeters/min
  int32_t line_number; // Last line number sent

  float position[N_AXIS]; // Where the interpreter considers the tool to be at
                          // this point in the code

  float coord_offset[N_AXIS]; // Retains the G92 coordinate offset (work
                              // coordinates) relative to machine zero in mm.
                              // Non-persistent. Cleared upon reset and boot.
} parser_state_t;
extern parser_state_t gc_state;

typedef struct {
  uint8_t non_modal_command;
  gc_modal_t modal;
  gc_values_t values;
} parser_block_t;

// Initialize the parser
void gc_init();

// Execute one block of rs275/ngc/g-code
uint8_t gc_execute_line(char *line);

// Set g-code parser position. Input in steps.
void gc_sync_position();

#endif

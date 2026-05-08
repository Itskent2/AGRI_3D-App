/*
  relays.c - Native PORTB relay subsystem for Agri3D
  All legacy CNC Spindle/Coolant logic completely removed.
*/

#include "grbl-agri3d.h"

void relays_init() {
  // Water and Fert are Active Low -> Set HIGH to turn OFF
  // Weeder is Active High -> Set LOW to turn OFF
  RELAY_PORT |= (1 << WATER_PUMP_BIT) | (1 << FERT_PUMP_BIT);
  RELAY_PORT &= ~(1 << WEEDER_MOT_BIT);
  RELAY_DDR |= RELAY_MASK;   // Now set as output
}

void relays_stop_all() {
  // Water and Fert are Active Low -> Pull HIGH to deactivate
  // Weeder is Active High -> Pull LOW to deactivate
  RELAY_PORT |= (1 << WATER_PUMP_BIT) | (1 << FERT_PUMP_BIT);
  RELAY_PORT &= ~(1 << WEEDER_MOT_BIT);
}

// -------------------------------------------------------------
// DEDICATED TOGGLES (MIXED LOGIC)
// -------------------------------------------------------------
void relay_water(uint8_t state) {
  if (state) {
    RELAY_PORT &= ~(1 << WATER_PUMP_BIT); // Pull LOW to turn ON (Active Low)
  } else {
    RELAY_PORT |= (1 << WATER_PUMP_BIT);  // Pull HIGH to turn OFF
  }
}

void relay_fert(uint8_t state) {
  if (state) {
    RELAY_PORT &= ~(1 << FERT_PUMP_BIT); // Pull LOW to turn ON (Active Low)
  } else {
    RELAY_PORT |= (1 << FERT_PUMP_BIT);  // Pull HIGH to turn OFF
  }
}

void relay_weeder(uint8_t state) {
  if (state) {
    RELAY_PORT |= (1 << WEEDER_MOT_BIT);
  } else {
    RELAY_PORT &= ~(1 << WEEDER_MOT_BIT);
  }
}
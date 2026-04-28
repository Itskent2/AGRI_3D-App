/*
  relays.c - Native PORTB relay subsystem for Agri3D
  All legacy CNC Spindle/Coolant logic completely removed.
*/

#include "grbl-agri3d.h"

void relays_init() {
  RELAY_PORT |= RELAY_MASK; // Pre-load HIGH state
  RELAY_DDR |= RELAY_MASK;  // Now set as output (seamlessly transitions to HIGH)
}

void relays_stop_all() {
  // ACTIVE LOW RELAYS: Pull pins HIGH to deactivate
  RELAY_PORT |= RELAY_MASK;
}

// -------------------------------------------------------------
// DEDICATED TOGGLES (ACTIVE LOW LOGIC)
// -------------------------------------------------------------
void relay_water(uint8_t state) {
  if (state) {
    RELAY_PORT &= ~(1 << WATER_PUMP_BIT);
  } // Pull LOW to turn ON
  else {
    RELAY_PORT |= (1 << WATER_PUMP_BIT);
  } // Pull HIGH to turn OFF
}

void relay_fert(uint8_t state) {
  if (state) {
    RELAY_PORT &= ~(1 << FERT_PUMP_BIT);
  } else {
    RELAY_PORT |= (1 << FERT_PUMP_BIT);
  }
}

void relay_weeder(uint8_t state) {
  if (state) {
    RELAY_PORT &= ~(1 << WEEDER_MOT_BIT);
  } else {
    RELAY_PORT |= (1 << WEEDER_MOT_BIT);
  }
}
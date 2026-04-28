/*
  relays.h - Native PORTB relay subsystem for Agri3D
  All legacy CNC Spindle/Coolant logic completely removed.
*/

#ifndef relays_h
#define relays_h

#include "grbl-agri3d.h"

// Hardware interfaces
void relays_init();
void relays_stop_all();

// Dedicated Toggle Functions (Active LOW Logic)
void relay_water(uint8_t state);
void relay_fert(uint8_t state);
void relay_weeder(uint8_t state);

#endif
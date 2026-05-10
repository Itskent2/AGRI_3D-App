/*
  print.h - String and Number Formatting for Agri3D Reports

  Standard Arduino Serial.print() is bloated and slow. This module provides
  highly optimized integer-to-ascii conversion routines using native AVR math.
  This allows the Nano to construct diagnostic reports (like TMC2209 DRV_STATUS
  and StallGuard values) into the UART TX buffer incredibly fast without
  dropping motor stepper pulses.
*/

#ifndef print_h
#define print_h

void printString(const char *s);
void printPgmString(const char *s);
void printInteger(long n);
void print_uint32_base10(uint32_t n);
void print_uint8_base10(uint8_t n);
void print_uint8_base2_ndigit(uint8_t n, uint8_t digits);
void printFloat(float n, uint8_t decimal_places);
void printFloat_CoordValue(float n);
void printFloat_RateValue(float n);
void print_uint32_base16_padded(uint32_t n); // 8-digit zero-padded hex (for TMC register dumps)

#endif

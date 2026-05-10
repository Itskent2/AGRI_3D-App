/*
  print.c - String and Number Formatting for Agri3D Reports

  This file handles the high-speed conversion of integers and floats into
  ASCII characters, specifically for tunneling ESP32-bound status reports
  (like MPos or TMC Diagnostics) directly into the hardware UART TX buffer.
*/

#include "grbl-agri3d.h"

void printString(const char *s) {
  while (*s)
    serial_write(*s++);
}

void printPgmString(const char *s) {
  char c;
  while ((c = pgm_read_byte_near(s++)))
    serial_write(c);
}

void print_uint8_base10(uint8_t n) {
  uint8_t digit_a = 0;
  uint8_t digit_b = 0;
  if (n >= 100) {
    digit_a = '0' + n % 10;
    n /= 10;
  }
  if (n >= 10) {
    digit_b = '0' + n % 10;
    n /= 10;
  }
  serial_write('0' + n);
  if (digit_b) {
    serial_write(digit_b);
  }
  if (digit_a) {
    serial_write(digit_a);
  }
}

void print_uint8_base2_ndigit(uint8_t n, uint8_t digits) {
  unsigned char buf[digits];
  uint8_t i = 0;
  for (; i < digits; i++) {
    buf[i] = n % 2;
    n /= 2;
  }
  for (; i > 0; i--)
    serial_write('0' + buf[i - 1]);
}

void print_uint32_base10(uint32_t n) {
  if (n == 0) {
    serial_write('0');
    return;
  }
  unsigned char buf[10];
  uint8_t i = 0;
  while (n > 0) {
    buf[i++] = n % 10;
    n /= 10;
  }
  for (; i > 0; i--)
    serial_write('0' + buf[i - 1]);
}

void printInteger(long n) {
  if (n < 0) {
    serial_write('-');
    print_uint32_base10(-n);
  } else {
    print_uint32_base10(n);
  }
}

void printFloat(float n, uint8_t decimal_places) {
  if (n < 0) {
    serial_write('-');
    n = -n;
  }
  uint8_t decimals = decimal_places;
  while (decimals >= 2) {
    n *= 100;
    decimals -= 2;
  }
  if (decimals) {
    n *= 10;
  }
  n += 0.5;

  unsigned char buf[13];
  uint8_t i = 0;
  uint32_t a = (long)n;
  while (a > 0) {
    buf[i++] = (a % 10) + '0';
    a /= 10;
  }
  while (i < decimal_places) {
    buf[i++] = '0';
  }
  if (i == decimal_places) {
    buf[i++] = '0';
  }
  for (; i > 0; i--) {
    if (i == decimal_places) {
      serial_write('.');
    }
    serial_write(buf[i - 1]);
  }
}

void printFloat_CoordValue(float n) {
  // Agri3D natively operates in MM.
  // Imperial conditional hooks removed.
  printFloat(n, N_DECIMAL_COORDVALUE_MM);
}

void printFloat_RateValue(float n) { printFloat(n, N_DECIMAL_RATEVALUE_MM); }

// Emit a uint32_t as 8 uppercase hex digits with leading zeros.
// Used by $TMC to print raw register values (e.g. "00140C03").
void print_uint32_base16_padded(uint32_t n) {
  static const char hex_chars[] = "0123456789ABCDEF";
  for (int8_t shift = 28; shift >= 0; shift -= 4) {
    serial_write(hex_chars[(n >> shift) & 0x0F]);
  }
}


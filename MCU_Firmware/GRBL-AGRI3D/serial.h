/*
  serial.h - Hardware UART routines for Agri3D/Farmbot ESP32 Bridge

  This module manages the 115200 baud connection between the ATmega328p (Nano)
  and the ESP32-S3 Master. It utilizes hardware interrupts to catch real-time
  commands (like '!' for Feed Hold) instantly, preventing the Nano from
  stuttering or overflowing while receiving dense G-Code streams.
*/

#ifndef serial_h
#define serial_h

#include <stdint.h>

#define RX_BUFFER_SIZE 128
#define TX_BUFFER_SIZE 104

#define SERIAL_NO_DATA 0xff

// Initializes the Hardware UART to 115200 baud for ESP32 comms
void serial_init();

// Pushes a byte into the TX ring buffer for asynchronous transmission
void serial_write(uint8_t data);

// Pulls a byte from the RX ring buffer (data received from ESP32)
uint8_t serial_read();

// Clears the RX buffer completely (typically used on Reset/E-Stop)
void serial_reset_read_buffer();

// Returns how many bytes are currently waiting in the RX buffer
uint8_t serial_get_rx_buffer_available();
uint8_t serial_get_rx_buffer_count();
uint8_t serial_get_tx_buffer_count();

#endif

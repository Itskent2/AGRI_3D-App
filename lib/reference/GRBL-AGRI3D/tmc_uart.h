#ifndef TMC_UART_H
#define TMC_UART_H

#include <avr/io.h>
#include <stdbool.h>
#include <stdint.h>

// Arduino Nano Port B mappings for TMC UART
// TX: D12 -> PB4
// RX: D8  -> PB0 (via 1k resistor)
#define TMC_UART_TX_DDR DDRB
#define TMC_UART_TX_PORT PORTB
#define TMC_UART_TX_BIT 4

#define TMC_UART_RX_DDR DDRB
#define TMC_UART_RX_PIN PINB
#define TMC_UART_RX_PORT PORTB
#define TMC_UART_RX_BIT 0

// Config baudrate (recommend 19200-115200 for TMC2209 autobaud)
#define TMC_BAUD_RATE 19200

// Prototypes
void tmc_uart_init(void);

// Blocking write of a full 64-bit datagram
void tmc_uart_write_reg(uint8_t driver_addr, uint8_t reg, uint32_t val);

// Sends a read request and awaits 64-bit response (with timeout)
// Returns true if read was successful and CRC matches
bool tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg, uint32_t *data_out);

// Asynchronous read that leaves global interrupts enabled.
// If GRBL steps during UART transmission, CRC will fail and return false.
// safely retries without skipping steps!
bool tmc_uart_read_reg_async(uint8_t driver_addr, uint8_t reg,
                             uint32_t *data_out);

// CRC calculator for TMC2209
uint8_t tmc_crc8(uint8_t *data, uint8_t length);

// Raw read: sends request and captures all 8 reply bytes with no CRC filtering.
// Fills buf[8]. Returns number of bytes actually received (< 8 = timeout).
// Used by $TRAW debug command.
uint8_t tmc_uart_read_raw(uint8_t driver_addr, uint8_t reg, uint8_t *buf);

#endif // TMC_UART_H

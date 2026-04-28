#include "tmc_uart.h"

#ifndef USE_A4988_DRIVERS

#include "tmc2209.h"
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>

// Bit timing for 19200 baud on 16MHz AVR is ~52.08 microseconds
// We use 19200 because at 115200 (8.68us), GRBL's 5us step interrupts would
// corrupt bits! At 52.08us, a 5us interrupt only stretches a bit by 10%, which
// is well within UART spec.
#define BIT_DELAY_US 52.08

void tmc_uart_init(void) {
  // Software Pull-Up Hack: Start with TX as an OUTPUT driving HIGH (5V).
  // Because TX is connected via a 1k resistor, this effectively acts as
  // our missing 1k hardware pull-up resistor for the PDN_UART bus!
  TMC_UART_TX_DDR |= (1 << TMC_UART_TX_BIT);
  TMC_UART_TX_PORT |= (1 << TMC_UART_TX_BIT);

  TMC_UART_RX_DDR &= ~(1 << TMC_UART_RX_BIT);
  TMC_UART_RX_PORT |= (1 << TMC_UART_RX_BIT); // Internal pull-up on RX
}

// Low-level bit-bang transmit
static void tmc_uart_tx_byte(uint8_t data) {
  // Take control of the bus (Set TX as output)
  TMC_UART_TX_DDR |= (1 << TMC_UART_TX_BIT);

  // Start bit (LOW)
  TMC_UART_TX_PORT &= ~(1 << TMC_UART_TX_BIT);
  _delay_us(BIT_DELAY_US);

  // Data bits (LSB first)
  for (uint8_t i = 0; i < 8; i++) {
    if (data & (1 << i)) {
      TMC_UART_TX_PORT |= (1 << TMC_UART_TX_BIT);
    } else {
      TMC_UART_TX_PORT &= ~(1 << TMC_UART_TX_BIT);
    }
    _delay_us(BIT_DELAY_US);
  }

  // Stop bit (HIGH)
  TMC_UART_TX_PORT |= (1 << TMC_UART_TX_BIT);
  _delay_us(BIT_DELAY_US);

  // DO NOT RELEASE THE BUS! Leave TX as an output driving HIGH (5V).
  // When the TMC2209 replies, it will sink current through the 1k isolation resistor.
  // TMC_UART_TX_DDR &= ~(1 << TMC_UART_TX_BIT);
}

// Low-level bit-bang receive with simple timeout
static bool tmc_uart_rx_byte(uint8_t *data_out) {
  uint8_t data = 0;

  // Wait for start bit (LOW)
  // 5000us timeout to avoid freezing GRBL entirely if driver is unresponsive
  uint16_t timeout = 5000;
  while (TMC_UART_RX_PIN & (1 << TMC_UART_RX_BIT)) {
    if (--timeout == 0)
      return false;
    _delay_us(1);
  }

  // Delay 1.5 bit lengths to sample in the middle of the first data bit
  _delay_us(BIT_DELAY_US * 1.5);

  // Data bits (LSB first)
  for (uint8_t i = 0; i < 8; i++) {
    if (TMC_UART_RX_PIN & (1 << TMC_UART_RX_BIT)) {
      data |= (1 << i);
    }
    _delay_us(BIT_DELAY_US);
  }

  *data_out = data;
  return true;
}

uint8_t tmc_crc8(uint8_t *data, uint8_t length) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < length; i++) {
    uint8_t current_byte = data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if ((crc >> 7) ^ (current_byte & 0x01)) {
        crc = (crc << 1) ^ 0x07;
      } else {
        crc = (crc << 1);
      }
      current_byte >>= 1;
    }
  }
  return crc;
}

void tmc_uart_write_reg(uint8_t driver_addr, uint8_t reg, uint32_t val) {
  uint8_t datagram[8];
  datagram[0] = 0x05; // SYNC
  datagram[1] = driver_addr;
  datagram[2] = reg | 0x80; // Write MSB is 1
  datagram[3] = (val >> 24) & 0xFF;
  datagram[4] = (val >> 16) & 0xFF;
  datagram[5] = (val >> 8) & 0xFF;
  datagram[6] = val & 0xFF;
  datagram[7] = tmc_crc8(datagram, 7);

  // Disable interrupts to ensure strict UART timing sequence
  // This blocks for (~0.7ms), which is acceptable during initialization.
  // If called during motion, consider a retry mechanism instead of disabling
  // interrupts to avoid jitter.
  uint8_t sreg = SREG;
  cli();
  for (uint8_t i = 0; i < 8; i++) {
    tmc_uart_tx_byte(datagram[i]);
  }
  SREG = sreg; // Restore interrupts
  // Trinamic requires at least 8 bit-times of idle HIGH state
  //  between datagrams. This 2ms delay guarantees the drivers stay in sync.
  _delay_ms(2);
}

bool tmc_uart_read_reg(uint8_t driver_addr, uint8_t reg, uint32_t *data_out) {
  _delay_ms(1);
  // TMC2209 requires >= 8 bit times (~416us at 19200) inter-node delay

  uint8_t req[4];
  req[0] = 0x05; // SYNC
  req[1] = driver_addr;
  req[2] = reg & 0x7F; // Write MSB is 0
  req[3] = tmc_crc8(req, 3);

  uint8_t sreg = SREG;
  cli();
  for (uint8_t i = 0; i < 4; i++) {
    tmc_uart_tx_byte(req[i]);
  }
  SREG = sreg;

  uint8_t res[8];
  bool success = true;
  for (uint8_t i = 0; i < 8; i++) {
    if (!tmc_uart_rx_byte(&res[i])) {
      success = false;
      break;
    }
  }

  if (!success)
    return false;

  if (res[7] != tmc_crc8(res, 7)) {
    return false; // CRC mismatch
  }

  // STRICT VALIDATION: Reject phantom all-zero frames caused by ISR jitter
  if (res[0] != 0x05 || res[1] != 0xFF || res[2] != (reg & 0x7F))
    return false;

  *data_out = ((uint32_t)res[3] << 24) | ((uint32_t)res[4] << 16) |
              ((uint32_t)res[5] << 8) | res[6];
  return true;
}

bool tmc_uart_read_reg_async(uint8_t driver_addr, uint8_t reg,
                             uint32_t *data_out) {
  // Blocking delay removed. The caller (tmc_config_poll_state) must ensure
  // adequate time has passed between UART requests via the TCNT2 state machine.

  uint8_t req[4];
  req[0] = 0x05; // SYNC
  req[1] = driver_addr;
  req[2] = reg & 0x7F; // Write MSB is 0
  req[3] = tmc_crc8(req, 3);

  // TX WITHOUT cli() to allow TIMER1 GRBL interrupts to fire during idle
  // polling
  for (uint8_t i = 0; i < 4; i++) {
    tmc_uart_tx_byte(req[i]);
  }

  uint8_t res[8];
  bool success = true;
  for (uint8_t i = 0; i < 8; i++) {
    // RX WITHOUT cli()
    if (!tmc_uart_rx_byte(&res[i])) {
      success = false;
      break;
    }
  }

  if (!success)
    return false;

  // STRICT VALIDATION: Reject phantom all-zero frames caused by ISR jitter
  if (res[0] != 0x05 || res[1] != 0xFF || res[2] != (reg & 0x7F))
    return false;

  if (res[7] != tmc_crc8(res, 7))
    return false;

  *data_out = ((uint32_t)res[3] << 24) | ((uint32_t)res[4] << 16) |
              ((uint32_t)res[5] << 8) | res[6];
  return true;
}

// ---------------------------------------------------------------------------
// Raw UART read — for debugging CRC failures.
// Sends a read request to the driver then captures exactly 8 bytes with NO
// CRC check. Returns how many bytes were received before timeout (8 = full
// reply, < 8 = driver did not respond in time).
// The caller can print all bytes and compute CRC manually.
// ---------------------------------------------------------------------------
uint8_t tmc_uart_read_raw(uint8_t driver_addr, uint8_t reg, uint8_t *buf) {
  uint8_t req[4];
  req[0] = 0x05; // SYNC
  req[1] = driver_addr;
  req[2] = reg & 0x7F; // Read bit clear
  req[3] = tmc_crc8(req, 3);

  uint8_t sreg = SREG;
  cli();
  for (uint8_t i = 0; i < 4; i++) {
    tmc_uart_tx_byte(req[i]);
  }

  uint8_t received = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (!tmc_uart_rx_byte(&buf[i]))
      break;
    received++;
  }
  SREG = sreg;
  return received;
}

#endif // !USE_A4988_DRIVERS

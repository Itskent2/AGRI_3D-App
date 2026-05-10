/*
  serial.c - Hardware UART routines for Agri3D/Farmbot ESP32 Bridge

  This file implements ring buffers and hardware interrupts (ISR) for the
  ATmega328p's UART module. By relying on interrupts rather than blocking
  code, the Nano can seamlessly stream G-Code from the ESP32-S3 while also
  generating high-speed Step/Dir pulses for the TMC2209 drivers.
*/

#include "grbl-agri3d.h"

#define RX_RING_BUFFER (RX_BUFFER_SIZE + 1)
#define TX_RING_BUFFER (TX_BUFFER_SIZE + 1)

uint8_t serial_rx_buffer[RX_RING_BUFFER];
uint8_t serial_rx_buffer_head = 0;
volatile uint8_t serial_rx_buffer_tail = 0;

uint8_t serial_tx_buffer[TX_RING_BUFFER];
uint8_t serial_tx_buffer_head = 0;
volatile uint8_t serial_tx_buffer_tail = 0;

uint8_t serial_get_rx_buffer_available() {
  uint8_t rtail = serial_rx_buffer_tail;
  if (serial_rx_buffer_head >= rtail) {
    return (RX_BUFFER_SIZE - (serial_rx_buffer_head - rtail));
  }
  return ((rtail - serial_rx_buffer_head - 1));
}

uint8_t serial_get_rx_buffer_count() {
  uint8_t rtail = serial_rx_buffer_tail;
  if (serial_rx_buffer_head >= rtail) {
    return (serial_rx_buffer_head - rtail);
  }
  return (RX_BUFFER_SIZE - (rtail - serial_rx_buffer_head));
}

uint8_t serial_get_tx_buffer_count() {
  uint8_t ttail = serial_tx_buffer_tail;
  if (serial_tx_buffer_head >= ttail) {
    return (serial_tx_buffer_head - ttail);
  }
  return (TX_RING_BUFFER - (ttail - serial_tx_buffer_head));
}

void serial_init() {
  // Hardcoded for 115200 Baud for ESP32-S3 Bridge (Agri3D spec)
  // Assumes F_CPU is 16000000L
  uint16_t UBRR0_value = ((F_CPU / (4L * 115200)) - 1) / 2;
  UCSR0A |= (1 << U2X0); // Baud doubler on for high speeds

  UBRR0H = UBRR0_value >> 8;
  UBRR0L = UBRR0_value;

  // enable rx, tx, and interrupt on complete reception of a byte
  UCSR0B |= (1 << RXEN0 | 1 << TXEN0 | 1 << RXCIE0);
}

void serial_write(uint8_t data) {
  uint8_t next_head = serial_tx_buffer_head + 1;
  if (next_head == TX_RING_BUFFER) {
    next_head = 0;
  }

  // Wait until there is space in the buffer
  while (next_head == serial_tx_buffer_tail) {
    if (sys_rt_exec_state & EXEC_RESET) {
      return;
    }
  }

  serial_tx_buffer[serial_tx_buffer_head] = data;
  serial_tx_buffer_head = next_head;

  // Enable Data Register Empty Interrupt to start tx-streaming
  UCSR0B |= (1 << UDRIE0);
}

ISR(SERIAL_UDRE) {
  uint8_t tail = serial_tx_buffer_tail;
  UDR0 = serial_tx_buffer[tail];
  tail++;
  if (tail == TX_RING_BUFFER) {
    tail = 0;
  }
  serial_tx_buffer_tail = tail;

  if (tail == serial_tx_buffer_head) {
    UCSR0B &= ~(1 << UDRIE0);
  }
}

uint8_t serial_read() {
  uint8_t tail = serial_rx_buffer_tail;
  if (serial_rx_buffer_head == tail) {
    return SERIAL_NO_DATA;
  } else {
    uint8_t data = serial_rx_buffer[tail];
    tail++;
    if (tail == RX_RING_BUFFER) {
      tail = 0;
    }
    serial_rx_buffer_tail = tail;
    return data;
  }
}

ISR(SERIAL_RX) {
  uint8_t data = UDR0;
  uint8_t next_head;

  // Real-time Agri3D Command Interception from ESP32
  switch (data) {
  case CMD_RESET:
    mc_reset();
    break;
  case CMD_STATUS_REPORT:
    system_set_exec_state_flag(EXEC_STATUS_REPORT);
    break;
  case CMD_CYCLE_START:
    system_set_exec_state_flag(EXEC_CYCLE_START);
    break;
  case CMD_FEED_HOLD:
    system_set_exec_state_flag(EXEC_FEED_HOLD);
    break;
  default:
    if (data > 0x7F) {
      switch (data) {
      case CMD_SAFETY_DOOR:
        system_set_exec_state_flag(EXEC_SAFETY_DOOR);
        break;
      case CMD_JOG_CANCEL:
        if (sys.state & STATE_JOG) {
          system_set_exec_state_flag(EXEC_MOTION_CANCEL);
        }
        break;
      case CMD_FEED_OVR_RESET:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_RESET);
        break;
      case CMD_FEED_OVR_COARSE_PLUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_PLUS);
        break;
      case CMD_FEED_OVR_COARSE_MINUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_MINUS);
        break;
      case CMD_RAPID_OVR_RESET:
        system_set_exec_motion_override_flag(EXEC_RAPID_OVR_RESET);
        break;
      }
    } else {
      next_head = serial_rx_buffer_head + 1;
      if (next_head == RX_RING_BUFFER) {
        next_head = 0;
      }
      if (next_head != serial_rx_buffer_tail) {
        serial_rx_buffer[serial_rx_buffer_head] = data;
        serial_rx_buffer_head = next_head;
      }
    }
  }
}

void serial_reset_read_buffer() {
  serial_rx_buffer_tail = serial_rx_buffer_head;
}

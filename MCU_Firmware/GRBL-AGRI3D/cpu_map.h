/*
  cpu_map.h - CPU and pin mapping for GRBL-AGRI3D (Arduino Nano)
  Customized for N16R8 Optimized Farmbot Ver3 with 4x TMC2209.
*/

#ifndef cpu_map_h
#define cpu_map_h

// ---------------------------------------------------------------------
// Serial port pins
// ---------------------------------------------------------------------
#define SERIAL_RX USART_RX_vect
#define SERIAL_UDRE USART_UDRE_vect

// ---------------------------------------------------------------------
// PORT D: High-Speed Stepper Controls (Optimized for No-Via Routing)
// ---------------------------------------------------------------------
#define STEP_PORT PORTD
#define STEP_DDR DDRD
#define DIRECTION_PORT PORTD
#define DIRECTION_DDR DDRD

// X-Axis Pair (PD2 & PD3)
#define X_STEP_BIT 2
#define X_DIRECTION_BIT 3

// Y-Axis Pair (PD4 & PD5)
#define Y_STEP_BIT 5
#define Y_DIRECTION_BIT 4

// Z-Axis Pair (PD6 & PD7)
#define Z_STEP_BIT 7
#define Z_DIRECTION_BIT 6

// Masks for batch updates
#define STEP_MASK ((1 << X_STEP_BIT) | (1 << Y_STEP_BIT) | (1 << Z_STEP_BIT))
#define DIRECTION_MASK                                                         \
  ((1 << X_DIRECTION_BIT) | (1 << Y_DIRECTION_BIT) | (1 << Z_DIRECTION_BIT))

// ---------------------------------------------------------------------
// PORT C: Logic, Limits & Feedback
// ---------------------------------------------------------------------

#define LIMIT_PORT PORTC
#define LIMIT_PIN PINC
#define LIMIT_DDR DDRC
#define X_LIMIT_BIT 1 // PC2 (A1) - X-DIAG StallGuard
#define Y_LIMIT_BIT 2 // PC1 (A2) - Y-DIAG StallGuard
#define Z_LIMIT_BIT 3 // PC3 (A3) - Z-LIMIT Physical Endstop
#define LIMIT_MASK                                                             \
  ((1 << X_LIMIT_BIT) | (1 << Y_LIMIT_BIT) | (1 << Z_LIMIT_BIT))
// We use PCINT1 for PORT C pins (Nano)
#define LIMIT_PCMSK PCMSK1
#define LIMIT_INT PCIE1 // Pin change interrupt enable pin
#define LIMIT_INT_vect PCINT1_vect

// ---------------------------------------------------------------------
// PORT B: Actuators & TMC UART
// ---------------------------------------------------------------------
#define RELAY_PORT PORTB
#define RELAY_DDR DDRB
#define WATER_PUMP_BIT 1 // PB1 (D9)
#define FERT_PUMP_BIT 2  // PB2 (D10)
#define WEEDER_MOT_BIT 3 // PB3 (D11)
#define RELAY_MASK                                                             \
  ((1 << WATER_PUMP_BIT) | (1 << FERT_PUMP_BIT) | (1 << WEEDER_MOT_BIT))

// Bit-Bang UART Pins for TMC2209
#define TMC_UART_PORT PORTB
#define TMC_UART_PIN PINB
#define TMC_UART_DDR DDRB
#define TMC_UART_TX_BIT 4 // PB4 (D12) -> To Drivers PDN_UART
#define TMC_UART_RX_BIT 0 // PB0 (D8) -> Via 1k Resistor
#define TMC_UART_MASK ((1 << TMC_UART_TX_BIT) | (1 << TMC_UART_RX_BIT))

#endif

#ifndef TMC2209_H
#define TMC2209_H

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// TMC2209 Register Map
// -----------------------------------------------------------------------------
#define TMC2209_REG_GCONF 0x00
#define TMC2209_REG_GSTAT 0x01
#define TMC2209_REG_IFCNT 0x02
#define TMC2209_REG_NODECONF 0x03
#define TMC2209_REG_OTP_PROG 0x04
#define TMC2209_REG_OTP_READ 0x05
#define TMC2209_REG_IOIN 0x06
#define TMC2209_REG_FACTORY 0x07

#define TMC2209_REG_IHOLD_IRUN 0x10
#define TMC2209_REG_TPOWERDOWN 0x11
#define TMC2209_REG_TSTEP 0x12
#define TMC2209_REG_TPWMTHRS 0x13
#define TMC2209_REG_VACTUAL 0x22

#define TMC2209_REG_TCOOLTHRS 0x14
#define TMC2209_REG_SGTHRS 0x40
#define TMC2209_REG_SG_RESULT 0x41
#define TMC2209_REG_COOLCONF 0x42

#define TMC2209_REG_MSCNT 0x6A
#define TMC2209_REG_MSCURACT 0x6B
#define TMC2209_REG_CHOPCONF 0x6C
#define TMC2209_REG_DRV_STATUS 0x6F
#define TMC2209_REG_PWMCONF 0x70
#define TMC2209_REG_PWMSCALE 0x71
#define TMC2209_REG_PWM_AUTO 0x72

// -----------------------------------------------------------------------------
// Addressing constants
// -----------------------------------------------------------------------------
#define TMC_ADDR_X 1  // MS1=VCC, MS2=GND
#define TMC_ADDR_Y1 3 // MS1=VCC, MS2=VCC
#define TMC_ADDR_Y2 2 // MS1=GND, MS2=VCC
#define TMC_ADDR_Z 0  // MS1=GND, MS2=GND

#endif // TMC2209_H

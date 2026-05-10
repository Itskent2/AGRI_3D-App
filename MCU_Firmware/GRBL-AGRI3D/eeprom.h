/*
  eeprom.h - EEPROM memory manipulation subsystem
  Optimized for Farmbot Ver3 (Agri3D) architecture.
  Safely stores and retrieves max travel limits, coordinate offsets,
  and stepping configurations from the Nano's non-volatile memory.
*/

#ifndef eeprom_h
#define eeprom_h

unsigned char eeprom_get_char(unsigned int addr);
void eeprom_put_char(unsigned int addr, unsigned char new_value);
void memcpy_to_eeprom_with_checksum(unsigned int destination, char *source,
                                    unsigned int size);
int memcpy_from_eeprom_with_checksum(char *destination, unsigned int source,
                                     unsigned int size);

#endif

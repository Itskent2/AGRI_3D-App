/**
 * @file agri3d_sensors.h
 * @brief Drivers for secondary sensors: NPK (Modbus), Rain (Digital/Analog), and Soil.
 */

#pragma once
#include <Arduino.h>

struct NpkData {
    uint16_t nitrogen;  // mg/kg
    uint16_t phosphorus;
    uint16_t potassium;
    bool valid;
};

void sensorsInit();

/** @return true if the rain sensor detects precipitation. */
bool isRaining();

/** @brief Poll the RS485 NPK sensor. */
NpkData readNpkSensor();

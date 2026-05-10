#include "agri3d_sensors.h"
#include "agri3d_config.h"
#include "agri3d_npk.h"
#include "../core/agri3d_logger.h"

// Pin definition (adjust in agri3d_config.h later)
#ifndef PIN_RAIN_SENSOR
#define PIN_RAIN_SENSOR 4 // Example GPIO
#endif

void sensorsInit() {
#if HW_RAIN_CONNECTED
    pinMode(RAIN_PIN, INPUT_PULLUP);
    AgriLog(TAG_SENSORS, LEVEL_INFO, "Rain sensor initialized.");
#endif
    
    npkInit();
    AgriLog(TAG_SENSORS, LEVEL_INFO, "NPK sensors initialized.");
}

bool isRaining() {
#if HW_RAIN_CONNECTED
    // Standard rain sensors are Active LOW (0 = Rain, 1 = Dry)
    return digitalRead(RAIN_PIN) == LOW;
#else
    return false; // Always dry if hardware is not connected
#endif
}

NpkData readNpkSensor() {
    NpkData data;
    // TODO: Implement Modbus RS485 communication here
    data.nitrogen = 0;
    data.phosphorus = 0;
    data.potassium = 0;
    data.valid = false;
    return data;
}

/**
 * @file ErrorCodes.h
 * @brief Standardized error codes for the Agri3D system.
 */

#pragma once

namespace Agri3D {

enum class ErrorCode {
    SUCCESS = 0,
    
    // Communication Errors (100-199)
    WIFI_FAILED = 100,
    WS_DISCONNECTED = 101,
    NANO_TIMEOUT = 110,
    
    // Hardware Errors (200-299)
    CAM_INIT_FAILED = 200,
    CAM_CAPTURE_FAILED = 201,
    SD_NOT_FOUND = 210,
    NPK_SENSOR_TIMEOUT = 220,
    
    // Motion/GRBL Errors (300-399)
    GRBL_ALARM = 300,
    MOVE_TIMEOUT = 301,
    
    // Logic/Operation Errors (400-499)
    OPERATION_BUSY = 400,
    WEATHER_GATED = 410,
    RAIN_SENSOR_TRIPPED = 411
};

} // namespace Agri3D

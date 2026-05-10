#include "SystemEnums.h"
#include <Arduino.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Centralized logger for AGRI-3D.
 * Supports granular tags and log levels for Flutter filtering.
 */

extern SemaphoreHandle_t logMutex;

/**
 * @brief Initialize the logging mutex.
 */
void loggerInit();

/**
 * @brief Send a log string to all connected clients.
 */
extern void broadcastLog(const char* log);

/**
 * @brief Main logging function. Thread-safe via logMutex.
 */
inline void AgriLog(LogTag tag, LogLevel level, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Helpers for tags and levels (implemented in cpp or header)
    auto getTagStr = [](LogTag tag) -> const char* {
        switch (tag) {
            case TAG_SYSTEM:  return "[SYSTEM]";
            case TAG_NET:     return "[NET]";
            case TAG_GRBL:    return "[GRBL]";
            case TAG_CAM:     return "[CAM]";
            case TAG_AI:      return "[AI]";
            case TAG_ROUTINE: return "[ROUTINE]";
            case TAG_SCAN:    return "[SCAN]";
            case TAG_WEED:    return "[WEED]";
            case TAG_ENV:     return "[ENV]";
            case TAG_FERT:    return "[FERT]";
            case TAG_SD:      return "[SD]";
            case TAG_SENSORS: return "[SENSORS]";
            case TAG_CMD:     return "[CMD]";
            case TAG_STATE:   return "[STATE]";
            default:          return "[UNK]";
        }
    };

    auto getLevelStr = [](LogLevel level) -> const char* {
        switch (level) {
            case LEVEL_INFO:    return "[INFO]";
            case LEVEL_WARN:    return "[WARN]";
            case LEVEL_ERR:     return "[ERR]";
            case LEVEL_SUCCESS: return "[OK]";
            default:            return "";
        }
    };

    char logStr[300];
    snprintf(logStr, sizeof(logStr), "%s%s %s", getTagStr(tag), getLevelStr(level), buffer);

    if (logMutex != NULL) {
        if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Serial.println(logStr);
            xSemaphoreGive(logMutex);
        } else {
            // Fallback if mutex is stuck, at least we try to log
            Serial.print("[LOCK_FAIL] ");
            Serial.println(logStr);
        }
    } else {
        // Mutex not init yet (early boot)
        Serial.println(logStr);
    }
    // Sanitize string to prevent WebSocket 1007 (Invalid UTF-8) errors on Flutter
    for (int i = 0; logStr[i] != '\0'; i++) {
        // Allow standard printable ASCII, tab, newline, and carriage return.
        // Replace everything else (including extended ASCII/binary noise) with '?'
        if ((unsigned char)logStr[i] < 32 && logStr[i] != '\n' && logStr[i] != '\r' && logStr[i] != '\t') {
            logStr[i] = '?';
        } else if ((unsigned char)logStr[i] > 126) {
            logStr[i] = '?';
        }
    }

    broadcastLog(logStr);
}

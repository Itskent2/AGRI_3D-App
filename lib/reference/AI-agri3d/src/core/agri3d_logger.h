#include "SystemEnums.h"
#include <Arduino.h>
#include <stdarg.h>

/**
 * @brief Centralized logger for AGRI-3D.
 * Supports granular tags and log levels for Flutter filtering.
 */

static const char* getTagStr(LogTag tag) {
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
}

static const char* getLevelStr(LogLevel level) {
    switch (level) {
        case LEVEL_INFO:    return "[INFO]";
        case LEVEL_WARN:    return "[WARN]";
        case LEVEL_ERR:     return "[ERR]";
        case LEVEL_SUCCESS: return "[OK]";
        default:            return "";
    }
}

inline void AgriLog(LogTag tag, LogLevel level, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    Serial.printf("%s%s %s\n", getTagStr(tag), getLevelStr(level), buffer);
}

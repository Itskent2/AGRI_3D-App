/**
 * @file SystemEnums.h
 * @brief Global enums for the Agri3D system.
 */

#pragma once
#include <stdint.h>

/** WiFi connectivity layer. Updated by WiFi event callbacks. */
enum WifiState : uint8_t {
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED
};

/** Flutter app WebSocket connection. Updated by WS_EVT_CONNECT/DISCONNECT. */
enum FlutterState : uint8_t {
    FLUTTER_DISCONNECTED,
    FLUTTER_CONNECTED
};

/** Nano (GRBL) serial link health. */
enum NanoState : uint8_t {
    NANO_UNKNOWN,
    NANO_CONNECTED,
    NANO_UNRESPONSIVE
};

/** GRBL machine state. */
enum GrblState : uint8_t {
    GRBL_UNKNOWN,
    GRBL_IDLE,
    GRBL_RUN,
    GRBL_JOG,
    GRBL_HOME,
    GRBL_HOLD,
    GRBL_ALARM,
    GRBL_CHECK,
    GRBL_DOOR
};

/** High-level mission / operation state. */
enum OperationState : uint8_t {
    OP_IDLE,
    OP_HOMING,
    OP_SD_RUNNING,
    OP_FERTILIZING,
    OP_SCANNING,      ///< Phase 1: gantry moving + saving frames to SD (no binary WS traffic)
    OP_UPLOADING,     ///< Phase 2: reading frames from SD and sending to Flutter
    OP_AI_WEEDING,
    OP_NPK_DIP,       ///< Moving Z to dip sensor and reading NPK
    OP_AUTONOMOUS,    ///< Master autonomous farming routine
    OP_RAIN_PAUSED,
    OP_ALARM_RECOVERY
};

/** Physical and API environment conditions. */
enum EnvironmentState : uint8_t {
    ENV_CLEAR,
    ENV_RAIN_SENSOR,
    ENV_WEATHER_GATED,
    ENV_RAIN_AND_WEATHER
};

// ── Log Tags (for filterable console output) ──────────────────────────────
enum LogTag : uint8_t {
    TAG_SYSTEM,
    TAG_NET,
    TAG_GRBL,
    TAG_CAM,
    TAG_AI,
    TAG_ROUTINE,
    TAG_SCAN,
    TAG_WEED,
    TAG_ENV,
    TAG_FERT,
    TAG_SD,
    TAG_SENSORS,
    TAG_CMD,
    TAG_STATE
};

enum LogLevel : uint8_t {
    LEVEL_INFO,
    LEVEL_WARN,
    LEVEL_ERR,
    LEVEL_SUCCESS
};

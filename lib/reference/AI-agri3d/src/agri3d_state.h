/**
 * @file agri3d_state.h
 * @brief System-wide state machine: all enums, the SystemStatus struct,
 *        transition helpers, and the broadcast function declaration.
 *
 * Every other module reads from `sysState` and calls `setXxx()` to update it.
 * Direct assignment to `sysState` fields is discouraged outside this module.
 */

#pragma once
#include <Arduino.h>
#include <WebSocketsServer.h>
#include "agri3d_config.h"

// ── Forward declaration ────────────────────────────────────────────────────
extern WebSocketsServer webSocket;

// ============================================================================
// STATE ENUMS
// ============================================================================

/** WiFi connectivity layer. Updated by WiFi event callbacks. */
enum WifiState : uint8_t {
    WIFI_DISCONNECTED,  ///< No network connection
    WIFI_CONNECTING,    ///< Boot-phase: scanning / associating
    WIFI_CONNECTED      ///< IP obtained, WebSocket server live
};

/** Flutter app WebSocket connection. Updated by WS_EVT_CONNECT/DISCONNECT. */
enum FlutterState : uint8_t {
    FLUTTER_DISCONNECTED, ///< No app client connected
    FLUTTER_CONNECTED     ///< At least one WebSocket client active
};

/**
 * Nano (GRBL) serial link health.
 * Updated by the watchdog in agri3d_grbl.cpp — NOT by Flutter commands.
 */
enum NanoState : uint8_t {
    NANO_UNKNOWN,       ///< Boot state — no reply received yet
    NANO_CONNECTED,     ///< Receiving valid GRBL responses
    NANO_UNRESPONSIVE   ///< Was connected but silent beyond watchdog window
};

/**
 * GRBL machine state, parsed directly from '<State|...>' status strings.
 * Mirrors the GRBL state machine 1-to-1.
 */
enum GrblState : uint8_t {
    GRBL_UNKNOWN,  ///< No status string received yet
    GRBL_IDLE,     ///< Ready — not moving
    GRBL_RUN,      ///< Executing a G-code move
    GRBL_JOG,      ///< Jogging (manual move) in progress
    GRBL_HOME,     ///< Homing cycle active
    GRBL_HOLD,     ///< Feed hold (paused mid-job)
    GRBL_ALARM,    ///< Alarm — all motion locked until $X
    GRBL_CHECK,    ///< G-code check mode (dry run)
    GRBL_DOOR      ///< Safety door open
};

/**
 * High-level mission / operation state.
 * Camera is LOCKED during OP_SCANNING and OP_AI_WEEDING.
 * isStreaming flag is independent and can be true alongside most states.
 */
enum OperationState : uint8_t {
    // Camera available ───────────────────────────────────────────────────
    OP_IDLE,          ///< Nothing running — all resources free
    OP_HOMING,        ///< $HX / $HY homing sequence in progress
    OP_SD_RUNNING,    ///< Executing G-code file from SD card
    OP_FERTILIZING,   ///< Fertiliser pump cycle active

    // Camera LOCKED ──────────────────────────────────────────────────────
    OP_SCANNING,      ///< SCAN_PLANT grid capture — camera locked
    OP_AI_WEEDING,    ///< AI weed detection + targeting — camera locked

    // Suspended / Error ──────────────────────────────────────────────────
    OP_RAIN_PAUSED,      ///< Halted: rain sensor or weather API gate
    OP_ALARM_RECOVERY    ///< GRBL ALARM fired — awaiting $X unlock
};

/** Physical and API environment conditions. Updated by agri3d_environment. */
enum EnvironmentState : uint8_t {
    ENV_CLEAR,            ///< No weather interruption
    ENV_RAIN_SENSOR,      ///< Physical rain sensor triggered
    ENV_WEATHER_GATED,    ///< Open-Meteo API reports rain/storm (code ≥51)
    ENV_RAIN_AND_WEATHER  ///< Both sensor and API triggered simultaneously
};

// ============================================================================
// SYSTEM STATUS STRUCT
// ============================================================================

/**
 * @brief Single global object holding every dimension of system state.
 *
 * Read freely from any module. Mutate ONLY via the setXxx() helpers below
 * so that broadcastSystemState() fires automatically on every change.
 */
struct SystemStatus {
    WifiState        wifi        = WIFI_CONNECTING;
    FlutterState     flutter     = FLUTTER_DISCONNECTED;
    NanoState        nano        = NANO_UNKNOWN;
    GrblState        grbl        = GRBL_UNKNOWN;
    OperationState   operation   = OP_IDLE;
    EnvironmentState environment = ENV_CLEAR;

    /**
     * Live camera stream flag.
     * Deliberately NOT part of OperationState so it can overlap with
     * OP_HOMING, OP_SD_RUNNING, OP_FERTILIZING, etc.
     */
    bool isStreaming = false;

    // Gantry position (mm) — updated by GRBL status parser
    float grblX = 0.0f;
    float grblY = 0.0f;
    float grblZ = 0.0f;

    // Stream rate (Frames Per Minute) — adjusted by SET_FPM command
    int fpm = STREAM_FPM_DEFAULT;

    // ── Safe Frame Handoff ──────────────────────────────────────────────────
    // Core 0 (camera) puts a frame here; Core 1 (network) sends it.
    // This ensures webSocket calls stay on a single core for thread safety.
    uint8_t* pendingFrame = nullptr;
    size_t   pendingFrameLen = 0;
    int8_t   pendingFrameClient = -1;
};

extern SystemStatus sysState;

// ============================================================================
// TRANSITION HELPERS — always call these, never assign sysState.x directly
// ============================================================================

void setWifi(WifiState s);
void setFlutter(FlutterState s);
void setNano(NanoState s);
void setGrbl(GrblState s);
void setOperation(OperationState s);
void setEnvironment(EnvironmentState s);
void setStreaming(bool active);
void setFpm(int fpm);
void setPosition(float x, float y, float z);

/** Returns true when the camera hardware is free to use. */
bool isCameraAvailable();

/**
 * Serialises the full sysState to JSON and broadcasts to all WebSocket clients.
 * Called automatically by every setXxx() function — do not call manually
 * unless you need a forced re-send (e.g. on new client connect).
 */
void broadcastSystemState();

// ── String converters (used by broadcastSystemState) ──────────────────────
const char* wifiStateStr(WifiState s);
const char* flutterStateStr(FlutterState s);
const char* nanoStateStr(NanoState s);
const char* grblStateStr(GrblState s);
const char* operationStateStr(OperationState s);
const char* environmentStateStr(EnvironmentState s);

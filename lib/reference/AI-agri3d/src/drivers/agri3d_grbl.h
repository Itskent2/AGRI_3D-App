/**
 * @file agri3d_grbl.h
 * @brief GRBL/Nano serial bridge: non-blocking reader, adaptive status poll,
 *        Nano watchdog, GRBL status parser, machine dimension cache (NVS),
 *        and crash log parser.
 *
 * -- Machine Dimension Cache --
 * When the Nano reports $130/$131/$132 (after homing/auto-dim), the ESP32
 * stores those values in its own NVS (Preferences) so they survive reboots
 * and power cuts. On boot, the cached values are loaded immediately so the
 * ESP32 always knows the workspace size without waiting for a $$ query.
 *
 * -- Crash Log --
 * The Nano stores a 2-byte crash log in its EEPROM and replays it at boot
 * as "[PREVIOUS_CRASH:X:5,Y1:3]". The ESP32 captures this, stores it in
 * its own NVS, and broadcasts it to Flutter. The log is cleared by the Nano
 * only after a successful homing cycle.
 *
 * -- TMC Driver Telemetry --
 * The Nano includes "|TMC:0,0,0,0" in every status packet. The ESP32 parses
 * and stores this for Flutter's diagnostics panel.
 */

#pragma once
#include <Arduino.h>

extern HardwareSerial NanoSerial;

// ── TMC driver state codes (mirrors Nano's tmc_state[]) ──────────────────
enum TmcDriverState : uint8_t {
    TMC_OK          = 0,  ///< Driver responding normally
    TMC_OVERTEMP    = 1,  ///< Over-temperature warning or shutdown
    TMC_DISCONNECTED = 2  ///< No UART response (driver missing or wiring fault)
};

/** Cached TMC driver states for all 4 axes [X, Y1, Y2, Z]. */
struct TmcStatus {
    TmcDriverState x  = TMC_OK;
    TmcDriverState y1 = TMC_OK;
    TmcDriverState y2 = TMC_OK;
    TmcDriverState z  = TMC_OK;
};
extern TmcStatus tmcStatus;

// ── Machine dimension cache ────────────────────────────────────────────────
/**
 * NVS-backed machine workspace dimensions (mm).
 * Loaded from NVS on boot. Updated whenever the Nano reports $130/$131/$132.
 * Cleared and re-measured by homing.
 */
struct MachineDimensions {
    float maxX = 0.0f;  ///< 0 = not yet measured
    float maxY = 0.0f;
    float maxZ = 0.0f;
    bool  valid = false; ///< true once at least one $130/$131 has been received
};
extern MachineDimensions machineDim;

// ── Crash record ───────────────────────────────────────────────────────────
/**
 * Last crash reported by the Nano on boot via [PREVIOUS_CRASH:...].
 * Stored in ESP32 NVS so it's preserved across ESP32 reboots too.
 *
 * TMC state codes in the crash:
 *   0 = OK, 1 = OverTemp, 2 = Disconnected, 3-15 = reserved
 */
struct CrashRecord {
    bool  hasRecord = false;
    char  raw[64]   = {0};  ///< Full raw crash string, e.g. "X:5,Y1:3"
    uint8_t tmcX    = 0;
    uint8_t tmcY1   = 0;
    uint8_t tmcY2   = 0;
    uint8_t tmcZ    = 0;
};
extern CrashRecord lastCrash;

// ── ALARM code lookup ──────────────────────────────────────────────────────
/** Returns a human-readable description for a GRBL alarm code (1-9). */
const char* alarmCodeDescription(uint8_t code);

// ── Public API ─────────────────────────────────────────────────────────────

/**
 * @brief Initialise the Nano serial port and load NVS-cached dimensions/crash.
 *        Call from setup().
 */
void grblInit();

/**
 * @brief Must be called from loop(). Reads available bytes from NanoSerial
 *        non-blockingly, fires telemetry handlers, and runs the adaptive poll
 *        and Nano watchdog.
 */
void grblLoop();

/**
 * @brief Block until GRBL reports Idle, or until timeoutMs elapses.
 * @param timeoutMs Maximum time to wait in milliseconds.
 * @return true if Idle was reached, false if timeout expired.
 */
bool waitForGrblIdle(uint32_t timeoutMs);

/**
 * @brief Send $$ to the Nano to request a full settings dump.
 *        Use after homing to refresh the dimension cache.
 */
void requestMachineDimensions();

/** Save current machineDim to NVS. Called automatically when dims update. */
void saveDimensionsToNVS();

/** Save lastCrash to NVS. Called automatically when a crash is parsed. */
void saveCrashToNVS();

/**
 * @brief Enqueue a G-code command to be sent to the Nano.
 *        Commands are sent sequentially only when the Nano is ready ('ok' received).
 *        Real-time commands (?, !, ~) bypass the queue and are sent immediately.
 */
void enqueueGrblCommand(const String& cmd);

/** Clear the crash record from RAM and NVS (e.g. after user acknowledges). */
void clearCrashRecord();

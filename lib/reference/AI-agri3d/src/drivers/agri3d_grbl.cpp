/**
 * @file agri3d_grbl.cpp
 * @brief GRBL/Nano serial bridge implementation.
 *
 * Incoming Nano message formats handled here:
 *   <Idle|MPos:x,y,z|Bf:a,b|F:rate|TMC:0,0,0,0>   ← real-time status
 *   ok                                               ← command acknowledged
 *   ALARM:N                                          ← alarm code (1-9)
 *   [MSG:text]                                       ← informational message
 *   [PREVIOUS_CRASH:X:5,Y1:3]                       ← boot crash replay
 *   $130=xxx  $131=xxx  $132=xxx                     ← dimension settings
 *   error:N                                          ← command error
 */

#include "agri3d_grbl.h"
#include "agri3d_config.h"
#include "agri3d_state.h"
#include "agri3d_network.h"
#include "../core/agri3d_logger.h"
#include "agri3d_sd.h"     
#include <Preferences.h>
#include <ArduinoJson.h>

// ── Hardware serial to Nano ────────────────────────────────────────────────
HardwareSerial NanoSerial(1);

// ── Global instances (declared extern in .h) ──────────────────────────────
TmcStatus        tmcStatus;
MachineDimensions machineDim;
CrashRecord      lastCrash;

// ── NVS namespace ─────────────────────────────────────────────────────────
static Preferences _prefs;
static const char* NVS_NS       = "agri3d";
static const char* NVS_MAX_X    = "maxX";
static const char* NVS_MAX_Y    = "maxY";
static const char* NVS_MAX_Z    = "maxZ";
static const char* NVS_DIM_VALID = "dimOk";
static const char* NVS_CRASH    = "crash";

#include <deque>

// ── Internal state ─────────────────────────────────────────────────────────
static String        _rxBuf;
static unsigned long _lastReplyMs = 0;
static unsigned long _lastPollMs  = 0;

// ── Command Queue ──────────────────────────────────────────────────────────
static std::deque<String> _cmdQueue;
static bool              _nanoReady = true; // Set to true initially so the first command can fire
static SemaphoreHandle_t _grblMutex = NULL; // Protects _cmdQueue and _nanoReady

// ============================================================================
// ALARM CODE LOOKUP
// ============================================================================

const char* alarmCodeDescription(uint8_t code) {
    switch (code) {
        case 1: return "Hard limit triggered";
        case 2: return "Soft limit exceeded";
        case 3: return "Reset while in motion — position may be lost";
        case 4: return "Probe fail: probe not in expected initial state";
        case 5: return "Probe fail: probe did not contact workpiece";
        case 6: return "Homing fail: reset issued during cycle";
        case 7: return "Homing fail: safety door opened during cycle";
        case 8: return "Homing fail: pull-off failed, switch still engaged";
        case 9: return "Homing fail: approach failed, switch never triggered";
        default: return "Unknown alarm";
    }
}

// ============================================================================
// NVS — DIMENSION CACHE
// ============================================================================

static void loadDimensionsFromNVS() {
    _prefs.begin(NVS_NS, true); // read-only
    machineDim.valid = _prefs.getBool(NVS_DIM_VALID, false);
    machineDim.maxX  = _prefs.getFloat(NVS_MAX_X, 0.0f);
    machineDim.maxY  = _prefs.getFloat(NVS_MAX_Y, 0.0f);
    machineDim.maxZ  = _prefs.getFloat(NVS_MAX_Z, 0.0f);
    _prefs.end();

    if (machineDim.valid) {
        AgriLog(TAG_GRBL, LEVEL_INFO, "NVS dims loaded: X=%.1f Y=%.1f Z=%.1f",
                      machineDim.maxX, machineDim.maxY, machineDim.maxZ);
    } else {
        AgriLog(TAG_GRBL, LEVEL_INFO, "No cached dimensions — will wait for homing.");
    }
}

void saveDimensionsToNVS() {
    _prefs.begin(NVS_NS, false); // read-write
    _prefs.putFloat(NVS_MAX_X,  machineDim.maxX);
    _prefs.putFloat(NVS_MAX_Y,  machineDim.maxY);
    _prefs.putFloat(NVS_MAX_Z,  machineDim.maxZ);
    _prefs.putBool(NVS_DIM_VALID, machineDim.valid);
    _prefs.end();
    AgriLog(TAG_GRBL, LEVEL_SUCCESS, "Dims saved to NVS: X=%.1f Y=%.1f Z=%.1f",
                  machineDim.maxX, machineDim.maxY, machineDim.maxZ);
}

// ============================================================================
// NVS — CRASH LOG
// ============================================================================

static void loadCrashFromNVS() {
    _prefs.begin(NVS_NS, true);
    String stored = _prefs.getString(NVS_CRASH, "");
    _prefs.end();

    if (stored.length() > 0) {
        lastCrash.hasRecord = true;
        stored.toCharArray(lastCrash.raw, sizeof(lastCrash.raw));
        AgriLog(TAG_GRBL, LEVEL_INFO, "NVS crash record: %s", lastCrash.raw);
    }
}

void saveCrashToNVS() {
    _prefs.begin(NVS_NS, false);
    _prefs.putString(NVS_CRASH, String(lastCrash.raw));
    _prefs.end();
}

void clearCrashRecord() {
    memset(&lastCrash, 0, sizeof(lastCrash));
    _prefs.begin(NVS_NS, false);
    _prefs.remove(NVS_CRASH);
    _prefs.end();
    AgriLog(TAG_GRBL, LEVEL_SUCCESS, "Crash record cleared.");
}

// ============================================================================
// MESSAGE PARSERS
// ============================================================================

/** Parses <State|MPos:x,y,z|Bf:...|F:...|TMC:a,b,c,d> */
static void parseStatusString(const String& msg) {
    // ── GRBL State ──
    int pipeIdx = msg.indexOf('|');
    if (pipeIdx > 1) {
        String stateStr = msg.substring(1, pipeIdx);
        GrblState newGrbl = GRBL_UNKNOWN;
        if      (stateStr == "Idle")  newGrbl = GRBL_IDLE;
        else if (stateStr == "Run")   newGrbl = GRBL_RUN;
        else if (stateStr == "Jog")   newGrbl = GRBL_JOG;
        else if (stateStr == "Home")  newGrbl = GRBL_HOME;
        else if (stateStr == "Hold")  newGrbl = GRBL_HOLD;
        else if (stateStr == "Alarm") newGrbl = GRBL_ALARM;
        else if (stateStr == "Check") newGrbl = GRBL_CHECK;
        else if (stateStr == "Door")  newGrbl = GRBL_DOOR;
        sysState.setGrbl(newGrbl);
    }

    // ── MPos ──
    int mposIdx = msg.indexOf("MPos:");
    if (mposIdx != -1) {
        mposIdx += 5;
        int endIdx = msg.indexOf('|', mposIdx);
        if (endIdx == -1) endIdx = msg.indexOf('>', mposIdx);
        if (endIdx != -1) {
            String posStr = msg.substring(mposIdx, endIdx);
            int c1 = posStr.indexOf(',');
            int c2 = posStr.indexOf(',', c1 + 1);
            if (c1 != -1 && c2 != -1) {
                float x = posStr.substring(0, c1).toFloat();
                float y = posStr.substring(c1 + 1, c2).toFloat();
                float z = posStr.substring(c2 + 1).toFloat();
                sysState.setPosition(x, y, z);
            }
        }
    }

    // ── TMC driver telemetry "|TMC:0,0,0,0" ──
    int tmcIdx = msg.indexOf("|TMC:");
    if (tmcIdx != -1) {
        tmcIdx += 5;
        int endIdx = msg.indexOf('>', tmcIdx);
        if (endIdx == -1) endIdx = msg.length();
        String tmcStr = msg.substring(tmcIdx, endIdx);
        int c1 = tmcStr.indexOf(',');
        int c2 = (c1 != -1) ? tmcStr.indexOf(',', c1 + 1) : -1;
        int c3 = (c2 != -1) ? tmcStr.indexOf(',', c2 + 1) : -1;
        if (c3 != -1) {
            tmcStatus.x  = (TmcDriverState)constrain(tmcStr.substring(0,c1).toInt(), 0, 2);
            tmcStatus.y1 = (TmcDriverState)constrain(tmcStr.substring(c1+1,c2).toInt(), 0, 2);
            tmcStatus.y2 = (TmcDriverState)constrain(tmcStr.substring(c2+1,c3).toInt(), 0, 2);
            tmcStatus.z  = (TmcDriverState)constrain(tmcStr.substring(c3+1).toInt(), 0, 2);
        }
    }
}

/** Parses [PREVIOUS_CRASH:X:5,Y1:3,Y2:0,Z:0] */
static void parsePreviousCrash(const String& msg) {
    // Extract content between '[PREVIOUS_CRASH:' and ']'
    int start = msg.indexOf("[PREVIOUS_CRASH:");
    if (start == -1) return;
    start += 16; // length of "[PREVIOUS_CRASH:"
    int end = msg.indexOf(']', start);
    if (end == -1) return;

    String content = msg.substring(start, end); // e.g. "X:5,Y1:3"
    content.toCharArray(lastCrash.raw, sizeof(lastCrash.raw));
    lastCrash.hasRecord = true;

    // Parse individual axis states
    auto extractVal = [&](const char* label) -> uint8_t {
        String lbl = String(label) + ":";
        int idx = content.indexOf(lbl);
        if (idx == -1) return 0;
        return (uint8_t)content.substring(idx + lbl.length()).toInt();
    };
    lastCrash.tmcX  = extractVal("X");
    lastCrash.tmcY1 = extractVal("Y1");
    lastCrash.tmcY2 = extractVal("Y2");
    lastCrash.tmcZ  = extractVal("Z");

    saveCrashToNVS();
    AgriLog(TAG_GRBL, LEVEL_INFO, "PREVIOUS CRASH from Nano: %s", lastCrash.raw);

    // Broadcast crash info to Flutter
    StaticJsonDocument<192> doc;
    doc["evt"]    = "PREVIOUS_CRASH";
    doc["record"] = lastCrash.raw;
    doc["tmcX"]   = lastCrash.tmcX;
    doc["tmcY1"]  = lastCrash.tmcY1;
    doc["tmcY2"]  = lastCrash.tmcY2;
    doc["tmcZ"]   = lastCrash.tmcZ;
    String out; serializeJson(doc, out);
    webSocket.broadcastTXT(out);
    
    // Auto-clear the crash alarm per user request
    enqueueGrblCommand("$clr");
}

/** Handles a complete, trimmed line from the Nano. */
static void handleNanoLine(const String& line) {
    if (line.length() == 0) return;

    // Every valid reply resets the watchdog
    sysState.resetNanoWatchdog();

    // ── Real-time status string ──
    if (line.startsWith("<") && line.endsWith(">")) {
        parseStatusString(line);
        // Forward raw status to Flutter for terminal display
        webSocket.broadcastTXT("{\"nano_raw\":\"" + line + "\"}");
        return;
    }

    // ── Machine dimensions + full $$ dump logging ──
    if (line.startsWith("$130=")) {
        machineDim.maxX  = line.substring(5).toFloat();
        machineDim.valid = (machineDim.maxX > 0 && machineDim.maxY > 0);
        AgriLog(TAG_GRBL, LEVEL_INFO, "%s", line.c_str());
        saveDimensionsToNVS();
    } else if (line.startsWith("$131=")) {
        machineDim.maxY  = line.substring(5).toFloat();
        machineDim.valid = (machineDim.maxX > 0 && machineDim.maxY > 0);
        AgriLog(TAG_GRBL, LEVEL_INFO, "%s", line.c_str());
        saveDimensionsToNVS();
    } else if (line.startsWith("$132=")) {
        machineDim.maxZ  = line.substring(5).toFloat();
        AgriLog(TAG_GRBL, LEVEL_INFO, "%s", line.c_str());
        saveDimensionsToNVS();
    } else if (line.startsWith("$") && line.indexOf('=') != -1) {
        // Any other $N=value line from a $$ dump — log it for serial inspection
        AgriLog(TAG_GRBL, LEVEL_INFO, "%s", line.c_str());
    }

    // ── Crash log replayed at Nano boot ──
    else if (line.startsWith("[PREVIOUS_CRASH:")) {
        parsePreviousCrash(line);
        return; // Don't forward the raw crash string — already sent structured JSON
    }

    // ── ALARM ──
    else if (line.startsWith("ALARM:")) {
        _nanoReady = true; // Reset readiness so the auto-clear can actually be sent
        uint8_t code = line.substring(6).toInt();
        sysState.setOperation(OP_ALARM_RECOVERY);
        sysState.setGrbl(GRBL_ALARM);

        StaticJsonDocument<128> doc;
        doc["evt"]   = "ALARM";
        doc["code"]  = code;
        doc["desc"]  = alarmCodeDescription(code);
        String out; serializeJson(doc, out);
        webSocket.broadcastTXT(out);
        AgriLog(TAG_GRBL, LEVEL_ERR, "ALARM %d: %s", code, alarmCodeDescription(code));
        
        // Auto-clear the alarm after saving/broadcasting
        enqueueGrblCommand("$clr");
        return;
    }

    // ── Informational messages (homing sub-messages, etc.) ──
    else if (line.startsWith("[MSG:")) {
        StaticJsonDocument<128> doc;
        doc["evt"] = "MSG";
        doc["msg"] = line.substring(5, line.length() - 1); // strip [MSG: and ]
        String out; serializeJson(doc, out);
        webSocket.broadcastTXT(out);
    }

    // ── ok ──
    else if (line == "ok") {
        sdSignalOk(); // Release SD flow-control gate
        if (_grblMutex && xSemaphoreTake(_grblMutex, portMAX_DELAY)) {
            _nanoReady = true; // Nano is ready for the next command in the queue
            xSemaphoreGive(_grblMutex);
        }
        if (sysState.getOperation() == OP_HOMING) {
            sysState.setOperation(OP_IDLE);
        }
    }
    // ── error ──
    else if (line.startsWith("error:")) {
        if (_grblMutex && xSemaphoreTake(_grblMutex, portMAX_DELAY)) {
            _cmdQueue.clear(); // Clear the queue on error to prevent cascading failures
            _nanoReady = true; // Nano rejected command, but is ready for the next one
            xSemaphoreGive(_grblMutex);
        }
        AgriLog(TAG_GRBL, LEVEL_ERR, "GRBL Error: %s", line.c_str());
    }

    // Forward every raw line to Flutter for the terminal view
    webSocket.broadcastTXT("{\"nano_raw\":\"" + line + "\"}");
}

// ============================================================================
// ADAPTIVE POLL
// ============================================================================

static uint16_t getPollInterval() {
    switch (sysState.getGrbl()) {
        case GRBL_RUN:
        case GRBL_JOG:    return POLL_INTERVAL_RUN;
        case GRBL_HOME:   return POLL_INTERVAL_HOME;
        case GRBL_ALARM:
        case GRBL_UNKNOWN: return POLL_INTERVAL_ALARM;
        default:           return POLL_INTERVAL_IDLE;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void grblInit() {
    NanoSerial.begin(NANO_BAUD, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
    _rxBuf.reserve(128);

    _grblMutex = xSemaphoreCreateMutex();

    loadDimensionsFromNVS();
    loadCrashFromNVS();

    AgriLog(TAG_GRBL, LEVEL_SUCCESS, "Bridge initialised.");
}

void grblLoop() {
    // ── 1. Non-blocking serial reader ──
    while (NanoSerial.available()) {
        char c = (char)NanoSerial.read();
        if (c == '\n') {
            _rxBuf.trim();
            handleNanoLine(_rxBuf);
            _rxBuf = "";
        } else if (c != '\r') {
            _rxBuf += c;
        }
    }

    bool sentCommand = false;

    // ── 2. Command Queue Processing ──
    if (_grblMutex && xSemaphoreTake(_grblMutex, portMAX_DELAY)) {
        if (_nanoReady && !_cmdQueue.empty()) {
            String nextCmd = _cmdQueue.front();
            _cmdQueue.pop_front();
            _nanoReady = false; // Wait for 'ok' before sending next
            if (nextCmd.startsWith("$H")) {
                sysState.setOperation(OP_HOMING);
            }
            NanoSerial.print(nextCmd + '\n'); // Send with \n only, avoids \r parsing bugs
            AgriLog(TAG_GRBL, LEVEL_INFO, "Sent from Queue: %s", nextCmd.c_str());
            sentCommand = true;
        }
        xSemaphoreGive(_grblMutex);
    }

    // ── 3. Adaptive status poll ──
    // We poll more frequently when moving for smooth UI updates.
    // We skip the poll if we JUST sent a command. Sending '?' back-to-back with 
    // a command at 115200 baud causes the Nano's RX interrupt to overrun and 
    // drop the first character of the command, resulting in error:1 or error:2!
    if (!sentCommand) {
        uint16_t interval = getPollInterval();
        if (millis() - _lastPollMs >= interval) {
            _lastPollMs = millis();
            NanoSerial.print('?');
        }
    }

    // (Watchdog check moved to sysState.refreshHeartbeats() in Core 0 task)
}

bool waitForGrblIdle(uint32_t timeoutMs) {
    unsigned long start = millis();

    // ── Phase 1: Confirm the machine actually started moving ─────────────────
    // POLL_INTERVAL_IDLE = 2000ms. The Nano sends 'ok' in ~5ms, making
    // _nanoReady=true and queue empty. But the gantry hasn't physically moved
    // yet! We must see GRBL_RUN before we can trust a subsequent GRBL_IDLE.
    // Wait up to (2 * POLL_INTERVAL_IDLE + 500ms) for a Run status.
    const uint32_t RUN_DETECT_WINDOW_MS = (POLL_INTERVAL_IDLE * 2) + 500;
    bool sawRunning = false;

    while (millis() - start < RUN_DETECT_WINDOW_MS) {
        GrblState gs = sysState.getGrbl();
        if (gs == GRBL_RUN || gs == GRBL_JOG || gs == GRBL_HOME) {
            sawRunning = true;
            break; // Machine confirmed running — enter Phase 2
        }
        // If command hasn't been dispatched yet, keep waiting regardless
        bool queueEmpty = false;
        if (_grblMutex && xSemaphoreTake(_grblMutex, portMAX_DELAY)) {
            queueEmpty = _cmdQueue.empty();
            xSemaphoreGive(_grblMutex);
        }
        if (!queueEmpty) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // ── Phase 2: Wait for genuine Idle ───────────────────────────────────────
    // If sawRunning is false, the move was trivially short (already done before
    // the first poll), which is fine — the queue/nanoReady check covers it.
    while (true) {
        // SAFETY CHECK: If rain is detected or weather gated during motion, abort!
        EnvironmentState env = sysState.getEnvironment();
        if (env == ENV_RAIN_SENSOR || env == ENV_RAIN_AND_WEATHER) {
            AgriLog(TAG_GRBL, LEVEL_ERR, "waitForIdle: ABORT due to rain!");
            return false;
        }

        if (millis() - start > timeoutMs) {
            AgriLog(TAG_GRBL, LEVEL_ERR, "waitForIdle: TIMEOUT");
            return false;
        }

        bool queueEmpty = false;
        bool nanoIsReady = false;
        if (_grblMutex && xSemaphoreTake(_grblMutex, portMAX_DELAY)) {
            queueEmpty = _cmdQueue.empty();
            nanoIsReady = _nanoReady;
            xSemaphoreGive(_grblMutex);
        }

        if (queueEmpty && nanoIsReady && sysState.getGrbl() == GRBL_IDLE) {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void requestMachineDimensions() {
    enqueueGrblCommand("$$");
    AgriLog(TAG_GRBL, LEVEL_INFO, "Enqueued $$ for Nano.");
}

void enqueueGrblCommand(const String& cmd) {
    if (cmd.length() == 0 || !_grblMutex) return;
    
    String upperCmd = cmd;
    upperCmd.toUpperCase();
    
    // Support pseudo-command DELAY:ms by translating to GRBL native dwell (G4 P...)
    if (upperCmd.startsWith("DELAY:")) {
        float sec = upperCmd.substring(6).toFloat() / 1000.0f;
        upperCmd = "G4 P" + String(sec, 3);
    }

    // Real-time overrides bypass the queue
    if (upperCmd == "?" || upperCmd == "!" || upperCmd == "~") {
        NanoSerial.print(upperCmd);
        return;
    }

    if (xSemaphoreTake(_grblMutex, portMAX_DELAY)) {
        // Emergency / Alarm clears bypass the queue AND reset queue state
        if (upperCmd == "$CLR" || upperCmd == "$X") {
            _cmdQueue.clear();
            _nanoReady = true;
            NanoSerial.println(upperCmd);
            AgriLog(TAG_GRBL, LEVEL_WARN, "Bypassed queue for unlock command: %s", upperCmd.c_str());
            xSemaphoreGive(_grblMutex);
            return;
        }

        // Auto-clear before any homing cycle
        if (upperCmd.startsWith("$H")) {
            _cmdQueue.push_back("$CLR");
        }

        _cmdQueue.push_back(upperCmd);
        AgriLog(TAG_GRBL, LEVEL_INFO, "Enqueued: %s (Queue size: %d)", upperCmd.c_str(), _cmdQueue.size());
        xSemaphoreGive(_grblMutex);
    }
}

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
#include "agri3d_sd.h"     // For sdSignalOk()
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

// ── Internal state ─────────────────────────────────────────────────────────
static String        _rxBuf;
static unsigned long _lastReplyMs = 0;
static unsigned long _lastPollMs  = 0;

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
        Serial.printf("[GRBL] NVS dims loaded: X=%.1f Y=%.1f Z=%.1f\n",
                      machineDim.maxX, machineDim.maxY, machineDim.maxZ);
    } else {
        Serial.println("[GRBL] No cached dimensions — will wait for homing.");
    }
}

void saveDimensionsToNVS() {
    _prefs.begin(NVS_NS, false); // read-write
    _prefs.putFloat(NVS_MAX_X,  machineDim.maxX);
    _prefs.putFloat(NVS_MAX_Y,  machineDim.maxY);
    _prefs.putFloat(NVS_MAX_Z,  machineDim.maxZ);
    _prefs.putBool(NVS_DIM_VALID, machineDim.valid);
    _prefs.end();
    Serial.printf("[GRBL] Dims saved to NVS: X=%.1f Y=%.1f Z=%.1f\n",
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
        Serial.printf("[GRBL] NVS crash record: %s\n", lastCrash.raw);
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
    Serial.println("[GRBL] Crash record cleared.");
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
        setGrbl(newGrbl);
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
                setPosition(x, y, z);
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
    Serial.printf("[GRBL] PREVIOUS CRASH from Nano: %s\n", lastCrash.raw);

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
}

/** Handles a complete, trimmed line from the Nano. */
static void handleNanoLine(const String& line) {
    if (line.length() == 0) return;

    // Every valid reply resets the watchdog
    _lastReplyMs = millis();
    if (sysState.nano != NANO_CONNECTED) setNano(NANO_CONNECTED);

    // ── Real-time status string ──
    if (line.startsWith("<") && line.endsWith(">")) {
        parseStatusString(line);
        // Forward raw status to Flutter for terminal display
        webSocket.broadcastTXT("{\"nano_raw\":\"" + line + "\"}");
        return;
    }

    // ── Machine dimensions ──
    if (line.startsWith("$130=")) {
        machineDim.maxX  = line.substring(5).toFloat();
        machineDim.valid = (machineDim.maxX > 0 && machineDim.maxY > 0);
        saveDimensionsToNVS();
    } else if (line.startsWith("$131=")) {
        machineDim.maxY  = line.substring(5).toFloat();
        machineDim.valid = (machineDim.maxX > 0 && machineDim.maxY > 0);
        saveDimensionsToNVS();
    } else if (line.startsWith("$132=")) {
        machineDim.maxZ  = line.substring(5).toFloat();
        saveDimensionsToNVS();
    }

    // ── Crash log replayed at Nano boot ──
    else if (line.startsWith("[PREVIOUS_CRASH:")) {
        parsePreviousCrash(line);
        return; // Don't forward the raw crash string — already sent structured JSON
    }

    // ── ALARM ──
    else if (line.startsWith("ALARM:")) {
        uint8_t code = line.substring(6).toInt();
        setOperation(OP_ALARM_RECOVERY);
        setGrbl(GRBL_ALARM);

        StaticJsonDocument<128> doc;
        doc["evt"]   = "ALARM";
        doc["code"]  = code;
        doc["desc"]  = alarmCodeDescription(code);
        String out; serializeJson(doc, out);
        webSocket.broadcastTXT(out);
        Serial.printf("[GRBL] ALARM %d: %s\n", code, alarmCodeDescription(code));
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
        // Forward raw so Flutter terminal sees it
    }

    // Forward every raw line to Flutter for the terminal view
    webSocket.broadcastTXT("{\"nano_raw\":\"" + line + "\"}");
}

// ============================================================================
// ADAPTIVE POLL
// ============================================================================

static uint16_t getPollInterval() {
    switch (sysState.grbl) {
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

    loadDimensionsFromNVS();
    loadCrashFromNVS();

    Serial.println("[GRBL] Bridge initialised.");
}

void grblLoop() {
    // ── Non-blocking serial reader ──
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

    // ── Adaptive status poll ──
    uint16_t interval = getPollInterval();
    if (millis() - _lastPollMs >= interval) {
        _lastPollMs = millis();
        NanoSerial.print('?');
    }

    // ── Nano watchdog ──
    // Window = max(4 × poll interval, NANO_WATCHDOG_FLOOR_MS)
    if (sysState.nano == NANO_CONNECTED) {
        unsigned long window = max((unsigned long)(getPollInterval() * 4),
                                   (unsigned long)NANO_WATCHDOG_FLOOR_MS);
        if (millis() - _lastReplyMs > window) {
            setNano(NANO_UNRESPONSIVE);
            Serial.println("[GRBL] WARNING: Nano unresponsive!");
        }
    }
}

bool waitForGrblIdle(uint32_t timeoutMs) {
    unsigned long start = millis();
    // Give GRBL a moment to leave Idle before we start checking
    delay(300);
    while (sysState.grbl != GRBL_IDLE) {
        grblLoop(); // Keep reading serial while waiting
        if (millis() - start > timeoutMs) {
            Serial.println("[GRBL] waitForIdle: TIMEOUT");
            return false;
        }
        delay(50);
    }
    return true;
}

void requestMachineDimensions() {
    NanoSerial.println("$$");
    Serial.println("[GRBL] Requested $$ from Nano.");
}

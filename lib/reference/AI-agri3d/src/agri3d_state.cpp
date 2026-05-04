/**
 * @file agri3d_state.cpp
 * @brief Implementation of the state machine transition helpers,
 *        broadcastSystemState(), and all string converter functions.
 */

#include "agri3d_state.h"
#include "agri3d_config.h"
#include <ArduinoJson.h>

// ── Global state instance ──────────────────────────────────────────────────
SystemStatus sysState;

// ============================================================================
// BROADCAST
// ============================================================================

void broadcastSystemState() {
    if (sysState.flutter == FLUTTER_DISCONNECTED) return; // No clients — skip

    // StaticJsonDocument sized for the full payload (~300 bytes)
    StaticJsonDocument<384> doc;
    doc["evt"]         = "SYSTEM_STATE";
    doc["wifi"]        = wifiStateStr(sysState.wifi);
    doc["flutter"]     = flutterStateStr(sysState.flutter);
    doc["nano"]        = nanoStateStr(sysState.nano);
    doc["grbl"]        = grblStateStr(sysState.grbl);
    doc["operation"]   = operationStateStr(sysState.operation);
    doc["environment"] = environmentStateStr(sysState.environment);
    doc["streaming"]   = sysState.isStreaming;
    doc["camera"]      = isCameraAvailable() ? "FREE" : "LOCKED";
    doc["x"]           = serialized(String(sysState.grblX, 2));
    doc["y"]           = serialized(String(sysState.grblY, 2));
    doc["z"]           = serialized(String(sysState.grblZ, 2));
    doc["fpm"]         = sysState.fpm;

    String out;
    serializeJson(doc, out);
    webSocket.broadcastTXT(out);

#if AGRI3D_DEBUG
    Serial.print("[STATE] "); Serial.println(out);
#endif
}

// ============================================================================
// TRANSITION HELPERS
// ============================================================================

void setWifi(WifiState s) {
    if (sysState.wifi == s) return;
    sysState.wifi = s;
    broadcastSystemState();
}

void setFlutter(FlutterState s) {
    if (sysState.flutter == s) return;
    sysState.flutter = s;
    // Note: broadcast happens after assignment — client may have just connected
    broadcastSystemState();
}

void setNano(NanoState s) {
    if (sysState.nano == s) return;
    sysState.nano = s;
    broadcastSystemState();
}

void setGrbl(GrblState s) {
    if (sysState.grbl == s) return;
    sysState.grbl = s;
    broadcastSystemState();
}

void setOperation(OperationState s) {
    if (sysState.operation == s) return;
    sysState.operation = s;
    broadcastSystemState();
}

void setEnvironment(EnvironmentState s) {
    if (sysState.environment == s) return;
    sysState.environment = s;
    broadcastSystemState();
}

void setStreaming(bool active) {
    if (sysState.isStreaming == active) return;
    sysState.isStreaming = active;
    broadcastSystemState();
}

void setFpm(int fpm) {
    fpm = constrain(fpm, STREAM_FPM_MIN, STREAM_FPM_MAX);
    if (sysState.fpm == fpm) return;
    sysState.fpm = fpm;
    broadcastSystemState();
}

/**
 * Updates gantry position without triggering a full state broadcast.
 * Position is included in the NEXT broadcast triggered by a real state change.
 * This avoids flooding clients with position-only packets every 250 ms.
 */
void setPosition(float x, float y, float z) {
    sysState.grblX = x;
    sysState.grblY = y;
    sysState.grblZ = z;
}

// ============================================================================
// CAMERA AVAILABILITY
// ============================================================================

bool isCameraAvailable() {
    return sysState.operation != OP_SCANNING &&
           sysState.operation != OP_AI_WEEDING;
}

// ============================================================================
// STRING CONVERTERS
// ============================================================================

const char* wifiStateStr(WifiState s) {
    switch (s) {
        case WIFI_DISCONNECTED: return "DISCONNECTED";
        case WIFI_CONNECTING:   return "CONNECTING";
        case WIFI_CONNECTED:    return "CONNECTED";
        default:                return "UNKNOWN";
    }
}

const char* flutterStateStr(FlutterState s) {
    switch (s) {
        case FLUTTER_DISCONNECTED: return "DISCONNECTED";
        case FLUTTER_CONNECTED:    return "CONNECTED";
        default:                   return "UNKNOWN";
    }
}

const char* nanoStateStr(NanoState s) {
    switch (s) {
        case NANO_UNKNOWN:       return "UNKNOWN";
        case NANO_CONNECTED:     return "CONNECTED";
        case NANO_UNRESPONSIVE:  return "UNRESPONSIVE";
        default:                 return "UNKNOWN";
    }
}

const char* grblStateStr(GrblState s) {
    switch (s) {
        case GRBL_UNKNOWN: return "UNKNOWN";
        case GRBL_IDLE:    return "IDLE";
        case GRBL_RUN:     return "RUN";
        case GRBL_JOG:     return "JOG";
        case GRBL_HOME:    return "HOME";
        case GRBL_HOLD:    return "HOLD";
        case GRBL_ALARM:   return "ALARM";
        case GRBL_CHECK:   return "CHECK";
        case GRBL_DOOR:    return "DOOR";
        default:           return "UNKNOWN";
    }
}

const char* operationStateStr(OperationState s) {
    switch (s) {
        case OP_IDLE:           return "IDLE";
        case OP_HOMING:         return "HOMING";
        case OP_SD_RUNNING:     return "SD_RUNNING";
        case OP_FERTILIZING:    return "FERTILIZING";
        case OP_SCANNING:       return "SCANNING";
        case OP_AI_WEEDING:     return "AI_WEEDING";
        case OP_RAIN_PAUSED:    return "RAIN_PAUSED";
        case OP_ALARM_RECOVERY: return "ALARM_RECOVERY";
        default:                return "UNKNOWN";
    }
}

const char* environmentStateStr(EnvironmentState s) {
    switch (s) {
        case ENV_CLEAR:            return "CLEAR";
        case ENV_RAIN_SENSOR:      return "RAIN_SENSOR";
        case ENV_WEATHER_GATED:    return "WEATHER_GATED";
        case ENV_RAIN_AND_WEATHER: return "RAIN_AND_WEATHER";
        default:                   return "CLEAR";
    }
}

/**
 * @file agri3d_state.cpp
 * @brief Implementation of the SystemState class.
 */

#include "agri3d_state.h"
#include "agri3d_config.h"
#include <ArduinoJson.h>

// Global instance
SystemState sysState;

SystemState::SystemState() 
    : _wifi(WIFI_DISCONNECTED),
      _flutter(FLUTTER_DISCONNECTED),
      _nano(NANO_UNKNOWN),
      _grbl(GRBL_UNKNOWN),
      _operation(OP_IDLE),
      _environment(ENV_CLEAR),
      _isStreaming(false),
      _grblX(0.0f),
      _grblY(0.0f),
      _grblZ(0.0f),
      _fpm(STREAM_FPM_DEFAULT) 
{}

// ============================================================================
// BROADCAST
// ============================================================================

void SystemState::broadcast() {
    if (_flutter == FLUTTER_DISCONNECTED) return;

    StaticJsonDocument<384> doc;
    doc["evt"]         = "SYSTEM_STATE";
    doc["wifi"]        = wifiStr(_wifi);
    doc["flutter"]     = flutterStr(_flutter);
    doc["nano"]        = nanoStr(_nano);
    doc["grbl"]        = grblStr(_grbl);
    doc["operation"]   = opStr(_operation);
    doc["environment"] = envStr(_environment);
    doc["streaming"]   = _isStreaming;
    doc["camera"]      = isCameraAvailable() ? "FREE" : "LOCKED";
    doc["x"]           = serialized(String(_grblX, 2));
    doc["y"]           = serialized(String(_grblY, 2));
    doc["z"]           = serialized(String(_grblZ, 2));
    doc["fpm"]         = _fpm;

    String out;
    serializeJson(doc, out);
    webSocket.broadcastTXT(out);
}

// ============================================================================
// SETTERS
// ============================================================================

void SystemState::setWifi(WifiState s) {
    if (_wifi == s) return;
    _wifi = s;
    broadcast();
}

void SystemState::setFlutter(FlutterState s) {
    if (_flutter == s) return;
    _flutter = s;
    broadcast();
}

void SystemState::setNano(NanoState s) {
    if (_nano == s) return;
    _nano = s;
    broadcast();
}

void SystemState::setGrbl(GrblState s) {
    if (_grbl == s) return;
    _grbl = s;
    broadcast();
}

void SystemState::setOperation(OperationState s) {
    if (_operation == s) return;
    _operation = s;
    broadcast();
}

void SystemState::setEnvironment(EnvironmentState s) {
    if (_environment == s) return;
    _environment = s;
    broadcast();
}

void SystemState::setStreaming(bool active) {
    if (_isStreaming == active) return;
    _isStreaming = active;
    broadcast();
}

void SystemState::setFpm(int fpm) {
    fpm = constrain(fpm, STREAM_FPM_MIN, STREAM_FPM_MAX);
    if (_fpm == fpm) return;
    _fpm = fpm;
    broadcast();
}

void SystemState::setPosition(float x, float y, float z) {
    _grblX = x;
    _grblY = y;
    _grblZ = z;
}

// ============================================================================
// CAMERA AVAILABILITY
// ============================================================================

bool isCameraAvailable() {
    return sysState.getOperation() != OP_SCANNING &&
           sysState.getOperation() != OP_AI_WEEDING;
}

// ============================================================================
// STRING CONVERTERS (Private Helpers)
// ============================================================================

const char* SystemState::wifiStr(WifiState s) {
    switch (s) {
        case WIFI_DISCONNECTED: return "DISCONNECTED";
        case WIFI_CONNECTING:   return "CONNECTING";
        case WIFI_CONNECTED:    return "CONNECTED";
        default:                return "UNKNOWN";
    }
}

const char* SystemState::flutterStr(FlutterState s) {
    switch (s) {
        case FLUTTER_DISCONNECTED: return "DISCONNECTED";
        case FLUTTER_CONNECTED:    return "CONNECTED";
        default:                   return "UNKNOWN";
    }
}

const char* SystemState::nanoStr(NanoState s) {
    switch (s) {
        case NANO_UNKNOWN:       return "UNKNOWN";
        case NANO_CONNECTED:     return "CONNECTED";
        case NANO_UNRESPONSIVE:  return "UNRESPONSIVE";
        default:                 return "UNKNOWN";
    }
}

const char* SystemState::grblStr(GrblState s) {
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

const char* SystemState::opStr(OperationState s) {
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

const char* SystemState::envStr(EnvironmentState s) {
    switch (s) {
        case ENV_CLEAR:            return "CLEAR";
        case ENV_RAIN_SENSOR:      return "RAIN_SENSOR";
        case ENV_WEATHER_GATED:    return "WEATHER_GATED";
        case ENV_RAIN_AND_WEATHER: return "RAIN_AND_WEATHER";
        default:                   return "CLEAR";
    }
}

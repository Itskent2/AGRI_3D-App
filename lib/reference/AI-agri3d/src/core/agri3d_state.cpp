/**
 * @file agri3d_state.cpp
 * @brief Implementation of the SystemState class.
 */

#include "agri3d_state.h"
#include "agri3d_config.h"
#include "agri3d_logger.h"
#include "agri3d_routine.h"
#include <ArduinoJson.h>
#include <Preferences.h>

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
      _streamTaskBusy(false),
      _scanReadyForUpload(false),
      _camOffset(DEFAULT_CAM_OFFSET_MM),
      _grblX(0.0f),
      _grblY(0.0f),
      _fpm(STREAM_FPM_DEFAULT),
      _resolution(FRAMESIZE_QQVGA),
      _lastNanoHeartbeatMs(0),
      _lastFlutterHeartbeatMs(0),
      _lastFlutterActivityMs(0),
      _lastFlutterWarnLogMs(0)
{
    // Restore persisted camera offset from NVS (if saved by a previous session)
    Preferences prefs;
    prefs.begin("agri3d_cfg", true); // read-only
    _camOffset = prefs.getFloat("cam_offset", DEFAULT_CAM_OFFSET_MM);
    prefs.end();
}

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
    doc["res"]         = (int)_resolution;
    doc["w_rate"]      = getWaterFlowRate();
    doc["f_rate"]      = getFertFlowRate();
    doc["scan_ready"]  = _scanReadyForUpload;
    doc["cam_offset"]  = _camOffset;

    String out;
    serializeJson(doc, out);
    webSocket.broadcastTXT(out);
    
    _lastFlutterHeartbeatMs = millis(); // Reset proactive timer on any broadcast
}

// ============================================================================
// HEARTBEATS
// ============================================================================

void SystemState::refreshHeartbeats() {
    unsigned long now = millis();

    // ── 1. Nano Watchdog ──
    if (_nano == NANO_CONNECTED) {
        // Window follows config: max(4 * current polling interval, floor)
        unsigned long window = max(4UL * currentPollIntervalMs(),
                                   (unsigned long)NANO_WATCHDOG_FLOOR_MS);
        
        // Increase timeout for homing
        if (_operation == OP_HOMING) {
            window = max(window, (unsigned long)NANO_WATCHDOG_HOME_MS);
        }
        
        if (now - _lastNanoHeartbeatMs > window) {
            setNano(NANO_UNRESPONSIVE);
        }
    }

    // ── 2. Flutter Proactive Heartbeat & Watchdog ──
    if (_flutter == FLUTTER_CONNECTED) {
        // A. Proactive State Update (if silent for too long)
        if (now - _lastFlutterHeartbeatMs > HEARTBEAT_INTERVAL_MS) {
            broadcast();
        }

        // B. Safety Watchdog (if app stops responding/pinging)
        unsigned long flutterWatchdogWindow =
            max(2UL * (unsigned long)HEARTBEAT_INTERVAL_MS,
                (unsigned long)FLUTTER_WATCHDOG_FLOOR_MS);
        if (now - _lastFlutterActivityMs > flutterWatchdogWindow) {
            if (_lastFlutterWarnLogMs == 0 ||
                now - _lastFlutterWarnLogMs >=
                    (unsigned long)FLUTTER_WATCHDOG_WARN_INTERVAL_MS) {
                AgriLog(TAG_NET, LEVEL_WARN, "Flutter heartbeat silent past watchdog window.");
                _lastFlutterWarnLogMs = now;
            }
        }

        if (FLUTTER_FORCE_DISCONNECT_ON_TIMEOUT) {
            unsigned long flutterDisconnectWindow =
                max(3UL * (unsigned long)HEARTBEAT_INTERVAL_MS,
                    (unsigned long)FLUTTER_DISCONNECT_FLOOR_MS);
            if (now - _lastFlutterActivityMs > flutterDisconnectWindow) {
                AgriLog(TAG_NET, LEVEL_WARN, "Flutter link stale; forcing disconnect.");
                setFlutter(FLUTTER_DISCONNECTED);
            }
        }
    }
}

void SystemState::resetNanoWatchdog() {
    _lastNanoHeartbeatMs = millis();
    if (_nano != NANO_CONNECTED) {
        setNano(NANO_CONNECTED);
    }
}

void SystemState::resetFlutterWatchdog() {
    _lastFlutterActivityMs = millis();
    _lastFlutterWarnLogMs = 0;
    // We no longer reset _lastFlutterHeartbeatMs here, so that 
    // proactive broadcasts happen on a fixed interval (standardized).
    
    if (_flutter != FLUTTER_CONNECTED) {
        setFlutter(FLUTTER_CONNECTED);
    }
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

void SystemState::setStreamTaskBusy(bool b) {
    _streamTaskBusy = b;
}

void SystemState::setFpm(int fpm) {
    fpm = constrain(fpm, STREAM_FPM_MIN, STREAM_FPM_MAX);
    if (_fpm == fpm) return;
    _fpm = fpm;
    broadcast();
}

void SystemState::setResolution(framesize_t res) {
    if (_resolution == res) return;
    _resolution = res;
    broadcast();
}

void SystemState::setPosition(float x, float y, float z) {
    _grblX = x;
    _grblY = y;
    _grblZ = z;
}

void SystemState::setScanReadyForUpload(bool ready) {
    if (_scanReadyForUpload == ready) return;
    _scanReadyForUpload = ready;
    broadcast();
}

float SystemState::getCamOffset() const {
    return _camOffset;
}

void SystemState::setCamOffset(float v) {
    if (_camOffset == v) return;
    _camOffset = v;
    // Persist to NVS so offset survives reboot
    Preferences prefs;
    prefs.begin("agri3d_cfg", false);
    prefs.putFloat("cam_offset", v);
    prefs.end();
    broadcast();
    AgriLog(TAG_STATE, LEVEL_INFO, "Camera offset set to %.1f mm", v);
}

unsigned long SystemState::currentPollIntervalMs() const {
    switch (_grbl) {
        case GRBL_RUN:
        case GRBL_JOG:
            return POLL_INTERVAL_RUN;
        case GRBL_HOME:
            return POLL_INTERVAL_HOME;
        case GRBL_ALARM:
        case GRBL_CHECK:
        case GRBL_DOOR:
            return POLL_INTERVAL_ALARM;
        case GRBL_IDLE:
        case GRBL_HOLD:
        case GRBL_UNKNOWN:
        default:
            return POLL_INTERVAL_IDLE;
    }
}

// ============================================================================
// CAMERA AVAILABILITY
// ============================================================================

bool isCameraAvailable() {
    return sysState.getOperation() != OP_SCANNING &&
           sysState.getOperation() != OP_UPLOADING &&
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
        case OP_UPLOADING:      return "UPLOADING";
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

#pragma once
#include <Arduino.h>
#include <WebSocketsServer.h>
#include "agri3d_config.h"
#include "SystemEnums.h"
#include <esp_camera.h>

// ── Forward declaration ────────────────────────────────────────────────────
extern WebSocketsServer webSocket;

// ============================================================================
// SYSTEM STATE CLASS (OOP)
// ============================================================================

class SystemState {
public:
    SystemState();

    // ── Safe Frame Handoff (Core 0 <-> Core 1) ─────────────────────────────
    uint8_t*     pendingFrame = nullptr;
    size_t       pendingFrameLen = 0;
    int          pendingFrameClient = -1;
    camera_fb_t* pendingFrameFB = nullptr; // Raw pointer to camera buffer
    String*      pendingAiResult = nullptr; // Pointer to JSON string for AI detections

    // ── Getters (Encapsulation) ─────────────────────────────────────────────
    WifiState        getWifi()        const { return _wifi; }
    FlutterState     getFlutter()     const { return _flutter; }
    NanoState        getNano()        const { return _nano; }
    GrblState        getGrbl()        const { return _grbl; }
    OperationState   getOperation()   const { return _operation; }
    EnvironmentState getEnvironment() const { return _environment; }
    bool             isStreaming()    const { return _isStreaming; }
    bool             isStreamTaskBusy() const { return _streamTaskBusy; }

    float getX() const { return _grblX; }
    float getY() const { return _grblY; }
    float getZ() const { return _grblZ; }
    int   getFpm() const { return _fpm; }
    framesize_t getResolution() const { return _resolution; }
    bool  isScanReadyForUpload() const { return _scanReadyForUpload; }

    // ── Setters (Logic triggers) ────────────────────────────────────────────
    void setWifi(WifiState s);
    void setFlutter(FlutterState s);
    void setNano(NanoState s);
    void setGrbl(GrblState s);
    void setOperation(OperationState s);
    void setEnvironment(EnvironmentState s);
    void setStreaming(bool active);
    void setStreamTaskBusy(bool b);
    void setFpm(int fpm);
    void setResolution(framesize_t res);
    void setPosition(float x, float y, float z);
    void setScanReadyForUpload(bool ready);
    float getCamOffset() const;
    void setCamOffset(float v);

    /**
     * @brief Serialises the full state to JSON and broadcasts to all clients.
     */
    void broadcast();

    /**
     * @brief Periodic health check for all links.
     * Centralises watchdog logic for Nano and pro-active pings for Flutter.
     */
    void refreshHeartbeats();
    void resetNanoWatchdog();
    void resetFlutterWatchdog();


private:
    WifiState        _wifi;
    FlutterState     _flutter;
    NanoState        _nano;
    GrblState        _grbl;
    OperationState   _operation;
    EnvironmentState _environment;

    bool  _isStreaming;
    bool  _streamTaskBusy;
    bool  _scanReadyForUpload;
    float _camOffset;
    float _grblX;
    float _grblY;
    float _grblZ;
    int   _fpm;
    framesize_t _resolution;
    unsigned long _lastNanoHeartbeatMs;
    unsigned long _lastFlutterHeartbeatMs;
    unsigned long _lastFlutterActivityMs;
    unsigned long _lastFlutterWarnLogMs;
    unsigned long currentPollIntervalMs() const;

    const char* wifiStr(WifiState s);
    const char* flutterStr(FlutterState s);
    const char* nanoStr(NanoState s);
    const char* grblStr(GrblState s);
    const char* opStr(OperationState s);
    const char* envStr(EnvironmentState s);
};

// Global singleton instance
extern SystemState sysState;

/** Helper for camera availability */
bool isCameraAvailable();

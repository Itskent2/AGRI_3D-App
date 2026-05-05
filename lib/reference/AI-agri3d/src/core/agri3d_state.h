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

    // ── Getters (Encapsulation) ─────────────────────────────────────────────
    WifiState        getWifi()        const { return _wifi; }
    FlutterState     getFlutter()     const { return _flutter; }
    NanoState        getNano()        const { return _nano; }
    GrblState        getGrbl()        const { return _grbl; }
    OperationState   getOperation()   const { return _operation; }
    EnvironmentState getEnvironment() const { return _environment; }
    bool             isStreaming()    const { return _isStreaming; }

    float getX() const { return _grblX; }
    float getY() const { return _grblY; }
    float getZ() const { return _grblZ; }
    int   getFpm() const { return _fpm; }

    // ── Setters (Logic triggers) ────────────────────────────────────────────
    void setWifi(WifiState s);
    void setFlutter(FlutterState s);
    void setNano(NanoState s);
    void setGrbl(GrblState s);
    void setOperation(OperationState s);
    void setEnvironment(EnvironmentState s);
    void setStreaming(bool active);
    void setFpm(int fpm);
    void setPosition(float x, float y, float z);

    /**
     * @brief Serialises the full state to JSON and broadcasts to all clients.
     */
    void broadcast();

    /** 
     * @brief Safe Frame Handoff Buffer
     * Note: In a full OOP refactor, this might move to a FrameBuffer class.
     */
    uint8_t* pendingFrame = nullptr;
    size_t   pendingFrameLen = 0;
    int8_t   pendingFrameClient = -1;

private:
    WifiState        _wifi;
    FlutterState     _flutter;
    NanoState        _nano;
    GrblState        _grbl;
    OperationState   _operation;
    EnvironmentState _environment;

    bool  _isStreaming;
    float _grblX;
    float _grblY;
    float _grblZ;
    int   _fpm;

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

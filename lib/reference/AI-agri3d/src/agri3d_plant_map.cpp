/**
 * @file agri3d_plant_map.cpp
 * @brief SCAN_PLANT command handler: pre-homing, G-code snake-grid traversal,
 *        per-frame capture + metadata broadcast, and AI weeding hook stub.
 */

#include "agri3d_plant_map.h"
#include "agri3d_config.h"
#include "agri3d_state.h"
#include "agri3d_grbl.h"
#include "agri3d_camera.h"
#include "agri3d_network.h"
#include <ArduinoJson.h>

// ============================================================================
// INTERNAL: PRE-SCAN HOMING
// ============================================================================

/**
 * @brief Home X then Y. Z is intentionally skipped (hardware broken).
 * @return true if both homing cycles completed successfully.
 */
static bool homeScanAxes() {
    Serial.println("[SCAN] Homing X...");
    NanoSerial.println("$HX");
    if (!waitForGrblIdle(SCAN_HOME_TIMEOUT_MS)) {
        Serial.println("[SCAN] ERROR: $HX timeout");
        return false;
    }
    delay(300);

    Serial.println("[SCAN] Homing Y...");
    NanoSerial.println("$HY");
    if (!waitForGrblIdle(SCAN_HOME_TIMEOUT_MS)) {
        Serial.println("[SCAN] ERROR: $HY timeout");
        return false;
    }
    delay(300);

    // Refresh dimension cache from Nano EEPROM after homing
    requestMachineDimensions();
    delay(1000); // Give Nano time to dump $130/$131 replies

    Serial.printf("[SCAN] Homed. Workspace: X=%.1f Y=%.1f\n",
                  machineDim.maxX, machineDim.maxY);
    return true;
}

// ============================================================================
// INTERNAL: BROADCAST HELPERS
// ============================================================================

static void broadcastScanStart(uint8_t clientNum, int total,
                                int cols, int rows,
                                float stepX, float stepY, float zHeight) {
    StaticJsonDocument<192> doc;
    doc["evt"]     = "SCAN_START";
    doc["total"]   = total;
    doc["cols"]    = cols;
    doc["rows"]    = rows;
    doc["stepX"]   = stepX;
    doc["stepY"]   = stepY;
    doc["zHeight"] = zHeight;
    doc["maxX"]    = machineDim.maxX;
    doc["maxY"]    = machineDim.maxY;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
}

static void broadcastScanComplete(uint8_t clientNum, int total, bool aborted) {
    StaticJsonDocument<96> doc;
    doc["evt"]     = "SCAN_COMPLETE";
    doc["total"]   = total;
    doc["aborted"] = aborted;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
}

static void broadcastScanError(uint8_t clientNum, const char* reason) {
    StaticJsonDocument<96> doc;
    doc["evt"]    = "SCAN_ERROR";
    doc["reason"] = reason;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
    Serial.printf("[SCAN] ERROR: %s\n", reason);
}

// ============================================================================
// PUBLIC: SCAN_PLANT HANDLER
// ============================================================================

void handleScanPlant(uint8_t clientNum, const String& cmdBody) {
    // ── Parse parameters ──────────────────────────────────────────────────
    // Format: "cols:rows:stepX:stepY:zHeight"
    int c1 = cmdBody.indexOf(':');
    int c2 = cmdBody.indexOf(':', c1 + 1);
    int c3 = cmdBody.indexOf(':', c2 + 1);
    int c4 = cmdBody.indexOf(':', c3 + 1);

    if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) {
        broadcastScanError(clientNum, "Bad format. Expected cols:rows:stepX:stepY:zHeight");
        return;
    }

    int   cols    = cmdBody.substring(0, c1).toInt();
    int   rows    = cmdBody.substring(c1+1, c2).toInt();
    float stepX   = cmdBody.substring(c2+1, c3).toFloat();
    float stepY   = cmdBody.substring(c3+1, c4).toFloat();
    float zHeight = cmdBody.substring(c4+1).toFloat();

    if (cols <= 0 || rows <= 0 || stepX <= 0 || stepY <= 0) {
        broadcastScanError(clientNum, "Invalid scan parameters (must be > 0)");
        return;
    }

    int total = cols * rows;
    Serial.printf("[SCAN] Starting %dx%d scan (%d frames) stepX=%.1f stepY=%.1f z=%.1f\n",
                  cols, rows, total, stepX, stepY, zHeight);

    // ── Guard: no scan while already scanning / alarmed ──────────────────
    if (sysState.operation == OP_SCANNING ||
        sysState.operation == OP_ALARM_RECOVERY) {
        broadcastScanError(clientNum, "Cannot scan: system busy or in alarm");
        return;
    }

    // ── Pause live stream and lock camera ─────────────────────────────────
    bool streamWasActive = sysState.isStreaming;
    setStreaming(false);
    setOperation(OP_HOMING);

    // ── Pre-scan homing ───────────────────────────────────────────────────
    if (!homeScanAxes()) {
        broadcastScanError(clientNum, "Homing failed — scan aborted");
        if (streamWasActive) setStreaming(true);
        setOperation(OP_IDLE);
        return;
    }

    setOperation(OP_SCANNING);
    broadcastScanStart(clientNum, total, cols, rows, stepX, stepY, zHeight);

    // ── Snake-pattern grid traversal ──────────────────────────────────────
    // Row 0: col 0→cols-1 (L→R)
    // Row 1: col cols-1→0  (R→L)
    // Row 2: col 0→cols-1 ...
    int frameIdx = 0;
    bool aborted = false;

    for (int row = 0; row < rows && !aborted; row++) {
        for (int colStep = 0; colStep < cols && !aborted; colStep++) {

            // Snake direction: even rows go L→R, odd rows go R→L
            int col = (row % 2 == 0) ? colStep : (cols - 1 - colStep);

            float targetX = col  * stepX;
            float targetY = row  * stepY;

            frameIdx++;

            // Safety: don't exceed measured workspace
            if (machineDim.valid) {
                if (targetX > machineDim.maxX) targetX = machineDim.maxX;
                if (targetY > machineDim.maxY) targetY = machineDim.maxY;
            }

            // Abort if client disconnected mid-scan
            if (sysState.flutter == FLUTTER_DISCONNECTED) {
                Serial.println("[SCAN] Client disconnected — aborting scan");
                aborted = true;
                break;
            }

            // Move and capture
            bool ok = captureFrameAtPosition(clientNum, frameIdx, total,
                                              targetX, targetY);
            if (!ok) {
                Serial.printf("[SCAN] Frame %d failed — continuing\n", frameIdx);
                // Non-fatal: skip this frame, keep scanning
            }
        }
    }

    // ── Return to origin ──────────────────────────────────────────────────
    Serial.println("[SCAN] Returning to origin (0, 0)");
    NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);

    // ── Restore state ─────────────────────────────────────────────────────
    setOperation(OP_IDLE);
    if (streamWasActive) setStreaming(true);
    broadcastScanComplete(clientNum, frameIdx, aborted);

    Serial.printf("[SCAN] Done. %d/%d frames captured.\n", frameIdx, total);
}

// =============================================================================
// TODO(Luna): AI Weeding Capture Implementation
// =============================================================================
//
// void aiCaptureCoordsForWeeding(uint8_t clientNum,
//                                 float* xList, float* yList, int count) {
//     setStreaming(false);
//     setOperation(OP_AI_WEEDING);
//     broadcastSystemState();
//
//     for (int i = 0; i < count; i++) {
//         captureFrameAtPosition(clientNum, i + 1, count, xList[i], yList[i]);
//         // TODO(Luna): After each capture, pass frame to aiProcessFrame()
//         // TODO(Luna): Decide whether to actuate weeder at this coordinate
//     }
//
//     NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
//     waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
//     setOperation(OP_IDLE);
//     setStreaming(true);
// }
//
// =============================================================================
// TODO(Luna): AI Frame Processor
// =============================================================================
//
// Called from captureFrameAtPosition() when AI is enabled.
// Input:  raw JPEG buffer + real-world XY coordinate of the capture
// Output: list of weed locations (relative pixel coords or mm offsets)
//
// void aiProcessFrame(const uint8_t* jpegBuf, size_t jpegLen,
//                     float captureX, float captureY) {
//     // TODO(Luna): Load TFLite model if not already loaded
//     // TODO(Luna): Decode JPEG → RGB tensor
//     // TODO(Luna): Run inference
//     // TODO(Luna): For each detected weed bounding box:
//     //               Convert pixel offset to mm offset using zHeight + FOV calibration
//     //               Add to weed coordinate list
//     //               Optionally broadcast {"evt":"WEED_DETECTED","x":...,"y":...}
// }
// =============================================================================

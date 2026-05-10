/**
 * @file agri3d_commands.cpp
 * @brief Central WebSocket command router.
 *
 * All command parsing lives here. Each handler is implemented in its
 * own module — this file only dispatches.
 */

#include "agri3d_commands.h"
#include "agri3d_config.h"
#include "agri3d_state.h"
#include "agri3d_network.h"
#include "agri3d_grbl.h"
#include "agri3d_camera.h"
#include "agri3d_plant_map.h"
#include "agri3d_logger.h"
#include "agri3d_npk.h"
#include "agri3d_routine.h"
#include "agri3d_environment.h"
#include "agri3d_sd.h"
#include <SD_MMC.h>
#include <ArduinoJson.h>

// ── Helpers ────────────────────────────────────────────────────────────────

/** Send a simple error JSON to one client. */
static void sendError(uint8_t num, const char* reason) {
    StaticJsonDocument<80> doc;
    doc["evt"]    = "ERROR";
    doc["reason"] = reason;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(num, out);
}

/** Returns everything after the first ':' in a command string. */
static String args(const String& cmd) {
    int idx = cmd.indexOf(':');
    return (idx >= 0) ? cmd.substring(idx + 1) : "";
}

/** Returns true if cmd matches prefix and optionally has args. */
static bool startsWith(const String& cmd, const char* prefix) {
    return cmd.startsWith(prefix);
}

// ============================================================================
// MAIN EVENT HANDLER
// ============================================================================

void webSocketEvent(uint8_t num, WStype_t type,
                    uint8_t* payload, size_t length) {
    (void)length;

    if (type != WStype_TEXT) return; // Binary/ping handled by network layer
    sysState.resetFlutterWatchdog();

    String cmd = String((char*)payload);
    cmd.trim();
    if (cmd.length() == 0) return;

#if AGRI3D_DEBUG
    AgriLog(TAG_CMD, LEVEL_INFO, "#%d: %s", num, cmd.c_str());
#endif

    // ── Stream ──────────────────────────────────────────────────────────────
    if (cmd == "START_STREAM") {
        if (!isCameraAvailable()) {
            sendError(num, "Camera locked by active operation");
            return;
        }
        sysState.setStreaming(true);
        return;
    }
    if (cmd == "STOP_STREAM") {
        sysState.setStreaming(false);
        return;
    }
    if (startsWith(cmd, "SET_FPM:")) {
        int fpm = args(cmd).toInt();
        if (fpm < STREAM_FPM_MIN || fpm > STREAM_FPM_MAX) {
            sendError(num, "FPM out of range (1-300)");
            return;
        }
        sysState.setFpm(fpm);
        return;
    }
    if (startsWith(cmd, "SET_RES:")) {
        int res = args(cmd).toInt();
        sensor_t *s = esp_camera_sensor_get();
        if (s) {
            bool wasStreaming = sysState.isStreaming();
            if (wasStreaming) {
                sysState.setStreaming(false);
                // Wait for stream task to finish current frame and pause
                while (sysState.isStreamTaskBusy()) {
                    delay(10);
                }
            }
            s->set_framesize(s, (framesize_t)res);
            sysState.setResolution((framesize_t)res);
            delay(500); // Wait for sensor to stabilize after reallocation
            AgriLog(TAG_CAM, LEVEL_INFO, "Resolution set to %d", res);
            if (wasStreaming) {
                sysState.setStreaming(true);
            }
        }
        return;
    }

    // ── State queries ────────────────────────────────────────────────────────
    if (cmd == "GET_STATE") {
        sysState.broadcast();
        // Also re-send plant map and today's NPK history on reconnect
        broadcastPlantMap(num);
        npkSendFullHistory(num);
        return;
    }
    if (cmd.startsWith("GET_NPK_LOG:")) {
        // Read from SD card file /soil_data.csv and send in chunks
        File file = SD_MMC.open("/soil_data.csv", FILE_READ);
        if (!file) {
            webSocket.sendTXT(num, "{\"evt\":\"NPK_LOG_END\",\"status\":\"fail\"}");
            return;
        }
        
        DynamicJsonDocument doc(2048);
        doc["evt"] = "NPK_LOG_CHUNK";
        JsonArray readings = doc.createNestedArray("readings");
        
        // Skip header
        if (file.available()) file.readStringUntil('\n');
        
        int count = 0;
        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (line.length() < 10) continue;
            
            long ts;
            float x, y;
            int gx, gy;
            float m, temp, ec, ph, n, p, k;
            if (sscanf(line.c_str(), "%ld,%f,%f,%d,%d,%f,%f,%f,%f,%f,%f,%f", 
                       &ts, &x, &y, &gx, &gy, &m, &temp, &ec, &ph, &n, &p, &k) == 12) {
                JsonObject r = readings.createNestedObject();
                r["ts"] = ts;
                r["x"] = gx;
                r["y"] = gy;
                r["n"] = n;
                r["p"] = p;
                r["k"] = k;
                r["m"] = m;
                r["ec"] = ec;
                r["ph"] = ph;
                count++;
            }
            
            if (count >= 10) {
                String out; serializeJson(doc, out);
                webSocket.sendTXT(num, out);
                doc.clear();
                doc["evt"] = "NPK_LOG_CHUNK";
                readings = doc.createNestedArray("readings");
                count = 0;
            }
        }
        
        if (count > 0) {
            String out; serializeJson(doc, out);
            webSocket.sendTXT(num, out);
        }
        
        file.close();
        webSocket.sendTXT(num, "{\"evt\":\"NPK_LOG_END\",\"status\":\"ok\"}");
        return;
    }

    if (cmd == "PING" || cmd.startsWith("{\"cmd\":\"PING\"")) {
        webSocket.sendTXT(num, "{\"evt\":\"PONG\",\"ms\":" +
                               String(millis()) + "}");
        return;
    }
    if (cmd == "CLEAR_CRASH") {
        clearCrashRecord();
        webSocket.sendTXT(num, "{\"evt\":\"CRASH_CLEARED\"}");
        return;
    }
    if (cmd == "GET_DIMS") {
        StaticJsonDocument<128> doc;
        doc["evt"]   = "DIMENSIONS";
        doc["maxX"]  = machineDim.maxX;
        doc["maxY"]  = machineDim.maxY;
        doc["maxZ"]  = machineDim.maxZ;
        doc["valid"] = machineDim.valid;
        String out; serializeJson(doc, out);
        webSocket.sendTXT(num, out);
        return;
    }

    // ── Gantry / GRBL ────────────────────────────────────────────────────────
    if (cmd == "HOME_X") {
        sysState.setOperation(OP_HOMING);
        enqueueGrblCommand("$HX");
        return;
    }
    if (cmd == "HOME_Y") {
        sysState.setOperation(OP_HOMING);
        enqueueGrblCommand("$HY");
        return;
    }
    if (cmd == "UNLOCK") {
        enqueueGrblCommand("$clr");
        sysState.setOperation(OP_IDLE);
        return;
    }
    if (cmd == "ESTOP") {
        NanoSerial.write(0x18); // Ctrl-X — GRBL hard stop
        sysState.setOperation(OP_ALARM_RECOVERY);
        webSocket.sendTXT(num, "{\"evt\":\"ESTOP_SENT\"}");
        return;
    }
    if (startsWith(cmd, "GCODE:")) {
        // Raw G-code passthrough — forward directly
        enqueueGrblCommand(args(cmd));
        return;
    }

    // ── SD Card ──────────────────────────────────────────────────────────────
    if (startsWith(cmd, "START_SD:")) {
        handleStartSD(num, args(cmd));
        return;
    }
    if (cmd == "STOP_SD") {
        handleStopSD(num);
        return;
    }

    // ── Scanning / Mapping ───────────────────────────────────────────────────
    if (startsWith(cmd, "SCAN_PLANT:")) {
        handleScanPlant(num, args(cmd));
        return;
    }
    if (startsWith(cmd, "SCAN_NPK:")) {
        String a = args(cmd);
        int c = a.indexOf(':');
        float sX = a.substring(0, c).toFloat();
        float sY = a.substring(c+1).toFloat();
        handleScanNpk(num, sX, sY);
        return;
    }
    if (startsWith(cmd, "SCAN_FULL:")) {
        handleScanFull(num, args(cmd));
        return;
    }
    if (cmd == "DIP_ALL_PLANTS") {
        handleDipAllPlants(num);
        return;
    }
    if (cmd == "AUTO_FARM") {
        handleAutonomousFarming(num);
        return;
    }
    if (startsWith(cmd, "AUTO_DETECT_PLANTS:")) {
        handleAutoDetectPlants(num, args(cmd));
        return;
    }
    if (cmd == "UPLOAD_SCAN") {
        handleScanUpload(num);
        return;
    }

    // ── Plant Registry ────────────────────────────────────────────────────────
    if (startsWith(cmd, "REGISTER_PLANT:")) {
        handleRegisterPlant(num, args(cmd));
        return;
    }
    if (startsWith(cmd, "CONFIRM_PLANT:")) {
        handleConfirmPlant(num, args(cmd));
        return;
    }
    if (startsWith(cmd, "REJECT_PLANT:")) {
        handleRejectPlant(num, args(cmd));
        return;
    }
    if (cmd == "CLEAR_PLANTS") {
        handleClearPlants(num);
        return;
    }
    if (startsWith(cmd, "DELETE_PLANT:")) {
        handleDeletePlant(num, args(cmd));
        return;
    }
    if (cmd == "GET_PLANT_MAP") {
        broadcastPlantMap(num);
        return;
    }

    // ── Farming Routine ───────────────────────────────────────────────────────
    if (startsWith(cmd, "RUN_FARMING_CYCLE")) {
        RoutineConfig cfg;
        // Optional args: RUN_FARMING_CYCLE:zSensor:zCamera
        String a = args(cmd);
        int c = a.indexOf(':');
        if (c >= 0) {
            cfg.zSensorHeight = a.substring(0, c).toFloat();
            cfg.zCameraHeight = a.substring(c+1).toFloat();
        }
        handleFarmingCycle(num, cfg);
        return;
    }

    // ── NPK ───────────────────────────────────────────────────────────────────
    if (cmd == "GET_NPK") {
        AgriLog(TAG_CMD, LEVEL_INFO, "Offloading NPK Dip to Brain (Core 1)");
        startRoutine(7); // Trigger executeNpkDip() in background task
        return;
    }
    if (startsWith(cmd, "NPK_HISTORY:")) {
        String a = args(cmd);
        int c = a.indexOf(':');
        if (c >= 0) {
            int gx = a.substring(0, c).toInt();
            int gy = a.substring(c+1).toInt();
            npkSendHistory(num, gx, gy);
        }
        return;
    }
    if (cmd == "NPK_HISTORY_ALL") {
        npkSendFullHistory(num);
        return;
    }

    // ── Environment ──────────────────────────────────────────────────────────
    if (startsWith(cmd, "SET_LOCATION:")) {
        String a = args(cmd);
        int c = a.indexOf(',');
        if (c >= 0) {
            float lat = a.substring(0, c).toFloat();
            float lon = a.substring(c+1).toFloat();
            setWeatherLocation(lat, lon);
            webSocket.sendTXT(num, "{\"evt\":\"LOCATION_SET\"}");
        } else {
            sendError(num, "Bad format. Expected SET_LOCATION:lat,lon");
        }
        return;
    }

    // ── Custom Operations ───────────────────────────────────────────────────
    if (startsWith(cmd, "WATER:")) {
        String a = args(cmd);
        int colons = 0;
        int idx = -1;
        while ((idx = a.indexOf(':', idx + 1)) >= 0) colons++;
        
        if (colons == 0) {
            float ml = a.toFloat();
            handleWater(num, sysState.getX(), sysState.getY(), ml);
        } else if (colons == 2) {
            int c1 = a.indexOf(':');
            int c2 = a.indexOf(':', c1 + 1);
            float x = a.substring(0, c1).toFloat();
            float y = a.substring(c1 + 1, c2).toFloat();
            float ml = a.substring(c2 + 1).toFloat();
            handleWater(num, x, y, ml);
        } else if (colons == 4) {
            int c1 = a.indexOf(':');
            int c2 = a.indexOf(':', c1 + 1);
            int c3 = a.indexOf(':', c2 + 1);
            int c4 = a.indexOf(':', c3 + 1);
            float x = a.substring(0, c1).toFloat();
            float y = a.substring(c1 + 1, c2).toFloat();
            float ml = a.substring(c2 + 1, c3).toFloat();
            float ox = a.substring(c3 + 1, c4).toFloat();
            float oy = a.substring(c4 + 1).toFloat();
            handleWater(num, x, y, ml, ox, oy);
        } else {
            sendError(num, "Bad format. Expected WATER:amount or WATER:x:y:amount or WATER:x:y:amount:ox:oy");
        }
        return;
    }

    if (startsWith(cmd, "FERTILIZE:")) {
        String a = args(cmd);
        int colons = 0;
        int idx = -1;
        while ((idx = a.indexOf(':', idx + 1)) >= 0) colons++;
        
        if (colons == 0) {
            float ml = a.toFloat();
            handleFertilize(num, sysState.getX(), sysState.getY(), ml);
        } else if (colons == 2) {
            int c1 = a.indexOf(':');
            int c2 = a.indexOf(':', c1 + 1);
            float x = a.substring(0, c1).toFloat();
            float y = a.substring(c1 + 1, c2).toFloat();
            float ml = a.substring(c2 + 1).toFloat();
            handleFertilize(num, x, y, ml);
        } else if (colons == 4) {
            int c1 = a.indexOf(':');
            int c2 = a.indexOf(':', c1 + 1);
            int c3 = a.indexOf(':', c2 + 1);
            int c4 = a.indexOf(':', c3 + 1);
            float x = a.substring(0, c1).toFloat();
            float y = a.substring(c1 + 1, c2).toFloat();
            float ml = a.substring(c2 + 1, c3).toFloat();
            float ox = a.substring(c3 + 1, c4).toFloat();
            float oy = a.substring(c4 + 1).toFloat();
            handleFertilize(num, x, y, ml, ox, oy);
        } else {
            sendError(num, "Bad format. Expected FERTILIZE:amount or FERTILIZE:x:y:amount or FERTILIZE:x:y:amount:ox:oy");
        }
        return;
    }

    if (cmd == "CLEAN_SENSORS") {
        handleCleanSensors(num);
        return;
    }

    if (startsWith(cmd, "SET_WATER_RATE:")) {
        float rate = args(cmd).toFloat();
        setWaterFlowRate(rate);
        webSocket.sendTXT(num, "{\"evt\":\"RATE_SET\",\"type\":\"water\",\"rate\":" + String(rate) + "}");
        return;
    }

    if (startsWith(cmd, "SET_FERT_RATE:")) {
        float rate = args(cmd).toFloat();
        setFertFlowRate(rate);
        webSocket.sendTXT(num, "{\"evt\":\"RATE_SET\",\"type\":\"fert\",\"rate\":" + String(rate) + "}");
        return;
    }

    if (startsWith(cmd, "SET_CAM_OFFSET:")) {
        float offset = args(cmd).toFloat();
        sysState.setCamOffset(offset);
        webSocket.sendTXT(num, "{\"evt\":\"CAM_OFFSET_SET\",\"value\":" + String(offset, 1) + "}");
        return;
    }

    if (cmd == "REBOOT") {
        AgriLog(TAG_CMD, LEVEL_WARN, "REBOOTING ESP32 via command #%d", num);
        webSocket.sendTXT(num, "{\"evt\":\"REBOOTING\"}");
        delay(500);
        ESP.restart();
        return;
    }

    // ── Fallback: raw G-code passthrough ────────────────────────────────────
    // Anything that didn't match a command is forwarded directly to the Nano.
    // This allows Flutter to send raw GRBL commands like $H, $X, G0 X10 etc.
    enqueueGrblCommand(cmd);
}

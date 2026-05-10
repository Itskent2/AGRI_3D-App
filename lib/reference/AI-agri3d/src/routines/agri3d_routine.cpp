/**
 * @file agri3d_routine.cpp
 * @brief Autonomous farming routine orchestrator.
 *
 * Per-plant cycle:
 *   Weather gate → Move → NPK read → Fertigation → Photo → Weed scan → Weed action
 *
 * Standalone routines:
 *   SCAN_NPK    → grid traverse, dip sensor at each cell
 *   SCAN_PHOTO  → grid traverse, take photo at each cell (delegates to plant_map)
 *   SCAN_FULL   → both at each cell
 */

#include "agri3d_routine.h"
#include "../core/AI_Agri3D.h"
#include "agri3d_plant_map.h"
#include "xgboost_model.h"
#include "agri3d_fuzzy.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>

// Forward declarations
void routineWorkerTask(void* pvParameters);

// ── Plant Registry ─────────────────────────────────────────────────────────
PlantPosition  plantRegistry[MAX_PLANTS];
int            plantCount = 0;

ScanParams globalScanParams;

// ── Candidate Buffer (awaiting Flutter confirmation) ───────────────────────
PlantCandidate candidateBuffer[MAX_CANDIDATES];
int            candidateCount = 0;

static Preferences _prefs;
static const char* NVS_ROUTINE_NS = "routine";
static float _waterFlowRate = 24.0f; // Default 24 ml/s
static float _fertFlowRate  = 2.0f;  // Default 2 ml/s

// ============================================================================
// PLANT REGISTRY — NVS PERSISTENCE
// ============================================================================

void savePlantRegistry() {
    _prefs.begin(NVS_ROUTINE_NS, false);
    _prefs.putInt("count", plantCount);
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;
        char key[8]; snprintf(key, sizeof(key), "p%02d", i);
        // Store as "x,y,name,diameter,N,P,K,dx,dy,cropType,ts"
        char val[128];
        snprintf(val, sizeof(val), "%.1f,%.1f,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%u",
                 plantRegistry[i].x, plantRegistry[i].y,
                 plantRegistry[i].name,
                 plantRegistry[i].diameter,
                 plantRegistry[i].targetN, plantRegistry[i].targetP,
                 plantRegistry[i].targetK,
                 plantRegistry[i].dx, plantRegistry[i].dy,
                 plantRegistry[i].cropType, plantRegistry[i].ts);
        _prefs.putString(key, val);
    }
    _prefs.end();
}

static void loadPlantRegistry() {
    _prefs.begin(NVS_ROUTINE_NS, true);
    plantCount = _prefs.getInt("count", 0);
    for (int i = 0; i < MAX_PLANTS; i++) {
        char key[8]; snprintf(key, sizeof(key), "p%02d", i);
        String val = _prefs.getString(key, "");
        if (val.length() == 0) { plantRegistry[i].active = false; continue; }

        // Parse "x,y,name,diameter,N,P,K,dx,dy,cropType,ts"
        int c[10];
        c[0] = val.indexOf(',');
        for (int j = 1; j < 10; j++) {
            c[j] = (c[j-1] >= 0) ? val.indexOf(',', c[j-1] + 1) : -1;
        }

        if (c[1] < 0) { plantRegistry[i].active = false; continue; } // Must at least have x,y,name

        plantRegistry[i].x        = val.substring(0, c[0]).toFloat();
        plantRegistry[i].y        = val.substring(c[0]+1, c[1]).toFloat();
        
        int nameEnd = (c[2] >= 0) ? c[2] : val.length();
        val.substring(c[1]+1, nameEnd).toCharArray(plantRegistry[i].name, 24);
        
        plantRegistry[i].diameter = (c[2] >= 0) ? val.substring(c[2]+1, c[3] >= 0 ? c[3] : val.length()).toFloat() : 0.0f;
        plantRegistry[i].targetN  = (c[3] >= 0) ? val.substring(c[3]+1, c[4] >= 0 ? c[4] : val.length()).toFloat() : 0.0f;
        plantRegistry[i].targetP  = (c[4] >= 0) ? val.substring(c[4]+1, c[5] >= 0 ? c[5] : val.length()).toFloat() : 0.0f;
        plantRegistry[i].targetK  = (c[5] >= 0) ? val.substring(c[5]+1, c[6] >= 0 ? c[6] : val.length()).toFloat() : 0.0f;
        plantRegistry[i].dx       = (c[6] >= 0) ? val.substring(c[6]+1, c[7] >= 0 ? c[7] : val.length()).toFloat() : 0.0f;
        plantRegistry[i].dy       = (c[7] >= 0) ? val.substring(c[7]+1, c[8] >= 0 ? c[8] : val.length()).toFloat() : 0.0f;
        plantRegistry[i].cropType = (c[8] >= 0) ? val.substring(c[8]+1, c[9] >= 0 ? c[9] : val.length()).toInt() : 0;
        plantRegistry[i].ts       = (c[9] >= 0) ? val.substring(c[9]+1).toInt() : 0;
        
        plantRegistry[i].active   = true;
    }
    _prefs.end();
    AgriLog(TAG_ROUTINE, LEVEL_INFO, "Loaded %d plants from NVS.", plantCount);
}

// ============================================================================
// PLANT REGISTRY — HELPERS
// ============================================================================

bool isKnownPlantPosition(float x, float y, float toleranceMm) {
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;
        float dx = plantRegistry[i].x - x;
        float dy = plantRegistry[i].y - y;
        float dist = sqrtf(dx*dx + dy*dy);
        
        // Use rosette diameter if available, otherwise fallback to tolerance
        float radius = (plantRegistry[i].diameter > 0) ? (plantRegistry[i].diameter / 2.0f) : toleranceMm;
        if (dist <= radius) return true;
    }
    return false;
}

void broadcastPlantMap(uint8_t clientNum) {
    // Send plants in chunks of 3 to stay well within the 2 KB WebSocket budget and avoid memory pool exhaustion.
    // Flutter reassembles them via PLANT_MAP_START / PLANT_CHUNK / PLANT_MAP_END.
    const int CHUNK_SIZE = 3;

    int totalActive = 0;
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (plantRegistry[i].active) totalActive++;
    }

    // Notify Flutter how many plants to expect
    {
        DynamicJsonDocument hdr(128);
        hdr["evt"]   = "PLANT_MAP_START";
        hdr["total"] = totalActive;
        String hdrOut; serializeJson(hdr, hdrOut);
        webSocket.sendTXT(clientNum, hdrOut);
    }

    int chunkIdx = 0;
    int plantIdx = 0;
    DynamicJsonDocument chunk(2048);
    chunk["evt"] = "PLANT_CHUNK";
    JsonArray arr = chunk.createNestedArray("plants");

    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;

        JsonObject p = arr.createNestedObject();
        p["idx"]      = i;
        p["x"]        = plantRegistry[i].x;
        p["y"]        = plantRegistry[i].y;
        p["name"]     = plantRegistry[i].name;
        p["diameter"] = plantRegistry[i].diameter;
        p["tN"]       = plantRegistry[i].targetN;
        p["tP"]       = plantRegistry[i].targetP;
        p["tK"]       = plantRegistry[i].targetK;
        p["dx"]       = plantRegistry[i].dx;
        p["dy"]       = plantRegistry[i].dy;
        p["c"]        = plantRegistry[i].cropType;
        p["ts"]       = plantRegistry[i].ts;

        plantIdx++;
        bool isLast = (plantIdx == totalActive);

        if (plantIdx % CHUNK_SIZE == 0 || isLast) {
            chunk["chunk"] = chunkIdx++;
            String out; serializeJson(chunk, out);
            webSocket.sendTXT(clientNum, out);
            delay(10); // brief yield so TCP stack can flush

            // Reset for next chunk
            chunk.clear();
            chunk["evt"] = "PLANT_CHUNK";
            arr = chunk.createNestedArray("plants");
        }
    }

    // Signal Flutter that all chunks have been sent
    DynamicJsonDocument done(64);
    done["evt"]   = "PLANT_MAP_END";
    done["total"] = totalActive;
    String doneOut; serializeJson(done, doneOut);
    webSocket.sendTXT(clientNum, doneOut);
}

// ============================================================================
// FERTIGATION HELPERS
// ============================================================================

/**
 * Decide and execute fertigation for one plant based on soil reading.
 * Returns immediately if weather gated or operation is not enabled.
 */
static void performFertigation(uint8_t clientNum, const PlantPosition& plant,
                                const SoilReading& soil, const RoutineConfig& cfg) {
    // Weather gate check (isWeatherGated set by agri3d_environment)
    if (sysState.getEnvironment() != ENV_CLEAR) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"FERTIGATION_SKIP\",\"reason\":\"WEATHER_GATED\"}");
        return;
    }

    // ── Water ───────────────────────────────────────────────────────────────
    if (cfg.doWatering && soil.moisture < 40.0f) {
        // TODO: Make threshold configurable per plant
        AgriLog(TAG_FERT, LEVEL_INFO, "Watering: M7 (pump on)");
        enqueueGrblCommand("M7");
        delay(3000); // TODO: Calculate duration from deficit
        enqueueGrblCommand("M9"); // Pump off
        webSocket.sendTXT(clientNum, "{\"evt\":\"WATERED\"}");
    }

    // ── Fertilizer ──────────────────────────────────────────────────────────
    if (cfg.doFertigation) {
        float fertMl = 0.0f;

        // User-set target takes priority
        if (plant.targetN > 0 || plant.targetP > 0 || plant.targetK > 0) {
            // Simple proportional: if all NPK are within 90% of target, skip
            bool needsNpk = (soil.n < plant.targetN * 0.9f) ||
                            (soil.p < plant.targetP * 0.9f) ||
                            (soil.k < plant.targetK * 0.9f);
            if (needsNpk) fertMl = 50.0f; // Default 50mL when user-set targets
        }

        // XGBoost model override (Using the 14MB C model)
        // Expected features: N, P, K, temperature, humidity, pH
        double xgb_features[6] = {
            (double)soil.n, (double)soil.p, (double)soil.k, 
            25.0, // Default Temperature
            60.0, // Default Humidity
            6.5   // Default pH
        };
        double xgb_prediction = score(xgb_features);
        
        // If the XGBoost model outputs a valid prediction, override the default
        if (xgb_prediction > 0) {
            fertMl = (float)xgb_prediction;
        }

        if (fertMl > 0) {
            String fertCmd = "M7 ml" + String((int)fertMl);
            enqueueGrblCommand(fertCmd);
            AgriLog(TAG_FERT, LEVEL_SUCCESS, "Fertilizing: %s (%.0f mL)",
                          fertCmd.c_str(), fertMl);

            StaticJsonDocument<96> doc;
            doc["evt"]  = "FERTILIZED";
            doc["ml"]   = fertMl;
            doc["plant"] = plant.name;
            String out; serializeJson(doc, out);
            webSocket.sendTXT(clientNum, out);
        }
    }
}

// ============================================================================
// WEED DETECTION + ACTUATION
// ============================================================================

static void performWeedAction(uint8_t clientNum, float captureX, float captureY,
                               const uint8_t* jpegBuf, size_t jpegLen) {
    // TODO(Luna): Replace this stub with actual AI weed detection
    //
    // int weedCount = aiDetectWeeds(jpegBuf, jpegLen, weedList, MAX_WEEDS);
    //
    // For now, this is a documented stub that shows exactly how the
    // actuation logic should work once Luna's model is integrated.

    // =========================================================================
    // TODO(Luna): WEED DETECTION STUB
    // =========================================================================
    // WeedCoord weedList[10];
    // int weedCount = aiDetectWeeds(jpegBuf, jpegLen, weedList, 10);
    // if (weedCount == 0) return;
    //
    // for (int i = 0; i < weedCount; i++) {
    //     float wx = captureX + weedList[i].mmX;
    //     float wy = captureY + weedList[i].mmY;
    //
    //     // SAFETY: Never weed a known plant position
    //     if (isKnownPlantPosition(wx, wy, 50.0f)) {
    //         Serial.printf("[WEED] Skipping (%.1f, %.1f) — known plant!\n", wx, wy);
    //         webSocket.sendTXT(clientNum,
    //             "{\"evt\":\"WEED_SKIP\",\"reason\":\"PLANT_OVERLAP\"}");
    //         continue;
    //     }
    //
    //     // Move weeder to weed coordinate
    //     NanoSerial.printf("G0 X%.1f Y%.1f F%d\n", wx, wy, GRBL_DEFAULT_FEEDRATE);
    //     waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    //
    //     // TODO(Luna): Actuate weeder (custom M-code)
    //     // NanoSerial.println("M105"); // Example weeder actuate
    //
    //     StaticJsonDocument<128> doc;
    //     doc["evt"] = "WEED_REMOVED";
    //     doc["x"]   = wx;  doc["y"] = wy;
    //     String out; serializeJson(doc, out);
    //     webSocket.sendTXT(clientNum, out);
    // }
    // =========================================================================

    (void)clientNum; (void)captureX; (void)captureY;
    (void)jpegBuf;   (void)jpegLen;
    AgriLog(TAG_WEED, LEVEL_INFO, "Detection stub — TODO(Luna)");
}

// ============================================================================
// MAIN FARMING CYCLE
// ============================================================================

void handleFarmingCycle(uint8_t clientNum, const RoutineConfig& cfg) {
    if (plantCount == 0) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"CYCLE_ERROR\",\"reason\":\"No plants registered\"}");
        return;
    }

    // Guard against running while busy
    if (sysState.getOperation() != OP_IDLE) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"CYCLE_ERROR\",\"reason\":\"System busy\"}");
        return;
    }

    // ── Weather gate check ────────────────────────────────────────────────
    if (sysState.getEnvironment() != ENV_CLEAR) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"CYCLE_SKIP\",\"reason\":\"WEATHER_GATED\"}");
        AgriLog(TAG_ENV, LEVEL_WARN, "Cycle skipped: weather gate active");
        return;
    }

    // ── Broadcast cycle start ─────────────────────────────────────────────
    {
        StaticJsonDocument<96> doc;
        doc["evt"]   = "CYCLE_START";
        doc["total"] = plantCount;
        String out; serializeJson(doc, out);
        webSocket.sendTXT(clientNum, out);
    }

    bool streamWasActive = sysState.isStreaming();
    sysState.setStreaming(false);
    sysState.setOperation(OP_HOMING);

    // ── Pre-cycle homing ──────────────────────────────────────────────────
    enqueueGrblCommand("$HX"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    enqueueGrblCommand("$HY"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    requestMachineDimensions();
    delay(1000);

    int plantsDone = 0;

    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;
        if (sysState.getFlutter() == FLUTTER_DISCONNECTED) break; // Abort if app disconnected

        // Pause routine if live streaming is active
        if (sysState.isStreaming()) {
            AgriLog(TAG_ROUTINE, LEVEL_WARN, "Live stream active — pausing farming routine.");
            while (sysState.isStreaming()) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (sysState.getFlutter() == FLUTTER_DISCONNECTED) break;
            }
            AgriLog(TAG_ROUTINE, LEVEL_INFO, "Live stream stopped — resuming farming routine.");
        }

        PlantPosition& plant = plantRegistry[i];
        
        // 1. Weather Gate
        if (isRaining()) {
            AgriLog(TAG_ROUTINE, LEVEL_WARN, "⛈ Skipping plant because it's raining.");
            return; 
        }

        AgriLog(TAG_ROUTINE, LEVEL_INFO, "Starting cycle for plant: %s", plant.name);
        plantsDone++;

        // ── Broadcast current plant ───────────────────────────────────────
        {
            StaticJsonDocument<128> doc;
            doc["evt"]  = "CYCLE_PLANT";
            doc["idx"]  = plantsDone;
            doc["name"] = plant.name;
            doc["x"]    = plant.x;
            doc["y"]    = plant.y;
            String out; serializeJson(doc, out);
            webSocket.sendTXT(clientNum, out);
        }

        sysState.setOperation(OP_SD_RUNNING); // Reuse as "routine running" state for now

        // ── STEP 1: Move to plant ─────────────────────────────────────────
        char moveCmd[48];
        snprintf(moveCmd, sizeof(moveCmd), "G0 X%.1f Y%.1f F%d", 
                 plant.x, plant.y, GRBL_DEFAULT_FEEDRATE);
        enqueueGrblCommand(moveCmd);
        waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);

        // ── STEP 2: NPK read ─────────────────────────────────────────────
        // TODO: Lower Z to cfg.zSensorHeight when Z is fixed
        // enqueueGrblCommand(String("G0 Z") + String(cfg.zSensorHeight) + " F500");
        // waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
        npkReadNow();
        SoilReading soil = latestSoil;

        // ── STEP 3: Fertigation ───────────────────────────────────────────
        // TODO: Raise Z to safe height first when Z is fixed
        performFertigation(clientNum, plant, soil, cfg);

        // ── STEP 4: Raise Z + Photo ───────────────────────────────────────
        // TODO: NanoSerial.printf("G0 Z%.1f F500\n", cfg.zCameraHeight);
        // TODO: waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
        sysState.setOperation(OP_SCANNING);

        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            // Send photo with plant metadata
            StaticJsonDocument<160> meta;
            meta["evt"]   = "FRAME_META";
            meta["idx"]   = plantsDone;
            meta["total"] = plantCount;
            meta["x"]     = sysState.getX();
            meta["y"]     = sysState.getY();
            meta["z"]     = sysState.getZ();
            meta["plant"] = plant.name;
            String metaStr; serializeJson(meta, metaStr);
            webSocket.sendTXT(clientNum, metaStr);
            webSocket.sendBIN(clientNum, fb->buf, fb->len);

            // ── STEP 5: Weed detection ─────────────────────────────────────
            if (cfg.doWeedScan) {
                // 4. Weed Detection (AI Hook)
                AiResult ai = aiAnalyzeFrame(sysState.pendingFrame, sysState.pendingFrameLen);
                if (ai.foundWeed && ai.confidence > 0.8f) {
                    AgriLog(TAG_WEED, LEVEL_WARN, "⚠️ Weed detected at (+%d, +%d)! Taking action...", 
                                  ai.xOffset, ai.yOffset);
                    // TODO: Move to relative offset and activate weed tool
                }
                performWeedAction(clientNum, sysState.getX(), sysState.getY(),
                                   fb->buf, fb->len);
            }

            esp_camera_fb_return(fb);
        }
    }

    // ── Return to origin ──────────────────────────────────────────────────
    NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);

    sysState.setOperation(OP_IDLE);
    if (streamWasActive) sysState.setStreaming(true);

    StaticJsonDocument<96> doc;
    doc["evt"]   = "CYCLE_COMPLETE";
    doc["done"]  = plantsDone;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
    AgriLog(TAG_SCAN, LEVEL_SUCCESS, "Cycle complete. %d plants serviced.", plantsDone);
}

// ============================================================================
// STANDALONE ROUTINES
// ============================================================================

void handleScanNpk(uint8_t clientNum, float stepX, float stepY) {
    if (!machineDim.valid) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"SCAN_ERROR\",\"reason\":\"Machine not homed\"}");
        return;
    }

    int cols = (int)(machineDim.maxX / stepX) + 1;
    int rows = (int)(machineDim.maxY / stepY) + 1;
    int total = cols * rows;

    sysState.setStreaming(false);
    sysState.setOperation(OP_HOMING);
    enqueueGrblCommand("$HX"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    enqueueGrblCommand("$HY"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    sysState.setOperation(OP_SCANNING);

    StaticJsonDocument<96> doc;
    doc["evt"]   = "SCAN_NPK_START";
    doc["total"] = total;
    String startOut; serializeJson(doc, startOut);
    webSocket.sendTXT(clientNum, startOut);

    // Snake pattern
    for (int row = 0; row < rows; row++) {
        for (int colStep = 0; colStep < cols; colStep++) {
            int col = (row % 2 == 0) ? colStep : (cols - 1 - colStep);
            float tx = col * stepX;
            float ty = row * stepY;
            tx = min(tx, machineDim.maxX);
            ty = min(ty, machineDim.maxY);

            char moveCmd[48];
            snprintf(moveCmd, sizeof(moveCmd), "G0 X%.1f Y%.1f F%d", tx, ty, GRBL_DEFAULT_FEEDRATE);
            enqueueGrblCommand(moveCmd);
            waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
            delay(200); // Stabilise before reading
            npkReadNow(); // Broadcasts to Flutter automatically
        }
    }

    NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    sysState.setOperation(OP_IDLE);
    sysState.setStreaming(true);
    webSocket.sendTXT(clientNum, "{\"evt\":\"SCAN_NPK_COMPLETE\"}");
}

void handleScanPhoto(uint8_t clientNum, const String& params) {
    // Delegate entirely to agri3d_plant_map.cpp
    handleScanPlant(clientNum, params);
}

void handleScanFull(uint8_t clientNum, const String& params) {
    // Parse: "cols:rows:stepX:stepY:zHeight"
    int c1 = params.indexOf(':');
    int c2 = params.indexOf(':', c1+1);
    int c3 = params.indexOf(':', c2+1);
    int c4 = params.indexOf(':', c3+1);
    if (c4 < 0) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"SCAN_ERROR\",\"reason\":\"Bad SCAN_FULL params\"}");
        return;
    }

    int   cols    = params.substring(0, c1).toInt();
    int   rows    = params.substring(c1+1, c2).toInt();
    float stepX   = params.substring(c2+1, c3).toFloat();
    float stepY   = params.substring(c3+1, c4).toFloat();
    float zH      = params.substring(c4+1).toFloat();

    AgriLog(TAG_ROUTINE, LEVEL_INFO, "Enqueuing %dx%d SCAN_FULL to Brain Core", cols, rows);

    globalScanParams.clientNum = clientNum;
    globalScanParams.cols = cols;
    globalScanParams.rows = rows;
    globalScanParams.stepX = stepX;
    globalScanParams.stepY = stepY;
    globalScanParams.zHeight = zH;

    startRoutine(6); // ROUTINE_SCAN_FULL
}

void executeScanFull(const ScanParams& cfg) {
    int   cols    = cfg.cols;
    int   rows    = cfg.rows;
    float stepX   = cfg.stepX;
    float stepY   = cfg.stepY;
    float zH      = cfg.zHeight;
    uint8_t clientNum = cfg.clientNum;
    int   total   = cols * rows;

    sysState.setStreaming(false);
    sysState.setOperation(OP_HOMING);
    enqueueGrblCommand("$HX"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    enqueueGrblCommand("$HY"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    requestMachineDimensions(); delay(1000);
    sysState.setOperation(OP_SCANNING);

    StaticJsonDocument<128> doc;
    doc["evt"]   = "SCAN_FULL_START";
    doc["total"] = total;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);

    int frameIdx = 0;
    for (int row = 0; row < rows; row++) {
        for (int colStep = 0; colStep < cols; colStep++) {
            int col = (row % 2 == 0) ? colStep : (cols - 1 - colStep);
            float tx = min(col * stepX, machineDim.maxX);
            float ty = min(row * stepY, machineDim.maxY);
            frameIdx++;

            NanoSerial.printf("G0 X%.1f Y%.1f F%d\n", tx, ty, GRBL_DEFAULT_FEEDRATE);
            waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
            delay(200);

            npkReadNow();                                         // NPK sample
            captureFrameAtPosition(clientNum, frameIdx, total,    // Photo
                                    tx, ty);
        }
    }

done:
    NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    sysState.setOperation(OP_IDLE);
    sysState.setScanReadyForUpload(true); // Flag that SD has an index ready for upload
    sysState.setStreaming(true);

    StaticJsonDocument<64> done_doc;
    done_doc["evt"]   = "SCAN_FULL_COMPLETE";
    done_doc["total"] = frameIdx;
    String doneOut; serializeJson(done_doc, doneOut);
    webSocket.sendTXT(clientNum, doneOut);
}

// ============================================================================
// PLANT REGISTRY COMMANDS
// ============================================================================

void handleRegisterPlant(uint8_t clientNum, const String& params) {
    // Expect JSON payload: {"name":"...","x":0,"y":0,"r":0,"dx":0,"dy":0,"c":1,"ts":0,"tN":0,"tP":0,"tK":0}
    StaticJsonDocument<256> req;
    DeserializationError err = deserializeJson(req, params);
    if (err) {
        webSocket.sendTXT(clientNum, "{\"evt\":\"PLANT_ERROR\",\"reason\":\"Bad JSON format\"}");
        return;
    }

    float x = req["x"] | 0.0f;
    float y = req["y"] | 0.0f;
    String name = req["name"] | "Plant";
    float diameter = req["r"] | 0.0f;
    float dx = req["dx"] | 0.0f;
    float dy = req["dy"] | 0.0f;
    int cropType = req["c"] | 0;
    uint32_t ts = req["ts"] | 0;
    float tN = req["tN"] | 0.0f;
    float tP = req["tP"] | 0.0f;
    float tK = req["tK"] | 0.0f;

    // Find empty slot or update existing
    int slot = -1;
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) { 
            if (slot == -1) slot = i; 
            continue; 
        }
        // If same position within 10mm, update it
        if (fabsf(plantRegistry[i].x - x) < 10 &&
            fabsf(plantRegistry[i].y - y) < 10) {
            slot = i; break;
        }
    }

    if (slot < 0) {
        webSocket.sendTXT(clientNum, "{\"evt\":\"PLANT_ERROR\",\"reason\":\"Registry full\"}");
        return;
    }

    plantRegistry[slot].x        = x;
    plantRegistry[slot].y        = y;
    plantRegistry[slot].diameter = diameter;
    plantRegistry[slot].targetN  = tN;
    plantRegistry[slot].targetP  = tP;
    plantRegistry[slot].targetK  = tK;
    plantRegistry[slot].dx       = dx;
    plantRegistry[slot].dy       = dy;
    plantRegistry[slot].cropType = cropType;
    plantRegistry[slot].ts       = ts;
    plantRegistry[slot].active   = true;
    name.toCharArray(plantRegistry[slot].name, 24);
    
    plantCount = 0;
    for (int i = 0; i < MAX_PLANTS; i++) if (plantRegistry[i].active) plantCount++;

    savePlantRegistry();

    StaticJsonDocument<256> doc;
    doc["evt"]      = "PLANT_REGISTERED";
    doc["slot"]     = slot;
    doc["name"]     = plantRegistry[slot].name;
    doc["x"]        = x;
    doc["y"]        = y;
    doc["diameter"] = diameter;
    doc["dx"]       = dx;
    doc["dy"]       = dy;
    doc["c"]        = cropType;
    doc["ts"]       = ts;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
    
    // Broadcast updated map to everyone
    broadcastPlantMap(clientNum);
    
    AgriLog(TAG_ROUTINE, LEVEL_SUCCESS, "Plant registered: %s (%.1f, %.1f) D=%.1f",
                  plantRegistry[slot].name, x, y, diameter);
}

void handleDeletePlant(uint8_t clientNum, const String& params) {
    int idx = params.toInt();
    if (idx < 0 || idx >= MAX_PLANTS) {
        webSocket.sendTXT(clientNum, "{\"evt\":\"PLANT_ERROR\",\"reason\":\"Invalid index\"}");
        return;
    }

    if (plantRegistry[idx].active) {
        plantRegistry[idx].active = false;
        plantCount--;
        savePlantRegistry();
        
        webSocket.sendTXT(clientNum, "{\"evt\":\"PLANT_DELETED\",\"idx\":" + String(idx) + "}");
        broadcastPlantMap(clientNum);
        AgriLog(TAG_ROUTINE, LEVEL_SUCCESS, "Plant #%d deleted.", idx);
    }
}

void handleClearPlants(uint8_t clientNum) {
    memset(plantRegistry, 0, sizeof(plantRegistry));
    plantCount = 0;
    _prefs.begin(NVS_ROUTINE_NS, false);
    _prefs.clear();
    _prefs.end();
    webSocket.sendTXT(clientNum, "{\"evt\":\"PLANTS_CLEARED\"}");
    AgriLog(TAG_ROUTINE, LEVEL_SUCCESS, "Plant registry cleared.");
}

// ============================================================================
// AUTO DETECT PLANTS
// ============================================================================

void handleAutoDetectPlants(uint8_t clientNum, const String& params) {
    // Parse: "cols:rows:stepX:stepY:zHeight" (same format as SCAN_FULL)
    int c1 = params.indexOf(':'), c2 = params.indexOf(':',c1+1);
    int c3 = params.indexOf(':',c2+1), c4 = params.indexOf(':',c3+1);
    if (c4 < 0) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"DETECT_ERROR\",\"reason\":\"Bad params\"}");
        return;
    }
    int   cols  = params.substring(0,c1).toInt();
    int   rows  = params.substring(c1+1,c2).toInt();
    float stepX = params.substring(c2+1,c3).toFloat();
    float stepY = params.substring(c3+1,c4).toFloat();
    float zH = params.substring(c4+1).toFloat(); // Use Z height

    int total = cols * rows;
    AgriLog(TAG_ROUTINE, LEVEL_INFO, "Enqueuing %dx%d auto-detect (%d frames) to Brain Core",
                cols, rows, total);

    globalScanParams.clientNum = clientNum;
    globalScanParams.cols = cols;
    globalScanParams.rows = rows;
    globalScanParams.stepX = stepX;
    globalScanParams.stepY = stepY;
    globalScanParams.zHeight = zH;

    startRoutine(4); // ROUTINE_AUTO_DETECT
}

void executeAutoDetectPlants(const ScanParams& cfg) {
    int total = cfg.cols * cfg.rows;
    uint8_t clientNum = cfg.clientNum;

    // Clear previous candidates
    memset(candidateBuffer, 0, sizeof(candidateBuffer));
    candidateCount = 0;

    bool streamWasActive = sysState.isStreaming();
    sysState.setStreaming(false);
    sysState.setOperation(OP_HOMING);
    
    // Home all axes
    enqueueGrblCommand("$H");
    waitForGrblIdle(SCAN_HOME_TIMEOUT_MS * 3);
    requestMachineDimensions(); delay(1000);
    
    sysState.setOperation(OP_SCANNING);

    // Move Z to requested height
    char zMove[32];
    snprintf(zMove, sizeof(zMove), "G0 Z%.1f F500", cfg.zHeight);
    enqueueGrblCommand(zMove);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);

    StaticJsonDocument<96> startDoc;
    startDoc["evt"]   = "DETECT_START";
    startDoc["total"] = total;
    String startOut; serializeJson(startDoc, startOut);
    webSocket.sendTXT(clientNum, startOut);

    int frameIdx = 0;
    for (int row = 0; row < cfg.rows; row++) {
        for (int colStep = 0; colStep < cfg.cols; colStep++) {
            if (sysState.getFlutter() == FLUTTER_DISCONNECTED) goto detect_done;
            int col = (row % 2 == 0) ? colStep : (cfg.cols - 1 - colStep);
            float tx = min(col * cfg.stepX, machineDim.maxX);
            float ty = min(row * cfg.stepY, machineDim.maxY);
            frameIdx++;

            NanoSerial.printf("G0 X%.1f Y%.1f F%d\n", tx, ty, GRBL_DEFAULT_FEEDRATE);
            waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
            delay(200);

            camera_fb_t* fb = esp_camera_fb_get();
            if (!fb) continue;

            // ================================================================
            // TODO(Luna): AI Plant Detection
            // ================================================================
            // Replace this section with actual plant detection model.
            //
            // Example interface:
            //   float confidence = aiDetectPlant(fb->buf, fb->len);
            //   bool isPlant = (confidence >= PLANT_DETECT_THRESHOLD);
            //
            // For now: every frame is treated as a candidate for testing.
            // Luna should change `isPlant` logic below.
            // ================================================================
            bool  isPlant    = false;    // TODO(Luna): set from AI model output
            float confidence = 0.0f;     // TODO(Luna): set from model confidence
            // ================================================================

            if (isPlant && candidateCount < MAX_CANDIDATES) {
                // Store in buffer for later CONFIRM/REJECT
                candidateBuffer[candidateCount] = {
                    .x          = tx,
                    .y          = ty,
                    .confidence = confidence,
                    .pending    = true
                };
                candidateCount++;

                // Send candidate event + thumbnail JPEG to Flutter
                StaticJsonDocument<160> cDoc;
                cDoc["evt"]        = "PLANT_CANDIDATE";
                cDoc["idx"]        = frameIdx;
                cDoc["total"]      = total;
                cDoc["x"]          = tx;
                cDoc["y"]          = ty;
                cDoc["confidence"] = confidence;
                cDoc["pendingId"]  = candidateCount - 1; // index in candidateBuffer
                String cOut; serializeJson(cDoc, cOut);
                webSocket.sendTXT(clientNum, cOut);
                // Send JPEG thumbnail immediately after
                webSocket.sendBIN(clientNum, fb->buf, fb->len);

                AgriLog(TAG_SYSTEM, LEVEL_INFO, "Candidate #%d at (%.1f, %.1f) conf=%.2f",
                              candidateCount, tx, ty, confidence);
            }

            // Always send the frame for Flutter to display the scan progress
            // even if not a plant (so user can see what the camera sees)
            StaticJsonDocument<128> fDoc;
            fDoc["evt"]     = "DETECT_FRAME";
            fDoc["idx"]     = frameIdx;
            fDoc["total"]   = total;
            fDoc["x"]       = tx;
            fDoc["y"]       = ty;
            fDoc["isPlant"] = isPlant;
            String fOut; serializeJson(fDoc, fOut);
            webSocket.sendTXT(clientNum, fOut);
            webSocket.sendBIN(clientNum, fb->buf, fb->len);

            esp_camera_fb_return(fb);
        }
    }

detect_done:
    NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    sysState.setOperation(OP_IDLE);
    if (streamWasActive) sysState.setStreaming(true);

    // Tell Flutter how many candidates need review
    StaticJsonDocument<96> doneDoc;
    doneDoc["evt"]        = "DETECTION_COMPLETE";
    doneDoc["scanned"]    = frameIdx;
    doneDoc["candidates"] = candidateCount;
    String doneOut; serializeJson(doneDoc, doneOut);
    webSocket.sendTXT(clientNum, doneOut);

    AgriLog(TAG_SYSTEM, LEVEL_SUCCESS, "Done. %d frames, %d candidates.",
                  frameIdx, candidateCount);
}

// ============================================================================
// CONFIRM / REJECT PLANT CANDIDATES
// ============================================================================

void handleConfirmPlant(uint8_t clientNum, const String& params) {
    // Format: "x:y:name"  (name optional)
    int c1 = params.indexOf(':');
    int c2 = params.indexOf(':', c1+1);
    if (c1 < 0) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"PLANT_ERROR\",\"reason\":\"Bad CONFIRM format\"}");
        return;
    }
    float x = params.substring(0, c1).toFloat();
    float y = params.substring(c1+1, (c2>=0) ? c2 : params.length()).toFloat();
    String name = (c2 >= 0) ? params.substring(c2+1) : "Plant";
    if (name.length() == 0) name = "Plant";

    // Mark the matching candidate as no longer pending
    for (int i = 0; i < MAX_CANDIDATES; i++) {
        if (!candidateBuffer[i].pending) continue;
        if (fabsf(candidateBuffer[i].x - x) < 10 &&
            fabsf(candidateBuffer[i].y - y) < 10) {
            candidateBuffer[i].pending = false;
            break;
        }
    }

    // Add to plant registry (reuse existing handler logic)
    String regParams = String(x, 1) + ":" + String(y, 1) + ":" + name;
    handleRegisterPlant(clientNum, regParams);
    // Mark as AI-detected
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;
        if (fabsf(plantRegistry[i].x - x) < 10 &&
            fabsf(plantRegistry[i].y - y) < 10) {
            plantRegistry[i].aiDetected = true;
            break;
        }
    }
    savePlantRegistry();
}

void handleRejectPlant(uint8_t clientNum, const String& params) {
    // Format: "x:y"
    int c1 = params.indexOf(':');
    if (c1 < 0) return;
    float x = params.substring(0, c1).toFloat();
    float y = params.substring(c1+1).toFloat();

    for (int i = 0; i < MAX_CANDIDATES; i++) {
        if (!candidateBuffer[i].pending) continue;
        if (fabsf(candidateBuffer[i].x - x) < 10 &&
            fabsf(candidateBuffer[i].y - y) < 10) {
            candidateBuffer[i].pending = false;
            AgriLog(TAG_SYSTEM, LEVEL_INFO, "Rejected candidate at (%.1f, %.1f)", x, y);
            break;
        }
    }
    StaticJsonDocument<64> doc;
    doc["evt"] = "PLANT_REJECTED";
    doc["x"]   = x;  doc["y"] = y;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
}

// ── Routine Worker Task ───────────────────────────────────────────────────
static TaskHandle_t _routineTaskHandle = NULL;
static uint32_t     _pendingRoutine = 0; // Bitmask of routines to run

void routineInit() {
    loadPlantRegistry();
    checkExistingScan();
    
    _prefs.begin(NVS_ROUTINE_NS, true);
    _waterFlowRate = _prefs.getFloat("w_rate", 24.0f);
    _fertFlowRate  = _prefs.getFloat("f_rate", 2.0f);
    _prefs.end();
    
    // Create the Routine Task (The Brain) on Core 1
    xTaskCreatePinnedToCore(
        routineWorkerTask,
        "RoutineTask",
        8192,
        NULL,
        2, // Mid priority
        &_routineTaskHandle,
        1 // Pinned to Core 1
    );
    AgriLog(TAG_ROUTINE, LEVEL_SUCCESS, "Brain initialized on Core 1.");
}

void routineWorkerTask(void* pvParameters) {
    for (;;) {
        // Wait for a notification to start a routine
        uint32_t routineType;
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &routineType, portMAX_DELAY) == pdTRUE) {
            
            // 1. Weather Gate: Check for Rain
            if (isRaining()) {
                AgriLog(TAG_ROUTINE, LEVEL_WARN, "⛈ Rain detected! Gating autonomous actions.");
                sysState.setOperation(OP_RAIN_PAUSED);
                webSocket.broadcastTXT("{\"evt\":\"WEATHER_GATE\",\"status\":\"RAINING\",\"action\":\"SLEEP\"}");
                
                // Wait for rain to stop or user override (simplified for now)
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue; 
            }

            // 2. Execute Routine
            if (routineType == 1) { // Farming Cycle
                RoutineConfig dummyCfg;
                handleFarmingCycle(activeClientNum, dummyCfg);
            } else if (routineType == 2) { // Full Grid Scan (placeholder)
                // handleFullGridScan();
            } else if (routineType == 3) { // Scan Plant Bed — Phase 1: SD save
                executeScanPlant(globalScanParams);
            } else if (routineType == 4) { // Auto Detect Plants
                executeAutoDetectPlants(globalScanParams);
            } else if (routineType == 5) { // Scan Upload — Phase 2: SD → Flutter
                executeScanUpload(globalScanParams.clientNum);
            } else if (routineType == 6) { // Scan Full (NPK + Photo)
                executeScanFull(globalScanParams);
            } else if (routineType == 7) { // Standalone NPK Dip
                executeNpkDip();
            } else if (routineType == 8) { // Dip All Plants
                executeDipAllPlants(globalScanParams.clientNum);
            } else if (routineType == 9) { // Autonomous Master Farming
                executeAutonomousFarming(globalScanParams.clientNum);
            }
            
            sysState.setOperation(OP_IDLE);
            AgriLog(TAG_ROUTINE, LEVEL_SUCCESS, "Sequence complete.");
        }
    }
}

void startRoutine(uint32_t type) {
    if (_routineTaskHandle) {
        xTaskNotify(_routineTaskHandle, type, eSetValueWithOverwrite);
    }
}

// ── Flow Rate Calibration ──────────────────────────────────────────────────

void setWaterFlowRate(float rate) {
    _waterFlowRate = rate;
    _prefs.begin(NVS_ROUTINE_NS, false);
    _prefs.putFloat("w_rate", rate);
    _prefs.end();
}

void setFertFlowRate(float rate) {
    _fertFlowRate = rate;
    _prefs.begin(NVS_ROUTINE_NS, false);
    _prefs.putFloat("f_rate", rate);
    _prefs.end();
}

float getWaterFlowRate() { return _waterFlowRate; }
float getFertFlowRate() { return _fertFlowRate; }

// ── Custom Operations ───────────────────────────────────────────────────

void handleWater(uint8_t clientNum, float x, float y, float ml, float ox, float oy) {
    float tx = x + ox;
    float ty = y + oy;
    
    char moveCmd[48];
    snprintf(moveCmd, sizeof(moveCmd), "G0 X%.1f Y%.1f F%d", tx, ty, GRBL_DEFAULT_FEEDRATE);
    enqueueGrblCommand(moveCmd);
    
    enqueueGrblCommand("M100"); // Water ON
    float durationSec = ml / _waterFlowRate;
    char dwellCmd[32];
    snprintf(dwellCmd, sizeof(dwellCmd), "G4 P%.3f", durationSec);
    enqueueGrblCommand(dwellCmd);
    enqueueGrblCommand("M101"); // Water OFF
    
    StaticJsonDocument<128> doc;
    doc["evt"] = "WATER_COMPLETE";
    doc["x"] = tx;
    doc["y"] = ty;
    doc["ml"] = ml;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
}

void handleFertilize(uint8_t clientNum, float x, float y, float ml, float ox, float oy) {
    float tx = x + ox;
    float ty = y + oy;
    
    char moveCmd[48];
    snprintf(moveCmd, sizeof(moveCmd), "G0 X%.1f Y%.1f F%d", tx, ty, GRBL_DEFAULT_FEEDRATE);
    enqueueGrblCommand(moveCmd);
    
    enqueueGrblCommand("M102"); // Fert ON
    float durationSec = ml / _fertFlowRate;
    char dwellCmd[32];
    snprintf(dwellCmd, sizeof(dwellCmd), "G4 P%.3f", durationSec);
    enqueueGrblCommand(dwellCmd);
    enqueueGrblCommand("M103"); // Fert OFF
    
    StaticJsonDocument<128> doc;
    doc["evt"] = "FERTILIZE_COMPLETE";
    doc["x"] = tx;
    doc["y"] = ty;
    doc["ml"] = ml;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
}

void handleCleanSensors(uint8_t clientNum) {
    enqueueGrblCommand("G0 Z0 F500");
    
    char moveCmd[48];
    snprintf(moveCmd, sizeof(moveCmd), "G0 Y%.1f F%d", machineDim.maxY - 20, GRBL_DEFAULT_FEEDRATE);
    enqueueGrblCommand(moveCmd);
    
    enqueueGrblCommand("G0 X0 Y0 F1000");
    enqueueGrblCommand("G0 Z5 F500");
    
    enqueueGrblCommand("M104"); // Weeder ON
    enqueueGrblCommand("G2 X0 Y0 I10 J0 F500");
    enqueueGrblCommand("M105"); // Weeder OFF
    
    webSocket.sendTXT(clientNum, "{\"evt\":\"CLEAN_SENSORS_COMPLETE\"}");
}

void executeNpkDip() {
    AgriLog(TAG_ROUTINE, LEVEL_INFO, "Starting standalone NPK dip sequence...");
    sysState.setOperation(OP_NPK_DIP);

    // 1. Move Z down to 5mm depth (absolute) at half speed (F83)
    enqueueGrblCommand("G90 G0 Z5 F83"); // Half of previous F167
    if (!waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS)) {
        AgriLog(TAG_ROUTINE, LEVEL_ERR, "NPK Dip: Z-Down Timeout");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(5000)); // Increased to 5s based on your T90 test (Mean stabilization was 3.84s)

    // 2. Take reading with retries (infinite until success)
    int attempt = 1;
    bool success = false;
    while (!success) {
        success = npkReadNow();
        if (!success) {
            AgriLog(TAG_ROUTINE, LEVEL_WARN, "NPK read failed, retrying in 1s... (Attempt %d)", attempt++);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // 3. Move Z back up to clearance (150mm) at the same slow speed
    char upCmd[48];
    snprintf(upCmd, sizeof(upCmd), "G90 G0 Z150.0 F83"); // Same speed as dipping
    enqueueGrblCommand(upCmd);
    if (!waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS)) {
        AgriLog(TAG_ROUTINE, LEVEL_WARN, "NPK Dip: Z-Up Return Timeout");
    }

    AgriLog(TAG_ROUTINE, LEVEL_SUCCESS, "NPK standalone dip complete.");
}

void handleDipAllPlants(uint8_t clientNum) {
    if (!machineDim.valid) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"SCAN_ERROR\",\"reason\":\"Machine not homed\"}");
        return;
    }
    
    if (plantCount == 0) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"SCAN_ERROR\",\"reason\":\"No plants registered\"}");
        return;
    }

    AgriLog(TAG_ROUTINE, LEVEL_INFO, "Enqueuing DIP_ALL_PLANTS to Brain Core");
    globalScanParams.clientNum = clientNum;
    startRoutine(8); // ROUTINE_DIP_ALL_PLANTS
}

void executeDipAllPlants(uint8_t clientNum) {
    sysState.setStreaming(false);
    sysState.setOperation(OP_HOMING);
    enqueueGrblCommand("$H"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    requestMachineDimensions(); delay(1000);
    sysState.setOperation(OP_SCANNING);

    // ── Safe Z Clearance ────────────────────────────────────────────────────
    // Raise Z-axis immediately after homing to prevent dragging through the soil
    // The user requested exactly 150 for Z clearance.
    char zCmd[32];
    snprintf(zCmd, sizeof(zCmd), "G90 G0 Z150.0 F83");
    enqueueGrblCommand(zCmd);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    // ────────────────────────────────────────────────────────────────────────

    StaticJsonDocument<128> doc;
    doc["evt"]   = "DIP_ALL_START";
    doc["total"] = plantCount;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);

    int dippedCount = 0;
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;

        PlantPosition& plant = plantRegistry[i];
        // dx/dy are absolute machine coordinates for the dip point
        float tx = plant.dx;
        float ty = plant.dy;

        // Ensure we don't go out of bounds
        tx = min(tx, machineDim.maxX);
        ty = min(ty, machineDim.maxY);

        AgriLog(TAG_ROUTINE, LEVEL_INFO, "Moving to dip point for %s at X:%.1f Y:%.1f", plant.name, tx, ty);
        char moveCmd[48];
        snprintf(moveCmd, sizeof(moveCmd), "G0 X%.1f Y%.1f F%d", tx, ty, GRBL_DEFAULT_FEEDRATE);
        enqueueGrblCommand(moveCmd);
        waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
        delay(200);

        // Perform the dip
        executeNpkDip();

        dippedCount++;
        StaticJsonDocument<128> progDoc;
        progDoc["evt"] = "DIP_ALL_PROGRESS";
        progDoc["idx"] = dippedCount;
        progDoc["total"] = plantCount;
        progDoc["name"] = plant.name;
        String progOut; serializeJson(progDoc, progOut);
        webSocket.sendTXT(clientNum, progOut);
    }

    NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    sysState.setOperation(OP_IDLE);
    sysState.setStreaming(true);

    StaticJsonDocument<64> done_doc;
    done_doc["evt"]   = "DIP_ALL_COMPLETE";
    done_doc["total"] = dippedCount;
    String doneOut; serializeJson(done_doc, doneOut);
    webSocket.sendTXT(clientNum, doneOut);
}

// ============================================================================
// AUTONOMOUS MASTER FARMING ROUTINE
// ============================================================================

void handleAutonomousFarming(uint8_t clientNum) {
    if (!machineDim.valid) {
        webSocket.sendTXT(clientNum, "{\"evt\":\"SCAN_ERROR\",\"reason\":\"Machine not homed\"}");
        return;
    }
    if (plantCount == 0) {
        webSocket.sendTXT(clientNum, "{\"evt\":\"SCAN_ERROR\",\"reason\":\"No plants registered\"}");
        return;
    }

    AgriLog(TAG_ROUTINE, LEVEL_INFO, "Enqueuing AUTONOMOUS_FARMING to Brain Core");
    globalScanParams.clientNum = clientNum;
    startRoutine(9); // ROUTINE_AUTO_FARM
}

void executeAutonomousFarming(uint8_t clientNum) {
    AgriLog(TAG_ROUTINE, LEVEL_INFO, "Starting Autonomous Master Farming Routine");
    sysState.setStreaming(false);
    
    // 1. Home Machine
    sysState.setOperation(OP_HOMING);
    enqueueGrblCommand("$H"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    requestMachineDimensions(); delay(1000);
    sysState.setOperation(OP_SCANNING);

    // ── Safe Z Clearance ────────────────────────────────────────────────────
    // Raise Z-axis immediately after homing to prevent dragging through the soil
    char zCmd[32];
    snprintf(zCmd, sizeof(zCmd), "G90 G0 Z150.0 F83");
    enqueueGrblCommand(zCmd);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    // ────────────────────────────────────────────────────────────────────────

    StaticJsonDocument<128> doc;
    doc["evt"]   = "AUTO_FARM_START";
    doc["total"] = plantCount;
    String out; serializeJson(doc, out);
    if (clientNum != 255) webSocket.sendTXT(clientNum, out);

    Agri3DFuzzyController flc;
    int processedCount = 0;

    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;
        PlantPosition& plant = plantRegistry[i];

        // Ensure within bounds
        float tx = min(plant.dx, machineDim.maxX);
        float ty = min(plant.dy, machineDim.maxY);

        AgriLog(TAG_ROUTINE, LEVEL_INFO, "Moving to %s (X:%.1f Y:%.1f)", plant.name, tx, ty);
        char moveCmd[48];
        snprintf(moveCmd, sizeof(moveCmd), "G0 X%.1f Y%.1f F%d", tx, ty, GRBL_DEFAULT_FEEDRATE);
        enqueueGrblCommand(moveCmd);
        waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
        delay(200);

        // 2. Data Acquisition (Dip Z-axis and read NPK)
        executeNpkDip();

        // Retrieve latest reading populated by executeNpkDip() -> npkReadNow()
        const SoilReading& r = latestSoil;
        if (!r.valid) {
            AgriLog(TAG_ROUTINE, LEVEL_WARN, "Skipping %s due to invalid NPK read", plant.name);
            continue;
        }

        // 3. Weather-Adaptive Gating
        float pPop = getWeatherPrecipProb();
        float cc   = getWeatherCloudCover();
        float rh   = getWeatherHumidity();
        bool isRainingNow = isPhysicalRainDetected();

        // If physically raining, abort immediately
        if (isRainingNow || pPop > 70.0f) {
            AgriLog(TAG_ROUTINE, LEVEL_ERR, "Raining (Phys: %d, Ppop: %.1f%%). Aborting Autonomous Farming!", isRainingNow, pPop);
            
            // Notify Flutter/Python about the abort decision
            StaticJsonDocument<128> gateDoc;
            gateDoc["evt"]    = "WEATHER_GATE";
            gateDoc["action"] = "ABORT";
            gateDoc["reason"] = isRainingNow ? "RAIN_SENSOR" : "HIGH_PPOP";
            gateDoc["ppop"]   = pPop;
            String out; serializeJson(gateDoc, out);
            webSocket.broadcastTXT(out);
            
            break; // Break the loop, go back to home
        }

        // Gating Variable Gf (0 = Conservative/Skip Irrigation, 1 = Normal)
        int Gf = 1;
        if (pPop >= 40.0f || (cc >= 60.0f && rh >= 70.0f)) {
            Gf = 0;
            AgriLog(TAG_ROUTINE, LEVEL_INFO, "Weather Gate Gf=0 (Cloudy/Humid/Rain chance). Irrigation scaled.");
            
            // Notify Flutter/Python about the gating decision
            StaticJsonDocument<128> gateDoc;
            gateDoc["evt"]    = "WEATHER_GATE";
            gateDoc["action"] = "SCALED";
            gateDoc["ppop"]   = pPop;
            String out; serializeJson(gateDoc, out);
            webSocket.broadcastTXT(out);
        }

        // 4. XGBoost Nutrient Prediction
        // Order: N, P, K, Temp, Moisture, pH
        double inputs[6];
        inputs[0] = r.n;
        inputs[1] = r.p;
        inputs[2] = r.k;
        inputs[3] = r.tempC;
        inputs[4] = r.moisture; // Substituting humidity with moisture as standard for soil models
        inputs[5] = r.ph;
        
        float predictedDosage = score(inputs);
        AgriLog(TAG_ROUTINE, LEVEL_INFO, "XGBoost Dosage Output: %.2f", predictedDosage);

        // 5. Mamdani FLC Evaluation
        float waterVolML = 0.0f;
        float fertVolML = 0.0f;
        flc.evaluate(r.moisture, r.ec, r.n, r.p, r.k, r.ph, predictedDosage, waterVolML, fertVolML);

        // Apply Weather Gating to water
        if (Gf == 0) {
            waterVolML *= 0.5f; // Reduce water by half if rain is highly probable
        }

        // 6. EC Safety Override
        const float EC_MAX = 1500.0f; // Threshold for extreme salinity
        if (r.ec >= EC_MAX) {
            AgriLog(TAG_ROUTINE, LEVEL_WARN, "EC Warning! %.1f >= %.1f. Flushing sequence engaged.", r.ec, EC_MAX);
            fertVolML = 0.0f; // Strictly zero fertilizer
            waterVolML += 50.0f; // Add 50ml flush
        }

        // 7. Actuation
        float wRate = getWaterFlowRate();
        float fRate = getFertFlowRate();
        
        float tWaterSec = (wRate > 0) ? (waterVolML / wRate) : 0;
        float tFertSec  = (fRate > 0) ? (fertVolML / fRate) : 0;

        AgriLog(TAG_ROUTINE, LEVEL_INFO, "Actuating: Water %.1fml (%.1fs), Fert %.1fml (%.1fs)", waterVolML, tWaterSec, fertVolML, tFertSec);

        // Actuate Fertilizer (M102 ON, M103 OFF)
        if (tFertSec > 0) {
            enqueueGrblCommand("M102"); 
            vTaskDelay(pdMS_TO_TICKS(tFertSec * 1000.0f));
            enqueueGrblCommand("M103");
            delay(100);
        }

        // Actuate Water (M100 ON, M101 OFF)
        if (tWaterSec > 0) {
            enqueueGrblCommand("M100");
            vTaskDelay(pdMS_TO_TICKS(tWaterSec * 1000.0f));
            enqueueGrblCommand("M101");
            delay(100);
        }

        processedCount++;

        StaticJsonDocument<128> progDoc;
        progDoc["evt"] = "AUTO_FARM_PROGRESS";
        progDoc["idx"] = processedCount;
        progDoc["total"] = plantCount;
        progDoc["name"] = plant.name;
        String progOut; serializeJson(progDoc, progOut);
        if (clientNum != 255) webSocket.sendTXT(clientNum, progOut);
    }

    // 8. Retraction & Loop complete (return Home)
    AgriLog(TAG_ROUTINE, LEVEL_INFO, "Autonomous Farming complete. Returning home.");
    NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    
    sysState.setOperation(OP_IDLE);
    sysState.setStreaming(true);

    StaticJsonDocument<64> doneDoc;
    doneDoc["evt"]   = "AUTO_FARM_COMPLETE";
    doneDoc["total"] = processedCount;
    String doneOut; serializeJson(doneDoc, doneOut);
    if (clientNum != 255) webSocket.sendTXT(clientNum, doneOut);
}

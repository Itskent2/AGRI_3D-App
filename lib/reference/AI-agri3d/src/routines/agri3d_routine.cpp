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
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>

// Forward declarations
void routineWorkerTask(void* pvParameters);

// ── Plant Registry ─────────────────────────────────────────────────────────
PlantPosition plantRegistry[MAX_PLANTS];
int           plantCount = 0;

// ── Candidate Buffer (awaiting Flutter confirmation) ───────────────────────
PlantCandidate candidateBuffer[MAX_CANDIDATES];
int            candidateCount = 0;

static Preferences _prefs;
static const char* NVS_ROUTINE_NS = "routine";
static float _waterFlowRate = 10.0f; // Default 10 ml/s
static float _fertFlowRate  = 10.0f; // Default 10 ml/s

// ============================================================================
// PLANT REGISTRY — NVS PERSISTENCE
// ============================================================================

void savePlantRegistry() {
    _prefs.begin(NVS_ROUTINE_NS, false);
    _prefs.putInt("count", plantCount);
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;
        char key[8]; snprintf(key, sizeof(key), "p%02d", i);
        // Store as "x,y,name,N,P,K"
        char val[80];
        snprintf(val, sizeof(val), "%.1f,%.1f,%s,%.1f,%.1f,%.1f",
                 plantRegistry[i].x, plantRegistry[i].y,
                 plantRegistry[i].name,
                 plantRegistry[i].targetN, plantRegistry[i].targetP,
                 plantRegistry[i].targetK);
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

        // Parse "x,y,name,N,P,K"
        int c1 = val.indexOf(',');
        int c2 = val.indexOf(',', c1+1);
        int c3 = val.indexOf(',', c2+1);
        int c4 = val.indexOf(',', c3+1);
        int c5 = val.indexOf(',', c4+1);

        if (c5 < 0) { plantRegistry[i].active = false; continue; }

        plantRegistry[i].x       = val.substring(0, c1).toFloat();
        plantRegistry[i].y       = val.substring(c1+1, c2).toFloat();
        val.substring(c2+1, c3).toCharArray(plantRegistry[i].name, 24);
        plantRegistry[i].targetN = val.substring(c3+1, c4).toFloat();
        plantRegistry[i].targetP = val.substring(c4+1, c5).toFloat();
        plantRegistry[i].targetK = val.substring(c5+1).toFloat();
        plantRegistry[i].active  = true;
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
        if (sqrtf(dx*dx + dy*dy) <= toleranceMm) return true;
    }
    return false;
}

void broadcastPlantMap(uint8_t clientNum) {
    DynamicJsonDocument doc(2048);
    doc["evt"] = "PLANT_MAP";
    JsonArray plants = doc.createNestedArray("plants");
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) continue;
        JsonObject p = plants.createNestedObject();
        p["idx"]  = i;
        p["x"]    = plantRegistry[i].x;
        p["y"]    = plantRegistry[i].y;
        p["name"] = plantRegistry[i].name;
        p["tN"]   = plantRegistry[i].targetN;
        p["tP"]   = plantRegistry[i].targetP;
        p["tK"]   = plantRegistry[i].targetK;
    }
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
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

        // TODO(Luna): XGBoost model override
        // fertMl = xgboostFertilizerAmount(soil, plant);

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
            if (sysState.getFlutter() == FLUTTER_DISCONNECTED) goto done;
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
    // Format: "x:y:name" or "x:y:name:N:P:K"
    int c1 = params.indexOf(':');
    int c2 = params.indexOf(':', c1+1);
    if (c1 < 0 || c2 < 0) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"PLANT_ERROR\",\"reason\":\"Bad format\"}");
        return;
    }

    float x = params.substring(0, c1).toFloat();
    float y = params.substring(c1+1, c2).toFloat();

    // Optional NPK targets
    int c3 = params.indexOf(':', c2+1);
    int c4 = params.indexOf(':', c3+1);
    int c5 = (c4 >= 0) ? params.indexOf(':', c4+1) : -1;

    String name = (c3 >= 0) ? params.substring(c2+1, c3) : params.substring(c2+1);
    float tN = (c4 >= 0) ? params.substring(c3+1, c4).toFloat() : 0;
    float tP = (c5 >= 0) ? params.substring(c4+1, c5).toFloat() : 0;
    float tK = (c5 >= 0) ? params.substring(c5+1).toFloat() : 0;

    // Find empty slot or update existing
    int slot = -1;
    for (int i = 0; i < MAX_PLANTS; i++) {
        if (!plantRegistry[i].active) { slot = i; break; }
        // If same position within 10mm, update it
        if (fabsf(plantRegistry[i].x - x) < 10 &&
            fabsf(plantRegistry[i].y - y) < 10) {
            slot = i; break;
        }
    }

    if (slot < 0) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"PLANT_ERROR\",\"reason\":\"Registry full\"}");
        return;
    }

    plantRegistry[slot].x       = x;
    plantRegistry[slot].y       = y;
    plantRegistry[slot].targetN = tN;
    plantRegistry[slot].targetP = tP;
    plantRegistry[slot].targetK = tK;
    plantRegistry[slot].active  = true;
    name.toCharArray(plantRegistry[slot].name, 24);
    plantCount = 0;
    for (int i = 0; i < MAX_PLANTS; i++) if (plantRegistry[i].active) plantCount++;

    savePlantRegistry();

    StaticJsonDocument<128> doc;
    doc["evt"]  = "PLANT_REGISTERED";
    doc["slot"] = slot;
    doc["name"] = plantRegistry[slot].name;
    doc["x"]    = x;
    doc["y"]    = y;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
    AgriLog(TAG_ROUTINE, LEVEL_SUCCESS, "Plant registered: %s (%.1f, %.1f)",
                  plantRegistry[slot].name, x, y);
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
    // float zH = params.substring(c4+1).toFloat(); // reserved for Z

    int total = cols * rows;

    // Clear previous candidates
    memset(candidateBuffer, 0, sizeof(candidateBuffer));
    candidateCount = 0;

    bool streamWasActive = sysState.isStreaming();
    sysState.setStreaming(false);
    sysState.setOperation(OP_HOMING);
    enqueueGrblCommand("$HX"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    enqueueGrblCommand("$HY"); waitForGrblIdle(SCAN_HOME_TIMEOUT_MS);
    requestMachineDimensions(); delay(1000);
    sysState.setOperation(OP_SCANNING);

    StaticJsonDocument<96> startDoc;
    startDoc["evt"]   = "DETECT_START";
    startDoc["total"] = total;
    String startOut; serializeJson(startDoc, startOut);
    webSocket.sendTXT(clientNum, startOut);

    int frameIdx = 0;
    for (int row = 0; row < rows; row++) {
        for (int colStep = 0; colStep < cols; colStep++) {
            if (sysState.getFlutter() == FLUTTER_DISCONNECTED) goto detect_done;
            int col = (row % 2 == 0) ? colStep : (cols - 1 - colStep);
            float tx = min(col * stepX, machineDim.maxX);
            float ty = min(row * stepY, machineDim.maxY);
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
    
    _prefs.begin(NVS_ROUTINE_NS, true);
    _waterFlowRate = _prefs.getFloat("w_rate", 10.0f);
    _fertFlowRate  = _prefs.getFloat("f_rate", 10.0f);
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
                RoutineConfig dummyCfg; // Need to populate this or make handleFarmingCycle more flexible
                handleFarmingCycle(activeClientNum, dummyCfg);
            } else if (routineType == 2) { // Full Grid Scan
                // handleFullGridScan(); // To be refactored
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
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    
    enqueueGrblCommand("M100"); // Water ON
    unsigned long duration = (ml / _waterFlowRate) * 1000;
    delay(duration);
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
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    
    enqueueGrblCommand("M102"); // Fert ON
    unsigned long duration = (ml / _fertFlowRate) * 1000;
    delay(duration);
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
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    
    char moveCmd[48];
    snprintf(moveCmd, sizeof(moveCmd), "G0 Y%.1f F%d", machineDim.maxY - 20, GRBL_DEFAULT_FEEDRATE);
    enqueueGrblCommand(moveCmd);
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    
    enqueueGrblCommand("G0 X0 Y0 F1000");
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    
    enqueueGrblCommand("G0 Z5 F500");
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    
    enqueueGrblCommand("M104"); // Weeder ON
    enqueueGrblCommand("G2 X0 Y0 I10 J0 F500");
    waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    enqueueGrblCommand("M105"); // Weeder OFF
    
    webSocket.sendTXT(clientNum, "{\"evt\":\"CLEAN_SENSORS_COMPLETE\"}");
}

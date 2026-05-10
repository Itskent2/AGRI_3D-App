/**
 * @file agri3d_routine.h
 * @brief Autonomous farming routine orchestrator and plant registry.
 *
 * This module is the "mission controller" that ties together weather gating,
 * NPK sensing, fertigation, camera imaging, and weed detection into one
 * repeatable per-plant cycle.
 *
 * -- Plant Registry --
 * Plants are registered by the user from Flutter via REGISTER_PLANT.
 * Each plant has an XY position (mm from home), a name, and optional
 * user-set target NPK values. If targets are 0 the XGBoost model decides.
 * Registry is persisted in NVS so it survives reboots.
 *
 * -- Commands --
 *   RUN_FARMING_CYCLE           Full autonomous per-plant routine
 *   AUTO_DETECT_PLANTS:cols:rows:stepX:stepY:zH
 *                               Scan grid, run AI plant detector, send
 *                               PLANT_CANDIDATE events to Flutter for
 *                               user confirmation. Non-blocking — ESP32
 *                               does not wait for replies mid-scan.
 *   CONFIRM_PLANT:x:y:name      Flutter confirmed a candidate → add to registry
 *   REJECT_PLANT:x:y            Flutter rejected a candidate → discard
 *   SCAN_NPK:stepX:stepY        NPK-only grid scan (heatmap population)
 *   SCAN_PHOTO:cols:rows:stepX:stepY:zH  Photo-only grid scan (plant map)
 *   SCAN_FULL:cols:rows:stepX:stepY:zH   NPK + photo at each cell
 *   REGISTER_PLANT:x:y:name[:N:P:K]      Manually add/update a plant entry
 *   CLEAR_PLANTS                          Remove all plant entries
 *   GET_PLANT_MAP                         Broadcast full plant registry
 */

#pragma once
#include <Arduino.h>
#include "agri3d_npk.h"

// ── Plant Registry ─────────────────────────────────────────────────────────
#define MAX_PLANTS  30   ///< Maximum plants that can be registered

struct PlantPosition {
    float  x;           ///< Gantry X position (mm from home)
    float  y;           ///< Gantry Y position (mm from home)
    char   name[24];    ///< Plant name e.g. "Lettuce"
    float  targetN;     ///< User-set target N mg/kg (0 = use XGBoost)
    float  targetP;     ///< User-set target P mg/kg (0 = use XGBoost)
    float  targetK;     ///< User-set target K mg/kg (0 = use XGBoost)
    float  diameter;    ///< Rosette diameter in mm for exclusion zone
    bool   active;      ///< true = slot in use
    bool   aiDetected;  ///< true = added via AUTO_DETECT (not manual)
    float  dx;          ///< Dipping coordinate X
    float  dy;          ///< Dipping coordinate Y
    int    cropType;    ///< Crop Type Enum (1=Lettuce, 2=Kangkong, 3=Spinach)
    uint32_t ts;        ///< Timestamp of last check
};

/**
 * Pending plant candidate awaiting Flutter user confirmation.
 * Populated during AUTO_DETECT_PLANTS scan, cleared after user responds.
 */
struct PlantCandidate {
    float  x;            ///< Gantry X where candidate was detected (mm)
    float  y;            ///< Gantry Y where candidate was detected (mm)
    float  confidence;   ///< AI detection confidence 0.0–1.0 (TODO Luna)
    bool   pending;      ///< true = awaiting user confirmation
};

#define MAX_CANDIDATES 30
extern PlantCandidate candidateBuffer[MAX_CANDIDATES];
extern int            candidateCount;

extern PlantPosition plantRegistry[MAX_PLANTS];
extern int           plantCount;

// ── Weed coordinate (output of AI weed detector) ──────────────────────────
struct WeedCoord {
    float mmX;   ///< Real-world X of detected weed (mm)
    float mmY;   ///< Real-world Y of detected weed (mm)
};

// ── Routine config (sent by Flutter via RUN_FARMING_CYCLE) ────────────────
struct RoutineConfig {
    float zSensorHeight  = 50.0f;   ///< Z height when dipping NPK sensor (mm)
    float zCameraHeight  = 200.0f;  ///< Z height for camera photos (mm)
    bool  doFertigation  = true;
    bool  doWatering     = true;
    bool  doWeedScan     = true;
};

// ── Shared Scan Parameters ──────────────────────────────────────────────────
struct ScanParams {
    uint8_t clientNum;
    int cols;
    int rows;
    float stepX;
    float stepY;
    float zHeight;
};
extern ScanParams globalScanParams;

// ── Public API ─────────────────────────────────────────────────────────────

/** Initialise plant registry from NVS. Call from setup(). */
void routineInit();

/** Run AUTO_DETECT_PLANTS: scan grid, run AI, send candidates to Flutter. */
void handleAutoDetectPlants(uint8_t clientNum, const String& params);

/**
 * Flutter confirmed a plant candidate → add to registry.
 * Params: "x:y:name"  (name is optional, defaults to "Plant")
 */
void handleConfirmPlant(uint8_t clientNum, const String& params);

/** Flutter rejected a candidate → remove from candidateBuffer. */
void handleRejectPlant(uint8_t clientNum, const String& params);

/** Run the full per-plant farming cycle for all registered plants. */
void handleFarmingCycle(uint8_t clientNum, const RoutineConfig& cfg);

/** NPK-only grid scan — populates heatmap without taking photos. */
void handleScanNpk(uint8_t clientNum, float stepX, float stepY);

/**
 * Photo-only grid scan — builds plant map.
 * Delegates to agri3d_plant_map.cpp handleScanPlant().
 */
void handleScanPhoto(uint8_t clientNum, const String& params);

/** NPK + photo at every grid cell. */
void handleScanFull(uint8_t clientNum, const String& params);

/**
 * @brief Parse and handle a manual plant registration command.
 *        Format: REGISTER_PLANT:x:y:name:diameter[:targetN:targetP:targetK]
 */
void handleRegisterPlant(uint8_t clientNum, const String& params);

/** Dip all registered plants autonomously */
void handleDipAllPlants(uint8_t clientNum);
void executeDipAllPlants(uint8_t clientNum);

/** Autonomous Master Farming Routine */
void handleAutonomousFarming(uint8_t clientNum);
void executeAutonomousFarming(uint8_t clientNum);

/**
 * @brief Delete a specific plant from the registry by index.
 */
void handleDeletePlant(uint8_t clientNum, const String& params);

/** Remove all plant entries from registry and NVS. */
void handleClearPlants(uint8_t clientNum);

/** Water at specific coordinates with amount. */
void handleWater(uint8_t clientNum, float x, float y, float ml, float ox = 0, float oy = 0);

/** Fertilize at specific coordinates with amount. */
void handleFertilize(uint8_t clientNum, float x, float y, float ml, float ox = 0, float oy = 0);

/** Clean NPK sensor and weeder. */
void handleCleanSensors(uint8_t clientNum);

/** Set flow rate for water (ml/s). */
void setWaterFlowRate(float rate);

/** Set flow rate for fertilizer (ml/s). */
void setFertFlowRate(float rate);

/** Get flow rate for water (ml/s). */
float getWaterFlowRate();

/** Get flow rate for fertilizer (ml/s). */
float getFertFlowRate();

/** Broadcast all registered plants as JSON to a specific client. */
void broadcastPlantMap(uint8_t clientNum);

/** Save plant registry to NVS (called automatically on every change). */
void savePlantRegistry();

/** Returns true if (x, y) is within toleranceMm of any registered plant. */
bool isKnownPlantPosition(float x, float y, float toleranceMm = 50.0f);

/** Send a routine task to Core 1 */
void startRoutine(uint32_t type);

/** Executed on Core 1 by routineWorkerTask */
void executeScanPlant(const ScanParams& cfg);

/** Executed on Core 1 by routineWorkerTask */
void executeScanFull(const ScanParams& cfg);

/** Dip Z-axis to 5mm, take reading, return to MaxZ-10. Non-blocking. */
void executeNpkDip();

/**
 * @file agri3d_npk.h
 * @brief NPK soil sensor handler (RS485 / UART2 Modbus RTU).
 *
 * -- What this module does --
 *   1. Polls the NPK sensor at a configurable interval.
 *   2. Associates each reading with the current gantry (X, Y) position
 *      so Flutter can place it on the heatmap grid.
 *   3. Stores a per-day history in ESP32 NVS (one entry per position per day).
 *   4. Broadcasts readings to Flutter in two formats:
 *        a) Live update:  {"evt":"NPK","x":..,"y":..,"n":..,"p":..,"k":..,"ts":..}
 *        b) History dump: {"evt":"NPK_HISTORY","date":"2025-05-03","readings":[...]}
 *
 * -- Flutter heatmap compatibility --
 *   The existing sensor_heatmaps.dart reads JSON with keys:
 *     x, y  → grid cell (integers, clamped to gridSize=50)
 *     m/val → single float value
 *   We extend this by sending three separate live packets (one per nutrient)
 *   PLUS a unified NPK packet for the plot_model.dart (n, p, k together).
 *
 * -- Modbus RTU query (standard NPK soil sensor) --
 *   TX: 01 03 00 1E 00 03 65 CD
 *   RX: 01 03 06 HH HL PH PL KH KL CRC
 *   N = (HH<<8|HL) mg/kg
 *   P = (PH<<8|PL) mg/kg
 *   K = (KH<<8|KL) mg/kg
 *
 * -- NVS History format --
 *   Namespace: "npk_hist"
 *   Key pattern: "YYYYMMDD_Gxx_yy"  (grid cell, not raw mm)
 *   Value: "n,p,k,ts" comma-separated string
 *   Max entries per day: bounded by NVS storage (approx 200 cells)
 *
 * -- Grid mapping --
 *   Raw gantry mm → grid cell index via:
 *     gridX = round(grblX / stepX)  where stepX = machineDim.maxX / (gridCols-1)
 *     gridY = round(grblY / stepY)  where stepY = machineDim.maxY / (gridRows-1)
 *   Default gridCols=5, gridRows=3 (matches Flutter plot_model.dart).
 */

#pragma once
#include <Arduino.h>

// ── Full 7-in-1 Soil Reading ─────────────────────────────────────────────────
/**
 * All 7 values from one sensor probe at one XY position.
 * The single RS485 sensor physically moves with the gantry to each cell.
 */
struct SoilReading {
    // ── 7 sensor values ────────────────────────────────────────────────────
    float moisture = 0.0f;  ///< Soil moisture      (%)
    float tempC    = 0.0f;  ///< Soil temperature   (°C)
    float ec       = 0.0f;  ///< Electrical conduct. (μS/cm)
    float ph       = 0.0f;  ///< Soil pH             (0-14)
    float n        = 0.0f;  ///< Nitrogen            (mg/kg)
    float p        = 0.0f;  ///< Phosphorus          (mg/kg)
    float k        = 0.0f;  ///< Potassium           (mg/kg)

    // ── Position metadata ──────────────────────────────────────────────────
    float x     = 0.0f;  ///< Gantry X at time of reading (mm)
    float y     = 0.0f;  ///< Gantry Y at time of reading (mm)
    int   gridX = 0;     ///< Heatmap grid column (0-based)
    int   gridY = 0;     ///< Heatmap grid row    (0-based)
    time_t timestamp = 0;
    bool   valid     = false;
};

// Keep alias for backwards compatibility
typedef SoilReading NpkReading;

/** Most-recent successful soil reading. */
extern SoilReading latestSoil;

/**
 * @brief Initialise the NPK UART2 port and DERE pin.
 *        Call from setup().
 */
void npkInit();

/**
 * @brief Must be called from loop(). Polls the sensor at NPK_POLL_INTERVAL_MS.
 */
void npkLoop();

/**
 * @brief Immediately take one full 7-in-1 reading at the current gantry position.
 *        Used by the farming routine at each plant stop.
 * @return true if sensor responded with valid data.
 */
bool npkReadNow();

/**
 * @brief Broadcast today's soil history for a specific grid cell.
 * @param clientNum WebSocket client to send to.
 * @param gridX     Grid column (0-based).
 * @param gridY     Grid row (0-based).
 */
void npkSendHistory(uint8_t clientNum, int gridX, int gridY);

/**
 * @brief Broadcast the full soil history for today (all cells).
 *        Called on Flutter client connect to pre-populate the heatmap.
 * @param clientNum WebSocket client to send to.
 */
void npkSendFullHistory(uint8_t clientNum);

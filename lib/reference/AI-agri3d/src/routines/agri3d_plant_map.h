/**
 * @file agri3d_plant_map.h
 * @brief Plant-map grid scanning — SD-first, heartbeat-safe two-phase design.
 *
 * Command protocol (Flutter → ESP32):
 *   SCAN_PLANT:cols:rows:stepX:stepY:zHeight   — Phase 1: scan + save to SD
 *   UPLOAD_SCAN                                 — Phase 2: upload SD frames to Flutter
 *
 * Phase 1 — SCAN_PLANT:
 *   cols    = number of columns (X direction)
 *   rows    = number of rows (Y direction)
 *   stepX   = mm between columns (e.g. 150)
 *   stepY   = mm between rows    (e.g. 150)
 *   zHeight = camera height in mm (e.g. 200)
 *
 *   Example: SCAN_PLANT:5:4:150:150:200 → 5×4 = 20 frames, 150mm grid
 *
 *   Events emitted:
 *     SCAN_START  {total, cols, rows, stepX, stepY, zHeight, maxX, maxY}
 *     SCAN_PROGRESS {idx, total, x, y, pct}     ← JSON only, no binary
 *     SCAN_COMPLETE {total, aborted, ready}      ← ready=true means upload available
 *
 * Phase 2 — UPLOAD_SCAN:
 *   Reads the SD index written during Phase 1 and streams each JPEG to Flutter.
 *   Flutter watchdog is reset between frames so heartbeat stays alive.
 *
 *   Events emitted:
 *     UPLOAD_SCAN_START {total}
 *     FRAME_META {idx, total, x, y}  followed immediately by binary JPEG
 *     UPLOAD_SCAN_COMPLETE {sent, total, aborted}
 *
 * AI integration (Phase 5):
 *   captureFrameAtPosition() already has a TODO(Luna) hook.
 */

#pragma once
#include <Arduino.h>
#include "agri3d_routine.h"

/**
 * @brief Parse and enqueue a SCAN_PLANT command onto Core 1.
 * @param clientNum  WebSocket client that issued the command.
 * @param cmdBody    Everything after "SCAN_PLANT:" e.g. "5:4:150:150:200"
 */
void handleScanPlant(uint8_t clientNum, const String& cmdBody);

/**
 * @brief Phase 1 worker — executed on Core 1 by routineWorkerTask.
 *        Traverses the grid, saves each JPEG to SD. No WebSocket binary traffic.
 */
void executeScanPlant(const ScanParams& cfg);

/**
 * @brief Enqueue an UPLOAD_SCAN request onto Core 1.
 * @param clientNum  WebSocket client that issued the command.
 */
void handleScanUpload(uint8_t clientNum);

/**
 * @brief Phase 2 worker — executed on Core 1 by routineWorkerTask.
 *        Reads index from SD and sends each FRAME_META + JPEG to Flutter.
 */
void executeScanUpload(uint8_t clientNum);

// =============================================================================
// TODO(Luna): AI Weeding Hook
// =============================================================================
// When the AI model identifies weed locations, call this function with a list
// of (x, y) coordinates. It will move to each one and capture a JPEG for
// targeted weeding confirmation or actuation.
//
// Signature (implement in agri3d_plant_map.cpp):
//   void aiCaptureCoordsForWeeding(uint8_t clientNum,
//                                   float* xList, float* yList, int count);
//
// The function should:
//   1. Set operation = OP_AI_WEEDING (locks camera from stream)
//   2. For each coordinate: captureFrameAtPosition() → sends FRAME_META + BIN
//   3. After all coords: set operation = OP_IDLE
// =============================================================================

/**
 * @file agri3d_plant_map.h
 * @brief Plant-map grid scanning via G-code traversal.
 *
 * Command protocol (Flutter → ESP32):
 *   SCAN_PLANT:cols:rows:stepX:stepY:zHeight
 *
 *   cols    = number of columns (X direction)
 *   rows    = number of rows (Y direction)
 *   stepX   = mm between columns (e.g. 150)
 *   stepY   = mm between rows    (e.g. 150)
 *   zHeight = camera height in mm (e.g. 200) — reserved for when Z is fixed
 *
 * Example:
 *   SCAN_PLANT:5:4:150:150:200  →  5×4 = 20 frames, 150mm grid, cam at 200mm Z
 *
 * Scan sequence:
 *   1. Broadcast SCAN_START JSON with total frame count and start position
 *   2. Home X ($HX) → wait idle
 *   3. Home Y ($HY) → wait idle
 *   4. Request $$ to refresh dimension cache
 *   5. Snake-pattern traversal:
 *        Row 0: col 0→N (left to right)
 *        Row 1: col N→0 (right to left)   ← minimises travel
 *        Row 2: col 0→N ...
 *      At each cell: captureFrameAtPosition()
 *   6. Return gantry to (0, 0)
 *   7. Restore isStreaming, broadcast SCAN_COMPLETE
 *
 * AI integration (Phase 5):
 *   captureFrameAtPosition() already has a TODO(Luna) hook.
 *   The same handleScanPlant() function can also be called with a weed
 *   coordinate list from aiDetectWeeds() for targeted re-imaging.
 */

#pragma once
#include <Arduino.h>
#include "agri3d_routine.h"

/**
 * @brief Parse and execute a SCAN_PLANT command.
 * @param clientNum  The WebSocket client that sent the command.
 * @param cmdBody    Everything after "SCAN_PLANT:" e.g. "5:4:150:150:200"
 */
void handleScanPlant(uint8_t clientNum, const String& cmdBody);

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

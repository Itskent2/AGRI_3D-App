/**
 * @file agri3d_camera.h
 * @brief Camera initialisation, FPM-based live stream task, and the core
 *        captureFrameAtPosition() helper used by both plant mapping and
 *        the AI weeding pipeline.
 *
 * Stream quality: UXGA (1600×1200) — highest quality justified by low FPS cap.
 * Stream rate:    1–300 FPM (1–5 FPS), controlled by SET_FPM command.
 *
 * Camera lock:
 *   The stream task checks isCameraAvailable() before every frame grab.
 *   OP_SCANNING and OP_AI_WEEDING lock the camera so the stream task skips.
 */

#pragma once
#include "esp_camera.h"
#include <Arduino.h>

/**
 * @brief Initialise the ESP32-S3 camera hardware.
 *        Must be called from setup() after WiFi init.
 * @return true if camera initialised successfully.
 */
bool cameraInit();

/**
 * @brief Helper function to check and reset the camera if unresponsive.
 */
void cameraSanityCheck();

/**
 * @brief FreeRTOS task — streams JPEG frames to all connected WebSocket clients
 *        at the rate set by sysState.fpm.
 *        Pinned to Core 1. Started by cameraInit().
 *        Automatically pauses when isCameraAvailable() returns false.
 */
void streamTask(void *pvParameters);

/**
 * @brief Move the gantry to (targetX, targetY), wait for GRBL Idle,
 *        capture one JPEG frame, and send it to a specific WebSocket client
 *        with a structured JSON metadata header.
 *
 * Protocol (two messages sent in sequence):
 *   1. TXT → {"evt":"FRAME_META","idx":N,"total":M,"x":X,"y":Y,"z":Z}
 *   2. BIN → raw JPEG bytes
 *
 * This function is the single shared primitive for:
 *   - agri3d_plant_map: grid scan
 *   - ai_weeding (Phase 5): capture at each detected weed coordinate
 *
 * @param clientNum  WebSocket client to send to (usually activeClientNum).
 * @param idx        1-based frame index.
 * @param total      Total number of frames in this scan session.
 * @param targetX    Gantry X destination (mm).
 * @param targetY    Gantry Y destination (mm).
 * @return true if capture and send succeeded.
 */
bool captureFrameAtPosition(uint8_t clientNum, int idx, int total,
                            float targetX, float targetY);

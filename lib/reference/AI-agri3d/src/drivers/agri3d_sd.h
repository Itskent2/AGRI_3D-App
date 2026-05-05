/**
 * @file agri3d_sd.h
 * @brief SD card initialisation and G-code file streaming with flow control.
 *
 * SD file streaming uses "ok"-gated flow control:
 *   1. Read one G-code line from file
 *   2. Send to Nano → set sdWaitingForOk = true
 *   3. Wait until Nano replies "ok" (signalled by grbl bridge)
 *   4. Read next line
 *
 * The grbl bridge (agri3d_grbl.cpp) sets sdWaitingForOk = false when
 * it receives "ok" from Nano. This keeps the Nano's planner buffer
 * from overflowing during long G-code jobs.
 *
 * Comments (lines starting with ';' or '(') are skipped.
 * Blank lines are skipped.
 */

#pragma once
#include <Arduino.h>

/**
 * Initialise the SD card (1-bit MMC mode).
 * Called from setup(). Non-fatal if no SD card present.
 * @return true if SD mounted successfully.
 */
bool sdInit();

/**
 * Open a G-code file from SD and begin streaming it to the Nano.
 * @param clientNum  WebSocket client that issued the command.
 * @param filename   Full path on SD card e.g. "/jobs/field_map.nc"
 */
void handleStartSD(uint8_t clientNum, const String& filename);

/** Stop the current SD stream and close the file. */
void handleStopSD(uint8_t clientNum);

/**
 * Must be called from loop(). Handles the "ok"-gated line-by-line
 * feed to the Nano. No-op if no SD stream is active.
 */
void sdLoop();

/**
 * Called by agri3d_grbl.cpp when Nano replies "ok".
 * Clears the flow-control gate so sdLoop() sends the next line.
 */
void sdSignalOk();

/** @return true if an SD stream is currently active. */
bool sdIsStreaming();

// ============================================================================
// IMAGE STORAGE
// ============================================================================

/**
 * @brief Save a JPEG buffer to SD card with standardised path + timestamp.
 *
 * Path format: /<category>/YYYYMMDD/<prefix>_<idx>_x<X>_y<Y>_<ts>.jpg
 *   category  = SD_IMG_PLANTMAP | SD_IMG_WEEDING | SD_IMG_DETECT | SD_IMG_STREAM
 *   prefix    = short label e.g. "f" (frame), "w" (weed), "d" (detect)
 *
 * @param buf       JPEG data buffer
 * @param len       Buffer length in bytes
 * @param category  Directory constant from agri3d_config.h
 * @param prefix    Filename prefix character (e.g. 'f', 'w', 'd')
 * @param idx       Frame/image index within this session
 * @param x         Gantry X at capture (mm)
 * @param y         Gantry Y at capture (mm)
 * @param timestamp Unix timestamp of the capture
 * @param outPath   Optional: buffer to receive the saved file path (NULL to ignore)
 * @return true if saved successfully.
 */
bool sdSaveImage(const uint8_t* buf, size_t len,
                 const char* category, char prefix,
                 int idx, float x, float y,
                 time_t timestamp,
                 char* outPath = nullptr);

/**
 * @brief Ensure a directory exists on the SD card, creating it if needed.
 * @param path Full path e.g. "/plantmap/20250503"
 */
void sdEnsureDir(const char* path);

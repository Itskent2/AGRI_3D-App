/**
 * @file agri3d_ai.h
 * @brief AI analysis hooks for plant/weed detection using Edge Impulse FOMO model.
 *
 * Model: Weed Detection (Edge Impulse Project #923987, Impulse #2)
 *   - Input:  96×96 RGB image
 *   - Output: FOMO object detection with 2 classes: "crop", "weed"
 *   - Arena:  ~290 KB (allocated in PSRAM)
 */

#pragma once
#include <Arduino.h>

// ── Edge Impulse SDK ──────────────────────────────────────────────────────
// The model_variables.h pulls in the full impulse definition, the compiled
// TFLite graph, and all DSP / postprocessing blocks.
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

// ── Detection result for a single bounding box ───────────────────────────
struct AiDetection {
    const char* label;   // "crop" or "weed"
    float confidence;    // 0.0 – 1.0
    int x;               // FOMO grid x (0–11 for 96px / 8px cells)
    int y;               // FOMO grid y
    int width;           // Always 1 cell for FOMO
    int height;          // Always 1 cell for FOMO
};

// ── Aggregate result of an AI frame analysis ─────────────────────────────
struct AiResult {
    bool foundPlant;
    bool foundWeed;
    float confidence;     // Highest weed confidence in this frame
    int xOffset;          // Pixel offset from center (highest-confidence weed)
    int yOffset;

    // Detailed detections
    int cropCount;
    int weedCount;
    int totalDetections;
    AiDetection detections[EI_CLASSIFIER_OBJECT_DETECTION_COUNT]; // max 10
};

/**
 * @brief Initialize AI engine — allocate Edge Impulse arena in PSRAM.
 *        Must be called after cameraInit() and PSRAM is available.
 */
void aiInit();

/**
 * @brief Perform weed/crop detection on a raw RGB888 pixel buffer.
 *
 * The caller must decode the JPEG and resize to 96×96 RGB before calling.
 * Use aiAnalyzeJpeg() for automatic JPEG decode + inference.
 *
 * @param buf  Pointer to 96×96×3 RGB888 pixel data (27648 bytes).
 * @param len  Length of the buffer (must be 27648).
 */
AiResult aiAnalyzeFrame(uint8_t* buf, size_t len);

/**
 * @brief Convenience wrapper: decodes a JPEG buffer, resizes to 96×96,
 *        and runs Edge Impulse inference.
 *
 * This is the primary entry point for the scanning/weeding pipeline.
 *
 * @param jpegBuf  Pointer to JPEG data from esp_camera.
 * @param jpegLen  Length of JPEG data.
 */
AiResult aiAnalyzeJpeg(uint8_t* jpegBuf, size_t jpegLen);

/**
 * @brief Returns true if the AI engine initialized successfully.
 */
bool aiIsReady();

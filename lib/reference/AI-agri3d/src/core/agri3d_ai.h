/**
 * @file agri3d_ai.h
 * @brief AI analysis hooks for plant detection, weed identification, and health monitoring.
 *        To be implemented by Luna.
 */

#pragma once
#include <Arduino.h>

/** Result of an AI frame analysis. */
struct AiResult {
    bool foundPlant;
    bool foundWeed;
    float confidence;
    int xOffset; // Pixel offset from center
    int yOffset;
};

/**
 * @brief Initialize AI engine (allocate tensors, load model from SD/Flash).
 */
void aiInit();

/**
 * @brief Perform computer vision analysis on a JPEG buffer.
 * @param buf Pointer to the JPEG data.
 * @param len Length of the JPEG data.
 */
AiResult aiAnalyzeFrame(uint8_t* buf, size_t len);

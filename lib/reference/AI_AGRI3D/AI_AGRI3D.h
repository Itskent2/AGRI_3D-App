#ifndef AI_AGRI3D_H
#define AI_AGRI3D_H

#include <Arduino.h>

// Include the Edge Impulse SDK main header
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

class AI_AGRI3D {
public:
    AI_AGRI3D();
    void begin();
    
    // Skeleton function for weed detection inference
    // Pass raw features (image data) and size
    void runWeedDetection(float *features, size_t feature_size);

private:
    void notifyWeedDetected();
};

extern AI_AGRI3D aiSystem;

#endif // AI_AGRI3D_H

#include <Arduino.h>
#include "AI_Agri3D.h"

void setup() {
    Serial.begin(115200);
    
    // Initialize AI-agri3d Core Systems
    networkInit();  // Sets up WiFi/AP, MDNS, WebSockets
    grblInit();     // Initializes UART connection to Nano
    routineInit();  // Loads plant registry from NVS
}

void loop() {
    networkLoop();  // Handles UDP beacon, WS polling
    grblLoop();     // Polls GRBL status
}

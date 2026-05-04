#include <Arduino.h>
#include "AI_Agri3D.h"

void setup() {
    Serial.begin(115200);
    
    // Give the USB OTG Serial port time to connect to the computer
    // before we start printing things or connecting to WiFi
    delay(3000);
    Serial.println("\n\n========================================");
    Serial.println("ESP32-S3 BOOTING UP...");
    Serial.println("========================================\n");
    
    // Initialize AI-agri3d Core Systems
    Serial.printf("[%lu] [BOOT] Starting networkInit()... (Free Heap: %u)\n", millis(), ESP.getFreeHeap());
    networkInit();      // Sets up WiFi/AP, MDNS, WebSockets
    Serial.printf("[%lu] [BOOT] networkInit() complete. (Free Heap: %u)\n", millis(), ESP.getFreeHeap());

    Serial.printf("[%lu] [BOOT] Starting grblInit()... (Free Heap: %u)\n", millis(), ESP.getFreeHeap());
    grblInit();         // Initializes UART connection to Nano
    Serial.printf("[%lu] [BOOT] grblInit() complete. (Free Heap: %u)\n", millis(), ESP.getFreeHeap());

    Serial.printf("[%lu] [BOOT] Starting cameraInit()... (Free Heap: %u)\n", millis(), ESP.getFreeHeap());
    cameraInit();       // Initializes camera and stream task
    Serial.printf("[%lu] [BOOT] cameraInit() complete. (Free Heap: %u)\n", millis(), ESP.getFreeHeap());

    // NOTE: Unavailable for now (commented out for testing purposes)
    // Serial.println("[BOOT] Starting routineInit()...");
    // routineInit();      // Loads plant registry from NVS
    // Serial.println("[BOOT] Starting sdInit()...");
    // sdInit();           // Initializes the SD card
    // Serial.println("[BOOT] Starting npkInit()...");
    // npkInit();          // Initializes NPK sensor
    // Serial.println("[BOOT] Starting environmentInit()...");
    // environmentInit();  // Initializes environment sensors
    
    Serial.println("[BOOT] SETUP COMPLETE. Entering loop().");
}

void loop() {
    networkLoop();  // Handles UDP beacon, WS polling
    grblLoop();     // Polls GRBL status
}

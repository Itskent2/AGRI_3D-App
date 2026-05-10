#include "esp_camera.h"
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "FS.h"
#include "SD_MMC.h"

// --- COMPREHENSIVE CONFIG ---
const bool DEBUG_ENA = true;
const char *AP_SSID = "Farmbot_S3_Direct";
const char *AP_PASS = "12345678";

// --- Stream Control ---
bool isStreaming = false; 

// --- Adaptive Resolution Ladder ---
framesize_t resolution_ladder[] = {FRAMESIZE_5MP, FRAMESIZE_QSXGA, FRAMESIZE_WQXGA, FRAMESIZE_QXGA, FRAMESIZE_UXGA, FRAMESIZE_SXGA, FRAMESIZE_XGA, FRAMESIZE_SVGA, FRAMESIZE_VGA, FRAMESIZE_CIF, FRAMESIZE_QVGA, FRAMESIZE_HQVGA, FRAMESIZE_QCIF, FRAMESIZE_QQVGA, FRAMESIZE_96X96};
const int ladder_size = sizeof(resolution_ladder) / sizeof(resolution_ladder[0]);
int current_ladder_idx = 8; // Start at VGA
unsigned long last_switch_time = 0;
const int switch_cooldown = 3000;
int bad_frame_count = 0, good_frame_count = 0;

// --- WiFi Config ---
struct WifiCreds { const char *ssid; const char *pass; };
WifiCreds myNetworks[] = {{"AdminAccess", "Admin@CTU.2024"}};
int networkCount = 1;

// --- Camera Pins (N16R8 typical ESP32-S3) ---
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5
#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

// --- Communications Bridge (Nano) ---
#define NANO_RX 44
#define NANO_TX 43
HardwareSerial NanoSerial(1);
String nanoRxBuffer = ""; // NEW: Buffer for non-blocking serial

// --- NPK Sensor (RS485 via UART2) ---
#define NPK_RX 19
#define NPK_TX 20
#define NPK_DERE 8
HardwareSerial NpkSerial(2);
const byte npkQuery[] = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x03, 0x65, 0xCD};
byte npkResponse[11];

// --- SD Card Pins (1-Bit MMC) ---
#define SD_MMC_CMD 38
#define SD_MMC_CLK 39
#define SD_MMC_D0 40
File rootFile;
bool isStreamingSD = false;
bool waitForOk = false;

// --- Environmental & System State ---
enum SystemState { STATE_IDLE, STATE_STREAMING_SD, STATE_RAIN_PAUSED, STATE_ALARM_LOCKED };
SystemState currentState = STATE_IDLE;

#define RAIN_PIN_DIGITAL 1
volatile bool rainDetected = false;
volatile bool isWeatherGated = false; // Updated by FreeRTOS task
float weather_lat = 10.3157, weather_lon = 123.8854;

// --- Timing & Telemetry ---
unsigned long lastTelemetryMillis = 0;
unsigned long lastNpkMillis = 0;
unsigned long crashUnlockAt = 0; 
bool pendingCrashUnlock = false;

WebSocketsServer webSocket = WebSocketsServer(80);

void debugLog(String msg) {
  if (DEBUG_ENA) {
    Serial.print("["); Serial.print(millis()); Serial.print("] "); Serial.println(msg);
  }
}

// ============================================================================
// 1. APP TO ESP32 COMMUNICATION (THE BRIDGE)
// ============================================================================

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      isStreaming = false;
      break;
    case WStype_CONNECTED:
      webSocket.sendTXT(num, "FARMBOT_ID:ESP32_AGRI3D");
      break;
    case WStype_TEXT: {
      String command = (char *)payload;
      command.trim();

      if (command == "START_STREAM") isStreaming = true;
      else if (command == "STOP_STREAM") isStreaming = false;
      else if (command.startsWith("START_SD:")) {
        String fileName = command.substring(9);
        rootFile = SD_MMC.open(fileName, FILE_READ);
        if (rootFile) {
          isStreamingSD = true; waitForOk = false;
        } else {
          webSocket.sendTXT(num, "ERROR:SD_OPEN_FAILED");
        }
      } 
      else if (command == "STOP_SD") {
        isStreamingSD = false; if (rootFile) rootFile.close();
      }
      else if (command.startsWith("SET_LOCATION:")) {
         int commaIdx = command.indexOf(",");
         if (commaIdx != -1) {
           weather_lat = command.substring(13, commaIdx).toFloat();
           weather_lon = command.substring(commaIdx + 1).toFloat();
         }
      }
      else {
        // Raw G-Code forwarding
        NanoSerial.println(command); 
      }
      break;
    }
  }
}

// ============================================================================
// 2. FARMBOT LOGIC & AGRONOMY (THE BRAINS)
// ============================================================================

void process_daily_farming_logic() {
  // TODO: Read schedule.json from SD and trigger jobs based on NTP time
}

void ai_identify_weeds(camera_fb_t *fb) {
  // TODO: (Phase 5) Pass frame buffer to TensorFlow Lite model
  // TODO: Identify weed vs. crop bounding boxes and send M-codes to weeder
}

void executeFertigationCycle(int detectedN, int detectedP, int detectedK) {
  // TODO: Implement fuzzy logic rules to calculate ml volume based on deficits
  // TODO: Translate ml volume to pump runtime milliseconds
  // TODO: Generate M100/M102 custom G-code sequences and send to Nano
}

// --- FreeRTOS Task: Weather API (Non-Blocking) ---
void weatherTask(void *pvParameters) {
  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(weather_lat, 4) + "&longitude=" + String(weather_lon, 4) + "&current=weather_code";
      http.begin(url);
      int httpCode = http.GET();

      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        int codeIdx = payload.indexOf("\"weather_code\":");
        if (codeIdx != -1) {
          int codeValue = payload.substring(codeIdx + 15).toInt();
          // WMO Codes 51+: Drizzle, Rain, Showers, Thunderstorms
          isWeatherGated = (codeValue >= 51); 
        }
      }
      http.end();
    }
    // Block this task for 15 minutes before checking API again
    vTaskDelay(pdMS_TO_TICKS(15 * 60 * 1000)); 
  }
}

// ============================================================================
// 3. GRBL & MOTOR HANDLING (THE ACTUATION LAYER)
// ============================================================================

void handleGrblTelemetry(String msg) {
  webSocket.broadcastTXT(msg); // Forward raw to Terminal

  if (msg == "ok") {
    waitForOk = false;
  }
  else if (msg.startsWith("[CRASH:")) {
    // FIX: Pause SD stream instead of closing file, so it can resume
    isStreamingSD = false; 
    currentState = STATE_ALARM_LOCKED; 
    webSocket.broadcastTXT("ALERT:" + msg); 
    pendingCrashUnlock = true;
    crashUnlockAt = millis() + 500; 
  }
  else if (msg.startsWith("ALARM") && !pendingCrashUnlock) {
    currentState = STATE_ALARM_LOCKED;
    isStreamingSD = false;
  }
}

// --- FIX: Non-Blocking Serial Reader ---
void readNanoSerialNonBlocking() {
  while (NanoSerial.available()) {
    char c = NanoSerial.read();
    if (c == '\n') {
      nanoRxBuffer.trim();
      if (nanoRxBuffer.length() > 0) {
        handleGrblTelemetry(nanoRxBuffer);
      }
      nanoRxBuffer = ""; // Clear buffer for next message
    } else {
      nanoRxBuffer += c; // Accumulate characters
    }
  }
}

// ============================================================================
// SYSTEM BOOT & MAIN LOOP
// ============================================================================

// (streamTask and smartConnect kept identical to your code...)
void streamTask(void *pvParameters) {
  while (true) {
    if (isStreaming && webSocket.connectedClients() > 0) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) { vTaskDelay(10 / portTICK_PERIOD_MS); continue; }
      webSocket.broadcastBIN(fb->buf, fb->len);
      esp_camera_fb_return(fb);
      vTaskDelay(30 / portTICK_PERIOD_MS);
    } else {
      vTaskDelay(150 / portTICK_PERIOD_MS);
    }
  }
}

void setup() {
  Serial.begin(115200);
  NanoSerial.begin(115200, SERIAL_8N1, NANO_RX, NANO_TX);
  
  pinMode(NPK_DERE, OUTPUT); digitalWrite(NPK_DERE, LOW); 
  NpkSerial.begin(9600, SERIAL_8N1, NPK_RX, NPK_TX);
  pinMode(RAIN_PIN_DIGITAL, INPUT_PULLDOWN);

  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  SD_MMC.begin("/sdcard", true);

  // TODO: Call smartConnect() and initialize esp_camera
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Spin up Core 1 tasks
  xTaskCreatePinnedToCore(streamTask, "streamTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(weatherTask, "weatherTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
  webSocket.loop();
  
  // Constantly read Nano without blocking the loop
  readNanoSerialNonBlocking(); 

  // --- Environmental Halt Logic ---
  bool currentRain = digitalRead(RAIN_PIN_DIGITAL); 
  if ((currentRain || isWeatherGated) && !rainDetected) {
    rainDetected = true; 
    if (currentState == STATE_STREAMING_SD) {
      NanoSerial.print("!"); // Feed Hold
      currentState = STATE_RAIN_PAUSED;
    }
  } else if (!currentRain && !isWeatherGated && rainDetected) {
    rainDetected = false;
    // TODO: Add auto-resume logic here (~ command) if desired
  }

  // --- Telemetry Polling ---
  if (millis() - lastTelemetryMillis > 1000) {
    lastTelemetryMillis = millis();
    NanoSerial.print("?"); 
  }

  // --- SD Streaming Flow Control ---
  if (isStreamingSD && !waitForOk && rootFile.available()) {
    String gline = rootFile.readStringUntil('\n');
    gline.trim();
    if (gline.length() > 0 && !gline.startsWith(";") && !gline.startsWith("(")) {
      NanoSerial.println(gline);
      waitForOk = true;
    }
  } else if (isStreamingSD && !rootFile.available()) {
    isStreamingSD = false; rootFile.close();
    webSocket.broadcastTXT("MSG:SD_JOB_COMPLETE");
  }

  // --- Auto-Unlock Recovery ---
  if (pendingCrashUnlock && millis() >= crashUnlockAt) {
    pendingCrashUnlock = false;
    NanoSerial.println("$X");
    // TODO: Set isStreamingSD = true here to automatically resume the SD file
  }
}
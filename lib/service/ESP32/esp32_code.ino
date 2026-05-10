#include "esp_camera.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include "FS.h"
#include "SD_MMC.h"

// --- Weather Adaptive Gating Parameters ---
#define RAINDROP_SENSOR_PIN 1 // Active LOW digital input for rain
Preferences preferences;
float farm_lat = 10.3157; // Default: Cebu City
float farm_lon = 123.8854;

const int TAU_POP = 70;
const int TAU_C = 85;
const int TAU_H = 80;

int G_f = 1;    // 1 = Safe to irrigate, 0 = Resource Conservation Mode
int R_phys = 0; // 0 = Dry, 1 = Rain detected physically

// --- COMPREHENSIVE CONFIG ---
const bool DEBUG_ENA = true;
const char *AP_SSID = "Farmbot_S3_Direct";
const char *AP_PASS = "12345678"; // Min 8 characters

// --- Stream Control ---
bool isStreaming = false; // App controls this!

// --- Adaptive Resolution Ladder ---
framesize_t resolution_ladder[] = {FRAMESIZE_5MP,   // 2592x1944
                                   FRAMESIZE_QSXGA, // 2560x1920
                                   FRAMESIZE_WQXGA, // 2560x1600
                                   FRAMESIZE_QXGA,  // 2048x1536
                                   FRAMESIZE_UXGA,  // 1600x1200
                                   FRAMESIZE_SXGA,  // 1280x1024
                                   FRAMESIZE_XGA,   // 1024x768
                                   FRAMESIZE_SVGA,  // 800x600
                                   FRAMESIZE_VGA,   // 640x480
                                   FRAMESIZE_CIF,   // 400x296
                                   FRAMESIZE_QVGA,  // 320x240
                                   FRAMESIZE_HQVGA, // 240x176
                                   FRAMESIZE_QCIF,  // 176x144
                                   FRAMESIZE_QQVGA, // 160x120
                                   FRAMESIZE_96X96};

const int ladder_size =
    sizeof(resolution_ladder) / sizeof(resolution_ladder[0]);
int current_ladder_idx = 8; // Start at VGA (Index 8) for a safe middle ground
unsigned long last_switch_time = 0;
const int switch_cooldown = 3000;
int bad_frame_count = 0;
int good_frame_count = 0;

// --- WiFi Config ---
struct WifiCreds {
  const char *ssid;
  const char *pass;
};

// Add your home/farm networks here
WifiCreds myNetworks[] = {
    //   {"moto g84 5G", "xxxxxxxx"},
    //   {"FarmRouter", "password123"},
    {"AdminAccess", "Admin@CTU.2024"}};
int networkCount = sizeof(myNetworks) / sizeof(myNetworks[0]);

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

// Host WebSocket on port 80
WebSocketsServer webSocket = WebSocketsServer(80);

// --- Dedicated Nano Serial Bridge ---
#define NANO_RX_PIN 44        // Connect to Nano TX (U0RX)
#define NANO_TX_PIN 43        // Connect to Nano RX (U0TX)
HardwareSerial NanoSerial(1); // UART1 for Nano

// --- SD Card Interface ---
#define SD_CMD 38
#define SD_CLK 39
#define SD_D0  40

void debugLog(String msg) {
  if (DEBUG_ENA) {
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] ");
    Serial.println(msg);
  }
}

// Safely change resolution
void setResolutionIdx(int idx) {
  if (idx < 0 || idx >= ladder_size)
    return;
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    if (s->set_framesize(s, resolution_ladder[idx]) == 0) {
      current_ladder_idx = idx;
      debugLog("ADAPTIVE: Switched to ResIdx [" + String(idx) + "]");
    }
    delay(150); // Give the sensor a moment to adjust
  }
}

// === SD CARD FUNCTIONS ===
void saveDataToSD(String filename, String data) {
    if(!SD_MMC.begin("/sdcard", true)) { // 1-bit mode
        debugLog("SD: Card Mount Failed");
        return;
    }
    File file = SD_MMC.open(filename.c_str(), FILE_APPEND);
    if(!file) {
        debugLog("SD: Failed to open file for appending");
        return;
    }
    if(file.println(data)) {
        debugLog("SD: Data appended");
    } else {
        debugLog("SD: Append failed");
    }
    file.close();
}

void saveImageToSD(camera_fb_t *fb) {
    if(!SD_MMC.begin("/sdcard", true)) { 
        return;
    }
    String path = "/img_" + String(millis()) + ".jpg";
    File file = SD_MMC.open(path.c_str(), FILE_WRITE);
    if(!file) {
        debugLog("SD: Failed to open file for writing image");
        return;
    }
    file.write(fb->buf, fb->len);
    file.close();
    debugLog("SD: Saved image to " + path);
}

// === MAMDANI FUZZY LOGIC CONTROLLER ===
// Fuzzification Functions
float fuzzyTri(float x, float a, float b, float c) {
    return max(min((x - a) / (b - a), (c - x) / (c - b)), 0.0f);
}

float fuzzyTrap(float x, float a, float b, float c, float d) {
    return max(min(min((x - a) / (b - a), 1.0f), (d - x) / (d - c)), 0.0f);
}

// Inference Engine (Returns Irrigation Volume V_w in mL)
float evaluateIrrigationVolume(float sm, float ec) {
    // 1. Fuzzify Inputs (Parameters tuned for default conceptual validation)
    float sm_dry = fuzzyTrap(sm, 0.0, 0.0, 20.0, 40.0);
    float sm_opt = fuzzyTri(sm, 30.0, 50.0, 70.0);
    float sm_wet = fuzzyTrap(sm, 60.0, 80.0, 100.0, 100.0);

    float ec_safe = fuzzyTrap(ec, 0.0, 0.0, 1.0, 1.5);
    float ec_caut = fuzzyTri(ec, 1.0, 1.5, 2.5);
    float ec_high = fuzzyTrap(ec, 2.0, 3.0, 5.0, 5.0);

    // 2. Rule Evaluation
    // Base rule strengths
    float rule_high = sm_dry;
    float rule_low  = sm_opt;
    float rule_med  = ec_high;
    float rule_zero = max(sm_wet, min(sm_wet, ec_high));

    // 3. Defuzzification (Centroid approximation simplified to singletons)
    // Assuming Volume bounds [0 - 1000 mL]
    float w_high = 1000.0, w_med = 500.0, w_low = 250.0, w_zero = 0.0;
    
    float numerator = (rule_high * w_high) + (rule_med * w_med) + (rule_low * w_low) + (rule_zero * w_zero);
    float denominator = rule_high + rule_med + rule_low + rule_zero;

    if (denominator == 0) return 0.0;
    return numerator / denominator;
}

// Smart Connect logic
void smartConnect() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  delay(500);

  debugLog("WIFI: Scanning for known networks...");
  int n = WiFi.scanNetworks();
  int bestIdx = -1;
  int bestRSSI = -1000;

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < networkCount; j++) {
      if (myNetworks[j].ssid != NULL && WiFi.SSID(i) == myNetworks[j].ssid) {
        if (WiFi.RSSI(i) > bestRSSI) {
          bestRSSI = WiFi.RSSI(i);
          bestIdx = j;
        }
      }
    }
  }

  if (bestIdx != -1) {
    debugLog("WIFI: Connecting to " + String(myNetworks[bestIdx].ssid));
    WiFi.begin(myNetworks[bestIdx].ssid, myNetworks[bestIdx].pass);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      if (DEBUG_ENA)
        Serial.print(".");
      attempts++;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    debugLog("\nWIFI: Connected! IP: " + WiFi.localIP().toString());
  } else {
    debugLog("\nWIFI: No network found. Starting Hotspot...");
    WiFi.softAP(AP_SSID, AP_PASS);
    debugLog("HOTSPOT: Active! Connect to IP 192.168.4.1");
  }
}

// === IP GEOLOCATION (AUTO-LOCATE) ===
void autoLocate() {
  debugLog("LOC: Attempting IP Geolocation...");
  HTTPClient http;
  http.begin("http://ip-api.com/json/");
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      float new_lat = doc["lat"];
      float new_lon = doc["lon"];
      if (new_lat != 0.0 && new_lon != 0.0) {
        farm_lat = new_lat;
        farm_lon = new_lon;
        preferences.begin("agri-3d", false);
        preferences.putFloat("lat", farm_lat);
        preferences.putFloat("lon", farm_lon);
        preferences.end();
        debugLog("LOC: Auto-Located! Lat: " + String(farm_lat) +
                 ", Lon: " + String(farm_lon));
      }
    }
  } else {
    debugLog("LOC: IP Geolocation failed. Using saved/default coordinates.");
  }
  http.end();
}

// === WEBSOCKET EVENT HANDLER ===
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload,
                    size_t length) {
  switch (type) {
  case WStype_DISCONNECTED:
    debugLog("WS: Client Disconnected");
    isStreaming = false; // Auto-stop stream if client drops
    break;

  case WStype_CONNECTED:
    debugLog("WS: Client Connected!");
    webSocket.sendTXT(num, "FARMBOT_ID:ESP32_CAM");
    break;

  case WStype_TEXT: {
    String command = (char *)payload;
    command.trim();

    // App Stream Controls
    if (command == "START_STREAM") {
      isStreaming = true;
      debugLog("STREAM: Started by App");
    } else if (command == "STOP_STREAM") {
      isStreaming = false;
      debugLog("STREAM: Stopped by App");
    }
    // --- JUMP TO NEW BASELINE RESOLUTION ---
    else if (command.startsWith("SET_RES:")) {
      int resIdx = command.substring(8).toInt();
      setResolutionIdx(resIdx);

      // Reset the adaptive engine so it cleanly evaluates this new starting
      // point
      last_switch_time = millis();
      bad_frame_count = 0;
      good_frame_count = 0;
      debugLog("ADAPTIVE: App requested new baseline. Adapting from here...");
    }
    // --- SET LOCATION FOR OPEN-METEO ---
    else if (command.startsWith("SET_LOC:")) {
      int commaIdx = command.indexOf(',');
      if (commaIdx > 8) {
        farm_lat = command.substring(8, commaIdx).toFloat();
        farm_lon = command.substring(commaIdx + 1).toFloat();
        preferences.begin("agri-3d", false);
        preferences.putFloat("lat", farm_lat);
        preferences.putFloat("lon", farm_lon);
        preferences.end();
        debugLog("LOC: Updated coords to " + String(farm_lat) + ", " +
                 String(farm_lon));
      }
    }
    // -------------------------------------------------------------
    // FLUTTER -> NANO COMMANDS
    // Forward everything else to Arduino Nano
    // -------------------------------------------------------------
    else {
      // Weather-Adaptive GATING INTERCEPT
      if (command.indexOf("M8") != -1) { // Example: M8 = Pump On
        R_phys = !digitalRead(
            RAINDROP_SENSOR_PIN); // Assuming active LOW sensor (0=rain)
        if (G_f == 0 && R_phys == 1) {
          debugLog("GATING: Physical Rain Detected! Irrigation Blocked.");
          webSocket.broadcastTXT("=WEATHER_PAUSE=");
          return; // BLOCKED
        } else if (G_f == 0) {
          debugLog("GATING: Forecast demands Rain! Irrigation Delayed.");
          webSocket.broadcastTXT("=WEATHER_PAUSE=");
          return; // BLOCKED
        }
      }

      debugLog("RX App->Nano: " + command);
      NanoSerial.println(command); // Send to Arduino Nano via dedicated UART
    }
    break;
  }
  }
}

// === WEATHER PREDICTIVE POLLING TASK ===
void weatherTask(void *pvParameters) {
  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      // Open-Meteo URL (Free, no API key required)
      String url =
          "http://api.open-meteo.com/v1/forecast?latitude=" + String(farm_lat) +
          "&longitude=" + String(farm_lon) +
          "&current=relative_humidity_2m,cloud_cover,precipitation&hourly="
          "precipitation_probability";

      debugLog("WEATHER: Fetching API...");
      http.begin(url);
      int httpCode = http.GET();
      if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);
        if (!error) {
          int R_h = doc["current"]["relative_humidity_2m"];
          int C_c = doc["current"]["cloud_cover"];
          int P_pop = doc["hourly"]["precipitation_probability"][0];

          // Weather-Adaptive Decision Algorithm (G_f computation)
          if (P_pop >= TAU_POP || (C_c >= TAU_C && R_h >= TAU_H)) {
            G_f = 0; // Rain likely (Conservation Mode)
            debugLog("WEATHER: Forecast -> RAIN (Conservation Mode Active)");
          } else {
            G_f = 1; // Clear
            debugLog("WEATHER: Forecast -> CLEAR (Safe to Irrigate)");
          }
        } else {
          debugLog("WEATHER: JSON parsing failed.");
        }
      } else {
        debugLog("WEATHER: API Call Failed.");
      }
      http.end();
    }
    vTaskDelay((15 * 60 * 1000) / portTICK_PERIOD_MS); // Poll every 15 minutes
  }
}

// === SEPARATE THREAD FOR CAMERA STREAMING ===
void streamTask(void *pvParameters) {
  while (true) {
    if (isStreaming && webSocket.connectedClients() > 0) {

      unsigned long start_time = millis();

      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }

      webSocket.broadcastBIN(fb->buf, fb->len);

      unsigned long frame_time = millis() - start_time;
      esp_camera_fb_return(fb);

      // --- ADAPTIVE RESOLUTION ENGINE ---
      unsigned long now = millis();

      // Evaluates stability and adjusts up or down automatically
      if (now - last_switch_time > switch_cooldown) {
        if (frame_time > 100) {
          bad_frame_count++;
          good_frame_count = 0;
        } else if (frame_time < 50) {
          good_frame_count++;
          bad_frame_count = 0;
        }

        // Drop resolution if network is struggling
        if (bad_frame_count >= 2 && current_ladder_idx < ladder_size - 1) {
          setResolutionIdx(current_ladder_idx + 1);
          last_switch_time = now;
          bad_frame_count = 0;
        }
        // Increase resolution if network is breezing through frames
        if (good_frame_count >= 10 && current_ladder_idx > 0) {
          setResolutionIdx(current_ladder_idx - 1);
          last_switch_time = now;
          good_frame_count = 0;
        }
      }

      vTaskDelay(30 / portTICK_PERIOD_MS);
    } else {
      vTaskDelay(150 / portTICK_PERIOD_MS);
    }
  }
}

// === TELEMETRY BROADCAST TASK ===
void telemetryTask(void *pvParameters) {
  while (true) {
    if (webSocket.connectedClients() > 0) {
        DynamicJsonDocument doc(512);
        doc["type"] = "telemetry";
        doc["G_f"] = G_f;
        doc["R_phys"] = R_phys;
        doc["lat"] = farm_lat;
        doc["lon"] = farm_lon;
        doc["res_idx"] = current_ladder_idx;
        doc["free_heap"] = ESP.getFreeHeap();
        
        String output;
        serializeJson(doc, output);
        webSocket.broadcastTXT(output);
    }
    vTaskDelay((5000) / portTICK_PERIOD_MS); // 5 seconds
  }
}

void setup() {
  if (DEBUG_ENA)
    Serial.begin(115200); // Dedicated to Laptop / Serial Monitor

  // Start dedicated communication with Arduino Nano
  NanoSerial.begin(115200, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);

  pinMode(RAINDROP_SENSOR_PIN, INPUT_PULLUP);
  preferences.begin("agri-3d", false);
  farm_lat = preferences.getFloat("lat", 10.3157); // Load saved coordinates
  farm_lon = preferences.getFloat("lon", 123.8854);
  preferences.end();

  delay(2000);
  if (psramInit())
    debugLog("SYSTEM: PSRAM Found.");

  // Init SD_MMC in 1-bit mode using custom pins
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if(!SD_MMC.begin("/sdcard", true)) {
      debugLog("SYSTEM: SD Card Mount Failed");
  } else {
      debugLog("SYSTEM: SD Card Mounted");
  }

  smartConnect();

  // Auto-locate via IP if connected to the internet
  if (WiFi.status() == WL_CONNECTED) {
    autoLocate();
  }

  // Camera Init
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.frame_size = resolution_ladder[current_ladder_idx];

  esp_camera_init(&config);
  MDNS.begin("farmbot");

  // Start WebSockets
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Spin up stream task on Core 1
  xTaskCreatePinnedToCore(streamTask, "streamTask", 4096, NULL, 1, NULL, 1);
  // Spin up weather task on Core 0 (networking core)
  xTaskCreatePinnedToCore(weatherTask, "weatherTask", 8192, NULL, 1, NULL, 0);
  // Spin up telemetry task
  xTaskCreatePinnedToCore(telemetryTask, "telemetryTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
  webSocket.loop();

  // -------------------------------------------------------------
  // NANO -> FLUTTER DATA
  // Listen to Nano over dedicated serial and broadcast to Flutter
  // -------------------------------------------------------------
  if (NanoSerial.available()) {
    String msg = NanoSerial.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      webSocket.broadcastTXT(msg);
    }
  }
}
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "AI_AGRI3D/AI_AGRI3D.h"


// ── Configuration ──
const char *ssid = "Jiji";
const char *password = "skyd4nc3-r41nd4nc3";
const char *mdnsName = "farmbot";

// ── Simplified Logging Macro ──
#define LOG(x) Serial.print(x)
#define LOGLN(x) Serial.println(x)
#define LOGF(...) Serial.printf(__VA_ARGS__)

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP udp;

// ── Timing and Simulation ──
unsigned long lastBroadcast = 0;
unsigned long lastSensor = 0;

// ── PING/PONG Tracking ──
unsigned long lastPingTime = 0; // millis() when last PING was received
unsigned long pingCount = 0;    // total PINGs received

// ── Nano Connection Tracking ──
unsigned long lastNanoReply = 0;  // millis() of last Nano response
unsigned long lastNanoStatus = 0; // millis() of last nano_connected broadcast
bool nanoConnected = false;       // current Nano connection state
const unsigned long NANO_TIMEOUT_MS = 3000;      // 3s silence = disconnected
const unsigned long NANO_STATUS_INTERVAL = 5000; // broadcast every 5s

// ── G-Code Defaults ──
const int DEFAULT_FEEDRATE = 1000;

// ── Background Timer Variables for Fertilizer ──
bool isFertilizing = false;
unsigned long fertilizeEndTime = 0;

// ── LOCAL GANTRY STATE ──────────────────────────────────────
String grblState = "Unknown";
float currentX = 0.0;
float currentY = 0.0;
float currentZ = 0.0;

float maxX = 1000.0;
float maxY = 1000.0;
float maxZ = 1000.0;

void parseGrblStatus(String statusMsg) {
  int firstPipe = statusMsg.indexOf('|');
  if (firstPipe > 1) {
    grblState = statusMsg.substring(1, firstPipe);
  }

  int posStart = statusMsg.indexOf("MPos:");
  if (posStart == -1)
    posStart = statusMsg.indexOf("WPos:");

  if (posStart != -1) {
    posStart += 5;
    int posEnd = statusMsg.indexOf('|', posStart);
    if (posEnd == -1)
      posEnd = statusMsg.indexOf('>', posStart);

    if (posEnd != -1) {
      String posStr = statusMsg.substring(posStart, posEnd);
      int comma1 = posStr.indexOf(',');
      int comma2 = posStr.indexOf(',', comma1 + 1);

      if (comma1 != -1 && comma2 != -1) {
        currentX = posStr.substring(0, comma1).toFloat();
        currentY = posStr.substring(comma1 + 1, comma2).toFloat();
        currentZ = posStr.substring(comma2 + 1).toFloat();
      }
    }
  }
}
// ─────────────────────────────────────────────────────────────

// Forward declaration so onEvent() can call handleWebSocketMessage()
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len,
                            AsyncWebSocketClient *client);

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len,
                            AsyncWebSocketClient *client) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len &&
      info->opcode == WS_TEXT) {
    data[len] = 0;
    String received = (char *)data;
    received.trim();

    LOGF("[Flutter → ESP32] %s\n", received.c_str());

    // 1. INTERCEPT THE FERTILIZER COMMAND
    if (received.startsWith("M7 ml")) {
      int targetVolume = received.substring(5).toInt();
      LOGF(">>> Intercepted Fertilizer: Pumping %d mL\n", targetVolume);

      Serial1.print("M7\n"); // FIXED: Single newline

      unsigned long durationMs = targetVolume * 100;
      isFertilizing = true;
      fertilizeEndTime = millis() + durationMs;
      return;
    }

    // 2. ALLOW STANDARD GRBL COMMANDS TO PASS
    else if (received.startsWith("G") || received.startsWith("$") ||
             received.startsWith("M") || received.startsWith("?")) {
      // Automatically add Feedrate if missing from "g1 x10"
      if ((received.startsWith("G0") || received.startsWith("G1")) &&
          received.indexOf('F') == -1) {
        received += " F" + String(DEFAULT_FEEDRATE);
      }

      LOGF(">>> EXECUTING GRBL CMD: %s\n", received.c_str());

      Serial1.print(received + "\n");

      JsonDocument response;
      response["status"] = "executing";
      response["cmd"] = received;
      String out;
      serializeJson(response, out);
      ws.textAll(out);
    }
    // ── IMPROVED PING/PONG ──
    else if (received == "PING") {
      pingCount++;
      lastPingTime = millis();

      // Build a rich PONG response with uptime and ping count
      JsonDocument pong;
      pong["status"] = "PONG";
      pong["uptime_ms"] = lastPingTime; // ms since ESP32 booted
      pong["ping_no"] = pingCount;      // how many pings received
      pong["ip"] = WiFi.localIP().toString();

      String pongOut;
      serializeJson(pong, pongOut);

      // Reply ONLY to the client who sent the PING (not broadcast)
      client->text(pongOut);

      LOGF("[PING #%lu] PONG sent → uptime: %lums\n", pingCount, lastPingTime);
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    LOGLN("✓ Flutter App Connected!");
    client->text("{\"system\":\"AGRI_3D\", \"msg\":\"CONNECTED\", \"x\":" +
                 String(currentX) + ", \"y\":" + String(currentY) +
                 ", \"z\":" + String(currentZ) + ", \"maxX\":" + String(maxX) +
                 ", \"maxY\":" + String(maxY) + "}");
    Serial1.print("?");
  } else if (type == WS_EVT_DISCONNECT) {
    LOGLN("✗ Flutter App Disconnected!");
    pingCount = 0; // Reset ping counter on disconnect
  } else if (type == WS_EVT_DATA) {
    handleWebSocketMessage(arg, data, len,
                           client); // Pass client for targeted PONG
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, 44, 43); // Nano TX/RX Pins

  LOGLN("\n┌─────────────────────────────────┐");
  LOGLN("│     AGRI 3D CONTROL SYSTEM      │");
  LOGLN("└─────────────────────────────────┘");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    LOG(".");
  }

  LOGLN("\n✓ WiFi Connected!");
  LOGF("  IP: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin(mdnsName))
    MDNS.addService("http", "tcp", 80);

  udp.begin(4210);
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();
  LOGLN("✓ WebSocket Server Active.");
  
  // Initialize AI_AGRI3D
  aiSystem.begin();
}

void loop() {
  // Background Timer for the Fertilizer Pump
  if (isFertilizing && millis() >= fertilizeEndTime) {
    isFertilizing = false;
    Serial1.print("M9\n"); // FIXED: Single newline
    LOGLN(">>> Fertilizer Timer Finished: Sent M9");
  }

  // Listen for responses from Nano (GRBL)
  if (Serial1.available()) {
    String nanoReply = Serial1.readStringUntil('\n');
    nanoReply.trim();

    if (nanoReply.length() > 0) {
      lastNanoReply = millis(); // ── Mark Nano as alive
      ws.textAll("{\"nano_raw\":\"" + nanoReply + "\"}");
      LOGF("[NANO] %s\n", nanoReply.c_str());

      delay(10); // Buffer Overflow Protection

      if (nanoReply.startsWith("$130="))
        maxX = nanoReply.substring(5).toFloat();
      else if (nanoReply.startsWith("$131="))
        maxY = nanoReply.substring(5).toFloat();
      else if (nanoReply.startsWith("$132="))
        maxZ = nanoReply.substring(5).toFloat();
      else if (nanoReply == "ok")
        Serial1.print("?");
      else if (nanoReply.startsWith("<") && nanoReply.endsWith(">"))
        parseGrblStatus(nanoReply);
    }
  }

  // ── Nano Connection Status Broadcast ──
  if (millis() - lastNanoStatus >= NANO_STATUS_INTERVAL) {
    lastNanoStatus = millis();
    bool isNanoAlive =
        (lastNanoReply > 0) && (millis() - lastNanoReply < NANO_TIMEOUT_MS);

    if (isNanoAlive != nanoConnected) {
      nanoConnected = isNanoAlive;
      LOGF("[NANO] Status changed → %s\n",
           nanoConnected ? "CONNECTED" : "DISCONNECTED");
    }

    // Always broadcast current nano status so Flutter stays in sync
    JsonDocument nanoStatus;
    nanoStatus["nano_connected"] = nanoConnected;
    // Use signed long to safely represent -1 (never replied) or elapsed ms
    long silentMs =
        (lastNanoReply == 0) ? -1L : (long)(millis() - lastNanoReply);
    nanoStatus["nano_silent_ms"] = silentMs;
    String nanoOut;
    serializeJson(nanoStatus, nanoOut);
    ws.textAll(nanoOut);
  }

  // FIXED: Continuous Status Polling While Moving
  static unsigned long lastStatusTime = 0;
  if ((grblState == "Run" || grblState == "Jog" || grblState == "Home") &&
      (millis() - lastStatusTime >= 200)) {
    lastStatusTime = millis();
    Serial1.print("?");
  }

  // UDP Discovery
  if (millis() - lastBroadcast > 3000) {
    lastBroadcast = millis();
    IPAddress broadcastIP = WiFi.localIP();
    broadcastIP[3] = 255;
    udp.beginPacket(broadcastIP, 4210);
    udp.print("AGRI3D_DISCOVERY:");
    udp.print(WiFi.localIP().toString());
    udp.endPacket();
  }

  ws.cleanupClients();
}
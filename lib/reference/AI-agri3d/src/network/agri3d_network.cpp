#include "agri3d_network.h"
#include "../core/agri3d_logger.h"
#include "agri3d_commands.h"
#include "agri3d_config.h"
#include "agri3d_state.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <mbedtls/md.h> // For HMAC Security

// ── WebSocket server ───────────────────────────────────────────────────────
WebSocketsServer webSocket(WS_PORT);

// ── Thread-safe log queue (Core 1 → Core 0) ──────────────────────────────
// WebSocketsServer is NOT thread-safe. Any broadcast from Core 1 (routine
// task) must be queued and sent from the Core-0 network loop.
#define LOG_QUEUE_DEPTH 32
#define LOG_QUEUE_MSG_LEN 304  // matches AgriLog buffer (300) + null

struct LogQueueItem {
    char msg[LOG_QUEUE_MSG_LEN];
};

static QueueHandle_t _logQueue = NULL;

void broadcastLog(const char* log) {
    if (xPortGetCoreID() == 0) {
        // Already on Core 0 — safe to send directly
        webSocket.broadcastTXT(log);
        return;
    }
    // On Core 1 — enqueue for Core 0 to drain
    if (_logQueue) {
        LogQueueItem item;
        strncpy(item.msg, log, LOG_QUEUE_MSG_LEN - 1);
        item.msg[LOG_QUEUE_MSG_LEN - 1] = '\0';
        xQueueSend(_logQueue, &item, 0); // non-blocking: drop if full
    }
}

/// Called from the Core-0 network loop to flush queued log messages.
void drainLogQueue() {
    if (!_logQueue) return;
    LogQueueItem item;
    while (xQueueReceive(_logQueue, &item, 0) == pdTRUE) {
        webSocket.broadcastTXT(item.msg);
    }
}

// ── Singleton client tracking ──────────────────────────────────────────────
int8_t activeClientNum = -1; // -1 = no client connected

// ── Internal state ─────────────────────────────────────────────────────────
static bool _apModeActive = false;
static WiFiUDP _udp;
static unsigned long _lastBeacon = 0;
static unsigned long _lastRetry = 0;

// ── Security & Client State ────────────────────────────────────────────────
enum ClientState { DISCONNECTED, AUTH_PENDING, AUTHENTICATED };
ClientState currentClientState = DISCONNECTED;
String currentChallengeNonce = "";
unsigned long lastHeartbeat = 0;
static const unsigned long COMM_TIMEOUT_MS = 60000; // Must exceed Flutter ping watchdog (20 missed x 2s = 40s)

TaskHandle_t networkTaskHandle = NULL;
static bool _pendingChallenge = false;

// ── Known WiFi networks ────────────────────────────────────────────────────
struct WifiCred {
  const char *ssid;
  const char *pass;
};

static const WifiCred knownNetworks[WIFI_NET_COUNT] = {
    {WIFI_NET_0_SSID, WIFI_NET_0_PASS},
    {WIFI_NET_1_SSID, WIFI_NET_1_PASS},
    {WIFI_NET_2_SSID, WIFI_NET_2_PASS}};

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * @brief Safely extracts a parameter from the URI string
 */
// Helper: Generate HMAC SHA256 string
String generateHMAC(String payload, String key) {
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload.c_str(), payload.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    String hashStr = "";
    for (int i = 0; i < 32; i++) {
        char buf[3];
        sprintf(buf, "%02x", hmacResult[i]);
        hashStr += buf;
    }
    return hashStr;
}

void buildAndSendPong(uint8_t num) {
    String pongMsg = "{\"evt\":\"PONG\",\"nano\":\"";
    pongMsg += sysState.getNano() == NANO_CONNECTED ? "CONNECTED" : "DISCONNECTED";
    pongMsg += "\",\"plants\":[";
    
    // Plant map piggybacking temporarily disabled (requires SD read or global state)
    pongMsg += "]}";
    
    webSocket.sendTXT(num, pongMsg);
}

static String getUriParam(const String &uri, const String &param) {
  String searchStr = param + "=";
  int start = uri.indexOf(searchStr);
  if (start == -1)
    return "";

  start += searchStr.length();
  int end = uri.indexOf('&', start);
  if (end == -1)
    end = uri.length();

  String val = uri.substring(start, end);
  val.trim();
  return val;
}

static bool tryConnect(const char *ssid, const char *pass) {
  AgriLog(TAG_NET, LEVEL_INFO, "Trying: %s", ssid);
  WiFi.begin(ssid, pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= WIFI_CONNECT_TIMEOUT_MS) {
      WiFi.disconnect(true);
      return false;
    }
    delay(250);
  }
  return true;
}

static void onStationConnected() {
  sysState.setWifi(WIFI_CONNECTED);
  AgriLog(TAG_NET, LEVEL_SUCCESS, "✓ WiFi connected → %s",
          WiFi.localIP().toString().c_str());

  AgriLog(TAG_NET, LEVEL_INFO, "========================================");
  AgriLog(TAG_NET, LEVEL_SUCCESS, "WIFI CONNECTED! IP ADDRESS: %s",
          WiFi.localIP().toString().c_str());
  AgriLog(TAG_NET, LEVEL_INFO, "========================================\n");

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("ws", "tcp", WS_PORT);
    AgriLog(TAG_NET, LEVEL_SUCCESS, "mDNS: ws://%s.local:%d", MDNS_HOSTNAME,
            WS_PORT);
  }

  _udp.begin(UDP_DISCOVERY_PORT);

  if (_apModeActive)
    stopAPMode();
}

// ============================================================================
// AP MODE
// ============================================================================

void startAPMode() {
  if (_apModeActive)
    return;
  _apModeActive = true;

  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, AP_MAX_CONN);
  AgriLog(TAG_NET, LEVEL_INFO, "AP started: SSID=%s  IP=%s", AP_SSID,
          WiFi.softAPIP().toString().c_str());

  _udp.begin(UDP_DISCOVERY_PORT);
  sysState.setWifi(WIFI_DISCONNECTED);
}

void stopAPMode() {
  if (!_apModeActive)
    return;
  _apModeActive = false;
  WiFi.softAPdisconnect(true);
  AgriLog(TAG_NET, LEVEL_INFO, "AP stopped — switched to station mode");
}

bool isAPMode() { return _apModeActive; }

// ============================================================================
// BACKGROUND WIFI RETRY TASK
// ============================================================================

static TaskHandle_t _retryTaskHandle = nullptr;

static void wifiRetryTask(void * /*pvParameters*/) {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS));

    if (WiFi.status() == WL_CONNECTED) {
      vTaskDelete(nullptr);
      return;
    }

    sysState.setWifi(WIFI_CONNECTING);
    AgriLog(TAG_NET, LEVEL_INFO, "Background WiFi retry...");

    for (int i = 0; i < WIFI_NET_COUNT; i++) {
      if (tryConnect(knownNetworks[i].ssid, knownNetworks[i].pass)) {
        onStationConnected();
        vTaskDelete(nullptr);
        return;
      }
    }

    sysState.setWifi(WIFI_DISCONNECTED);
    AgriLog(TAG_NET, LEVEL_WARN, "Retry failed — staying in AP mode");
  }
}

// ============================================================================
// WEBSOCKET EVENT — SINGLETON ENFORCEMENT & GHOST FILTER
// ============================================================================

static void wsEventWrapper(uint8_t num, WStype_t type, uint8_t *payload,
                           size_t length) {
  switch (type) {
  case WStype_CONNECTED: {
    AgriLog(TAG_NET, LEVEL_INFO, "🔌 New client connected (Slot #%d)", num);

    if (activeClientNum != -1 && activeClientNum != (int8_t)num) {
      webSocket.disconnect(activeClientNum);
      sysState.setStreaming(false);
    }

    activeClientNum = (int8_t)num;
    currentClientState = AUTH_PENDING; 
    lastHeartbeat = millis();

    // Buffer the CHALLENGE message so we don't violate WS protocol by sending instantly
    currentChallengeNonce = String(random(100000, 999999));
    _pendingChallenge = true;
    break;
  }
  case WStype_DISCONNECTED: {
    if ((int8_t)num == activeClientNum) {
      activeClientNum = -1;
      currentClientState = DISCONNECTED;
      sysState.setFlutter(FLUTTER_DISCONNECTED);
      if (sysState.isStreaming()) sysState.setStreaming(false);
      AgriLog(TAG_NET, LEVEL_WARN, "Client disconnected");
    }
    break;
  }
  case WStype_PING:
  case WStype_PONG:
    // WebSocket-level control frames: reset heartbeat so the deadman switch
    // doesn't fire during OS-level keep-alive pings from Flutter.
    lastHeartbeat = millis();
    break;
  case WStype_TEXT: {
    String msg = String((char *)payload);
    lastHeartbeat = millis();
    
    // DEBUG LOG:
    AgriLog(TAG_NET, LEVEL_INFO, "RX (#%d): %s", num, msg.c_str());

    // Bypass hash check, but still require Flutter to send the AUTH packet
    if (currentClientState == AUTH_PENDING) {
      if (msg.indexOf("\"cmd\":\"AUTH\"") > 0) {
        currentClientState = AUTHENTICATED;
        sysState.setFlutter(FLUTTER_CONNECTED);
        sysState.resetFlutterWatchdog();
        sysState.broadcast();
        webSocket.sendTXT(num, "{\"evt\":\"AUTH_SUCCESS\"}");
        webSocket.sendTXT(num, "{\"evt\":\"SYSTEM_STATE\", \"system\":\"AGRI_3D\"}");
        AgriLog(TAG_NET, LEVEL_SUCCESS, "🔒 Client Authenticated (SECURITY BYPASSED)");
      }
      return; 
    }

    if (currentClientState == AUTHENTICATED) {
      if (msg.indexOf("\"cmd\":\"PING\"") > 0) {
        buildAndSendPong(num);
        sysState.resetFlutterWatchdog();
      } else {
        webSocketEvent(num, type, payload, length);
      }
    }
    break;
  }
  case WStype_BIN: {
    if (currentClientState == AUTHENTICATED) {
        lastHeartbeat = millis();
        sysState.resetFlutterWatchdog();
        webSocketEvent(num, type, payload, length);
    }
    break;
  }
}
}

// ============================================================================
// PUBLIC API
// ============================================================================

void networkCoreZeroTask(void *pvParameters);

void networkInit() {
  sysState.setWifi(WIFI_CONNECTING);

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm); // Boost TX power to max for enclosed housings
  bool connected = false;

  for (int i = 0; i < WIFI_NET_COUNT && !connected; i++) {
    connected = tryConnect(knownNetworks[i].ssid, knownNetworks[i].pass);
  }

  if (connected) {
    onStationConnected();
  } else {
    AgriLog(TAG_NET, LEVEL_WARN, "All networks failed — starting AP fallback");
    WiFi.mode(WIFI_AP_STA);
    startAPMode();

    xTaskCreatePinnedToCore(wifiRetryTask, "wifiRetry", 4096, nullptr, 1,
                            &_retryTaskHandle, 0);
  }

  // Create the thread-safe log queue BEFORE starting the WebSocket server
  // so Core-1 routines can safely enqueue log messages from their first tick.
  _logQueue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(LogQueueItem));

  webSocket.begin();
  webSocket.onEvent(wsEventWrapper);
  
  xTaskCreatePinnedToCore(
        networkCoreZeroTask,   /* Task function. */
        "NetTask",             /* Name of task. */
        8192,                  /* Stack size of task */
        NULL,                  /* Parameter of the task */
        3,                     /* Priority of the task (High for network) */
        &networkTaskHandle,    /* Task handle to keep track of created task */
        0);                    /* Pin task to core 0 */
        
  AgriLog(TAG_NET, LEVEL_SUCCESS, "WebSocket server started on port %d",
          WS_PORT);
}

void sendDiscoveryBeacon() {
  IPAddress broadcastIP;

  if (WiFi.status() == WL_CONNECTED) {
    broadcastIP = WiFi.localIP();
  } else if (_apModeActive) {
    broadcastIP = WiFi.softAPIP();
  } else {
    return;
  }

  broadcastIP[3] = 255;

  _udp.beginPacket(broadcastIP, UDP_DISCOVERY_PORT);
  _udp.print("AGRI3D_DISCOVERY:");
  _udp.print(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                           : WiFi.softAPIP().toString());
  _udp.endPacket();
}

void networkCoreZeroTask(void *pvParameters) {
  AgriLog(TAG_NET, LEVEL_INFO, "Network Task initialized on Core %d", xPortGetCoreID());
  while (true) {
    webSocket.loop();

    // Drain log messages queued by Core-1 tasks (thread-safe handoff)
    drainLogQueue();

    if (_pendingChallenge && activeClientNum != -1) {
      _pendingChallenge = false;
      String challengeMsg = "{\"evt\":\"CHALLENGE\",\"nonce\":\"" + currentChallengeNonce + "\"}";
      webSocket.sendTXT(activeClientNum, challengeMsg);
    }

    // ── Zero-Copy Frame Handoff ──
    if (sysState.pendingFrameFB != nullptr) {
      if (sysState.pendingFrameClient >= 0) {
        // If the TCP buffer is full (due to high latency), sendBIN will return false.
        // For live streaming, it is better to DROP the frame than to queue it
        // and cause a 1002 Protocol Error or massive video delay.
        bool sentOk = webSocket.sendBIN((uint8_t)sysState.pendingFrameClient,
                                        sysState.pendingFrame, sysState.pendingFrameLen);
        
        if (sentOk) {
            if (sysState.pendingAiResult != nullptr) {
              webSocket.sendTXT((uint8_t)sysState.pendingFrameClient, *sysState.pendingAiResult);
              delete sysState.pendingAiResult;
              sysState.pendingAiResult = nullptr;
            }
        } else {
            AgriLog(TAG_NET, LEVEL_WARN, "TCP Buffer Full! Dropping video frame (Latency too high)");
            if (sysState.pendingAiResult != nullptr) {
                delete sysState.pendingAiResult;
                sysState.pendingAiResult = nullptr;
            }
        }
      }
      
      // Buffer the websocket: Yield to let LwIP TCP buffer drain before freeing frame.
      // Increased base delay for high latency conditions.
      vTaskDelay(pdMS_TO_TICKS(100 + (sysState.pendingFrameLen / 2048)));
      
      esp_camera_fb_return(sysState.pendingFrameFB);
      sysState.pendingFrameFB = nullptr;
      sysState.pendingFrame = nullptr;
      sysState.pendingFrameLen = 0;
      sysState.pendingFrameClient = -1;
    }

    // UDP discovery beacon
    if (millis() - _lastBeacon >= UDP_BROADCAST_INTERVAL) {
      _lastBeacon = millis();
      sendDiscoveryBeacon();
    }

    // Deadman's Switch
    if (currentClientState != DISCONNECTED && (millis() - lastHeartbeat > COMM_TIMEOUT_MS)) {
        AgriLog(TAG_NET, LEVEL_ERR, "⚠ Watchdog Timeout - Dropping Client");
        webSocket.disconnect(activeClientNum);
        activeClientNum = -1;
        currentClientState = DISCONNECTED;
        sysState.setFlutter(FLUTTER_DISCONNECTED);
        if (sysState.isStreaming()) sysState.setStreaming(false);
    }

    vTaskDelay(pdMS_TO_TICKS(10)); 
  }
}

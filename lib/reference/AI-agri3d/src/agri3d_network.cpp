/**
 * @file agri3d_network.cpp
 * @brief WiFi management, AP fallback, mDNS, UDP discovery, WebSocket server.
 */

#include "agri3d_network.h"
#include "agri3d_commands.h" // webSocketEvent() handler
#include "agri3d_config.h"
#include "agri3d_state.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ── WebSocket server (singleton defined here, declared extern elsewhere) ───
WebSocketsServer webSocket(WS_PORT);

// ── Singleton client tracking ──────────────────────────────────────────────
int8_t activeClientNum = -1; // -1 = no client connected

// ── Internal state ─────────────────────────────────────────────────────────
static bool _apModeActive = false;
static WiFiUDP _udp;
static unsigned long _lastBeacon = 0;
static unsigned long _lastRetry = 0;

// ── Known WiFi networks (edit agri3d_config.h to add/remove) ──────────────
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
 * @brief Attempt to connect to a single network.
 * @return true if connected within WIFI_CONNECT_TIMEOUT_MS.
 */
static bool tryConnect(const char *ssid, const char *pass) {
  Serial.printf("[NET] Trying: %s\n", ssid);
  WiFi.begin(ssid, pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= WIFI_CONNECT_TIMEOUT_MS) {
      WiFi.disconnect(true);
      return false;
    }
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return true;
}

/** Called once a station connection is confirmed. */
static void onStationConnected() {
  setWifi(WIFI_CONNECTED);
  Serial.printf("[NET] ✓ WiFi connected → %s\n",
                WiFi.localIP().toString().c_str());

  // Make it highly visible for the user
  Serial.println("\n========================================");
  Serial.print("WIFI CONNECTED! IP ADDRESS: ");
  Serial.println(WiFi.localIP());
  Serial.println("========================================\n");

  // Start mDNS so Flutter can find the device as farmbot.local
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("ws", "tcp", WS_PORT);
    Serial.printf("[NET] mDNS: ws://%s.local:%d\n", MDNS_HOSTNAME, WS_PORT);
  }

  // Start UDP for auto-discovery
  _udp.begin(UDP_DISCOVERY_PORT);

  // If we were in AP mode, bring it down cleanly
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

  // AP_MAX_CONN = 1 enforces the singleton at the network layer too
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, AP_MAX_CONN);
  Serial.printf("[NET] AP started: SSID=%s  IP=%s\n", AP_SSID,
                WiFi.softAPIP().toString().c_str());

  // UDP still works on the AP interface for local discovery
  _udp.begin(UDP_DISCOVERY_PORT);

  setWifi(WIFI_DISCONNECTED); // Station is not connected — state reflects that
}

void stopAPMode() {
  if (!_apModeActive)
    return;
  _apModeActive = false;
  WiFi.softAPdisconnect(true);
  Serial.println("[NET] AP stopped — switched to station mode");
}

bool isAPMode() { return _apModeActive; }

// ============================================================================
// BACKGROUND WIFI RETRY TASK (FreeRTOS)
// ============================================================================

/**
 * Runs on Core 0. While in AP mode, tries known networks every
 * WIFI_RETRY_INTERVAL_MS. When a station connection succeeds it calls
 * onStationConnected() and suspends itself (no more retries needed).
 */
static TaskHandle_t _retryTaskHandle = nullptr;

static void wifiRetryTask(void * /*pvParameters*/) {
  while (true) {
    // Sleep for the retry interval — this keeps the task cheap
    vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS));

    // Only retry if we're NOT already connected as a station
    if (WiFi.status() == WL_CONNECTED) {
      vTaskDelete(nullptr); // Success — no more retries needed
      return;
    }

    setWifi(WIFI_CONNECTING);
    Serial.println("[NET] Background WiFi retry...");

    for (int i = 0; i < WIFI_NET_COUNT; i++) {
      if (tryConnect(knownNetworks[i].ssid, knownNetworks[i].pass)) {
        onStationConnected();
        vTaskDelete(nullptr); // Connected — self-terminate
        return;
      }
    }

    // Still nothing — stay in AP mode
    setWifi(WIFI_DISCONNECTED);
    Serial.println("[NET] Retry failed — staying in AP mode");
  }
}

// ============================================================================
// WEBSOCKET EVENT — SINGLETON ENFORCEMENT
// ============================================================================

/**
 * Wraps the user-facing webSocketEvent() (defined in agri3d_commands.cpp).
 * Intercepts CONNECT events to enforce the singleton policy before passing
 * control to the command router.
 */
static void wsEventWrapper(uint8_t num, WStype_t type, uint8_t *payload,
                           size_t length) {
  if (type == WStype_CONNECTED) {
#if WS_SINGLETON
    // Get the client's IP address and query (sid)
    IPAddress remoteIP = webSocket.remoteIP(num);
    String currentIP = remoteIP.toString();
    String sid = "none";
    
    // The 'payload' for WStype_CONNECTED is the URL
    if (payload != NULL) {
      String url = String((char*)payload);
      int sidIdx = url.indexOf("sid=");
      if (sidIdx != -1) {
        sid = url.substring(sidIdx + 4);
        // Cut off at next param if any
        int endIdx = sid.indexOf('&');
        if (endIdx != -1) sid = sid.substring(0, endIdx);
      }
    }

    // GHOST PROTECTION: If we already have a session, and the new one has NO sid,
    // REJECT the new one immediately without touching the active session.
    if (activeClientNum != -1 && sid == "none") {
       Serial.printf("[NET] 🛡 Rejected Ghost (no sid) from %s to protect active client #%d\n", 
                     currentIP.c_str(), activeClientNum);
       webSocket.disconnect(num);
       return;
    }
    
    if (activeClientNum != -1 && activeClientNum != (int8_t)num) {
      IPAddress oldIP = webSocket.remoteIP(activeClientNum);
      
      // If same IP, it's a legitimate reconnection/refresh from the same device
      if (oldIP == remoteIP) {
        Serial.printf("[NET] ♻ Reconnection from %s (sid: %s). Swapping slots #%d -> #%d\n", 
                      currentIP.c_str(), sid.c_str(), activeClientNum, num);
      } else {
        Serial.printf("[NET] ⚔ Conflict! New client %s (sid: %s) kicking old client %s\n", 
                      currentIP.c_str(), sid.c_str(), oldIP.toString().c_str());
      }
      
      webSocket.disconnect(activeClientNum);
      activeClientNum = -1;
    }
    activeClientNum = (int8_t)num;
    Serial.printf("[NET] ✓ Flutter client #%d connected from %s (sid: %s)\n", num, currentIP.c_str(), sid.c_str());
#endif
    setFlutter(FLUTTER_CONNECTED);
    // Send the full system state snapshot immediately on connect
    broadcastSystemState();
  }

  else if (type == WStype_DISCONNECTED) {
    if ((int8_t)num == activeClientNum) {
      activeClientNum = -1;
      setFlutter(FLUTTER_DISCONNECTED);
      // Stop stream and cancel any scan if the client drops
      if (sysState.isStreaming)
        setStreaming(false);
      Serial.printf("[NET] Flutter client #%d disconnected\n", num);
    }
    return; // Don't forward disconnect to command router
  }

  // Forward TEXT / BIN / PING / PONG to the command router
  webSocketEvent(num, type, payload, length);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void networkInit() {
  setWifi(WIFI_CONNECTING);

  // Station mode — try every known network
  WiFi.mode(WIFI_STA);
  bool connected = false;

  for (int i = 0; i < WIFI_NET_COUNT && !connected; i++) {
    connected = tryConnect(knownNetworks[i].ssid, knownNetworks[i].pass);
  }

  if (connected) {
    onStationConnected();
  } else {
    Serial.println("[NET] All networks failed — starting AP fallback");
    WiFi.mode(WIFI_AP_STA); // AP + STA so background retry can still scan
    startAPMode();

    // Kick off background retry task on Core 0
    xTaskCreatePinnedToCore(wifiRetryTask, "wifiRetry", 4096, nullptr, 1,
                            &_retryTaskHandle, 0);
  }

  // Start WebSocket server with the singleton-enforcing wrapper
  webSocket.begin();
  webSocket.onEvent(wsEventWrapper);
  Serial.printf("[NET] WebSocket server started on port %d\n", WS_PORT);
}

void sendDiscoveryBeacon() {
  // Broadcast on the correct interface (station or AP)
  IPAddress broadcastIP;

  if (WiFi.status() == WL_CONNECTED) {
    broadcastIP = WiFi.localIP();
  } else if (_apModeActive) {
    broadcastIP = WiFi.softAPIP();
  } else {
    return; // No interface available
  }

  broadcastIP[3] = 255; // .255 = subnet broadcast

  _udp.beginPacket(broadcastIP, UDP_DISCOVERY_PORT);
  _udp.print("AGRI3D_DISCOVERY:");
  _udp.print(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                           : WiFi.softAPIP().toString());
  _udp.endPacket();
}

void networkLoop() {
  webSocket.loop();

  // ── Safe Frame Handoff: Process pending camera frames ──
  // We do this AFTER webSocket.loop() to ensure pings/pongs are handled first.
  if (sysState.pendingFrame != nullptr) {
    if (sysState.pendingFrameClient >= 0) {
      webSocket.sendBIN((uint8_t)sysState.pendingFrameClient, sysState.pendingFrame, sysState.pendingFrameLen);
    }
    free(sysState.pendingFrame);
    sysState.pendingFrame = nullptr;
    sysState.pendingFrameLen = 0;
    sysState.pendingFrameClient = -1;
  }

  // UDP discovery beacon
  if (millis() - _lastBeacon >= UDP_BROADCAST_INTERVAL) {
    _lastBeacon = millis();
    sendDiscoveryBeacon();
  }
}

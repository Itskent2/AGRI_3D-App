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
#include "../core/agri3d_logger.h"

// ── WebSocket server (singleton defined here, declared extern elsewhere) ───
WebSocketsServer webSocket(WS_PORT);

// ── Singleton client tracking ──────────────────────────────────────────────
int8_t activeClientNum = -1; // -1 = no client connected

// ── Internal state ─────────────────────────────────────────────────────────
static bool _apModeActive = false;
static WiFiUDP _udp;
static unsigned long _lastBeacon = 0;
static unsigned long _lastRetry = 0;

// ── IP Lock & Watchdog ──────────────────────────────────────────────────────
static IPAddress _lockedIP = IPAddress(0,0,0,0);
static unsigned long _lastCommMs = 0;
static const unsigned long COMM_TIMEOUT_MS = 10000; // 10s auto-release

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

/** Called once a station connection is confirmed. */
static void onStationConnected() {
  sysState.setWifi(WIFI_CONNECTED);
  AgriLog(TAG_NET, LEVEL_SUCCESS, "✓ WiFi connected → %s", WiFi.localIP().toString().c_str());

  // Make it highly visible for the user
  AgriLog(TAG_NET, LEVEL_INFO, "========================================");
  AgriLog(TAG_NET, LEVEL_SUCCESS, "WIFI CONNECTED! IP ADDRESS: %s", WiFi.localIP().toString().c_str());
  AgriLog(TAG_NET, LEVEL_INFO, "========================================\n");

  // Start mDNS so Flutter can find the device as farmbot.local
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("ws", "tcp", WS_PORT);
    AgriLog(TAG_NET, LEVEL_SUCCESS, "mDNS: ws://%s.local:%d", MDNS_HOSTNAME, WS_PORT);
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
  AgriLog(TAG_NET, LEVEL_INFO, "AP started: SSID=%s  IP=%s", AP_SSID,
                WiFi.softAPIP().toString().c_str());

  // UDP still works on the AP interface for local discovery
  _udp.begin(UDP_DISCOVERY_PORT);

  sysState.setWifi(WIFI_DISCONNECTED); // Station is not connected — state reflects that
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

    sysState.setWifi(WIFI_CONNECTING);
    AgriLog(TAG_NET, LEVEL_INFO, "Background WiFi retry...");

    for (int i = 0; i < WIFI_NET_COUNT; i++) {
      if (tryConnect(knownNetworks[i].ssid, knownNetworks[i].pass)) {
        onStationConnected();
        vTaskDelete(nullptr); // Connected — self-terminate
        return;
      }
    }

    // Still nothing — stay in AP mode
    sysState.setWifi(WIFI_DISCONNECTED);
    AgriLog(TAG_NET, LEVEL_WARN, "Retry failed — staying in AP mode");
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
    AgriLog(TAG_NET, LEVEL_INFO, "New Connection Request (slot #%d)", num);
  
    // ── Security Check ──
    bool authenticated = false;
    String uri = String((const char*)payload, length);
    AgriLog(TAG_NET, LEVEL_INFO, "Handshake URI: %s", uri.c_str());

    int keyIdx = uri.indexOf("key=");
    if (keyIdx != -1) {
      String providedKey = uri.substring(keyIdx + 4);
      int nextParam = providedKey.indexOf('&');
      if (nextParam != -1) providedKey = providedKey.substring(0, nextParam);
      
      providedKey.trim(); 
      
      if (providedKey == AGRI3D_SECURE_TOKEN) {
        authenticated = true;
      }
    }

    if (!authenticated) {
      AgriLog(TAG_NET, LEVEL_ERR, "🛡 Security Rejection: Invalid Handshake [Key: %s]", uri.c_str());
      webSocket.disconnect(num);
      return;
    }

#if WS_SINGLETON
    IPAddress remoteIP = webSocket.remoteIP(num);
    String currentIP = remoteIP.toString();

    // 1. IP TAKEOVER: If it's the same IP, always allow it and kick the old slot.
    if (_lockedIP != IPAddress(0,0,0,0) && _lockedIP == remoteIP) {
        if (activeClientNum != -1 && activeClientNum != (int8_t)num) {
            AgriLog(TAG_NET, LEVEL_INFO, "♻ IP Takeover: %s swapped slot #%d -> #%d", 
                          currentIP.c_str(), activeClientNum, num);
            webSocket.disconnect(activeClientNum);
        }
        _lockedIP = remoteIP;
        _lastCommMs = millis();
        activeClientNum = (int8_t)num;
    } 
    // 2. NEW LOCK: If no one is connected, lock to this IP.
    else if (activeClientNum == -1) {
        _lockedIP = remoteIP;
        _lastCommMs = millis();
        activeClientNum = (int8_t)num;
        AgriLog(TAG_NET, LEVEL_INFO, "🔒 IP Locked: %s (slot #%d)", currentIP.c_str(), num);
    }
    // 3. REJECTION: Someone else is using it.
    else {
        AgriLog(TAG_NET, LEVEL_ERR, "🛡 Rejection: %s tried to connect but %s is locked.", 
                      currentIP.c_str(), _lockedIP.toString().c_str());
        webSocket.disconnect(num);
        return;
    }
#endif
    sysState.setFlutter(FLUTTER_CONNECTED);
    sysState.broadcast();
  }

  else if (type == WStype_DISCONNECTED) {
    if ((int8_t)num == activeClientNum) {
      activeClientNum = -1;
      // We DON'T clear _lockedIP here immediately. We allow a 10s grace period
      // so if the app is reconnecting, it gets priority.
      sysState.setFlutter(FLUTTER_DISCONNECTED);
      if (sysState.isStreaming())
        sysState.setStreaming(false);
      AgriLog(TAG_NET, LEVEL_INFO, "Locked client #%d disconnected. Lock held for %s", num, _lockedIP.toString().c_str());
    }
    return;
  }

  // Forward TEXT / BIN / PING / PONG to the command router
  webSocketEvent(num, type, payload, length);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void networkInit() {
  sysState.setWifi(WIFI_CONNECTING);

  // Station mode — try every known network
  WiFi.mode(WIFI_STA);
  bool connected = false;

  for (int i = 0; i < WIFI_NET_COUNT && !connected; i++) {
    connected = tryConnect(knownNetworks[i].ssid, knownNetworks[i].pass);
  }

  if (connected) {
    onStationConnected();
  } else {
    AgriLog(TAG_NET, LEVEL_WARN, "All networks failed — starting AP fallback");
    WiFi.mode(WIFI_AP_STA); // AP + STA so background retry can still scan
    startAPMode();

    // Kick off background retry task on Core 0
    xTaskCreatePinnedToCore(wifiRetryTask, "wifiRetry", 4096, nullptr, 1,
                            &_retryTaskHandle, 0);
  }

  // Start WebSocket server with the singleton-enforcing wrapper
  webSocket.begin();
  webSocket.onEvent(wsEventWrapper);
  AgriLog(TAG_NET, LEVEL_SUCCESS, "WebSocket server started on port %d", WS_PORT);
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

  // ── Zero-Copy Frame Handoff ──
  if (sysState.pendingFrameFB != nullptr) {
    if (sysState.pendingFrameClient >= 0) {
      webSocket.sendBIN((uint8_t)sysState.pendingFrameClient, sysState.pendingFrame, sysState.pendingFrameLen);
    }
    // Return the buffer to the camera driver now that it's sent
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

  // ── 🔒 IP Lock Watchdog ──
  if (activeClientNum != -1) {
    _lastCommMs = millis(); // If someone is connected, they are active by definition of webSocket.loop()
  } else if (_lockedIP != IPAddress(0,0,0,0)) {
    // If no one is connected but we have a lock, check timeout
    if (millis() - _lastCommMs > COMM_TIMEOUT_MS) {
      AgriLog(TAG_NET, LEVEL_INFO, "🔓 Lock released: %s timed out.", _lockedIP.toString().c_str());
      _lockedIP = IPAddress(0,0,0,0);
    }
  }
}

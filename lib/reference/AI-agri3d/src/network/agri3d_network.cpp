#include "agri3d_network.h"
#include "../core/agri3d_logger.h"
#include "agri3d_commands.h"
#include "agri3d_config.h"
#include "agri3d_state.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ── WebSocket server ───────────────────────────────────────────────────────
WebSocketsServer webSocket(WS_PORT);

void broadcastLog(const char* log) {
    webSocket.broadcastTXT(log);
}

// ── Singleton client tracking ──────────────────────────────────────────────
int8_t activeClientNum = -1; // -1 = no client connected

// ── Internal state ─────────────────────────────────────────────────────────
static bool _apModeActive = false;
static WiFiUDP _udp;
static unsigned long _lastBeacon = 0;
static unsigned long _lastRetry = 0;

// ── IP Lock & Watchdog ─────────────────────────────────────────────────────
static IPAddress _lockedIP = IPAddress(0, 0, 0, 0);
static unsigned long _lastCommMs = 0;
static const unsigned long COMM_TIMEOUT_MS = 10000; // 10s auto-release

// ── Session State for Ghost Filtering ─────────────────────────────────────
static String _activeSid = "";
static int _highestGen = -1;

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
  if (type == WStype_CONNECTED) {
    AgriLog(TAG_NET, LEVEL_INFO, "New Connection Request (slot #%d)", num);

    String uri = String((const char *)payload, length);
    AgriLog(TAG_NET, LEVEL_INFO, "Handshake URI: %s", uri.c_str());

    String providedKey = getUriParam(uri, "key");
    String incomingSid = getUriParam(uri, "sid");
    int incomingGen = getUriParam(uri, "gen").toInt();

    // ── 1. Security Check ──
    if (providedKey != AGRI3D_SECURE_TOKEN) {
      AgriLog(TAG_NET, LEVEL_ERR,
              "🛡 Security Rejection: Invalid Handshake [Key: %s]",
              providedKey.c_str());
      webSocket.disconnect(num);
      return;
    }

#if WS_SINGLETON
    // ── 2. THE GHOST FILTER ──
    if (incomingSid == _activeSid && incomingGen <= _highestGen) {
      AgriLog(TAG_NET, LEVEL_ERR,
              "👻 Ghost connection rejected! Gen %d is older/equal to Gen %d",
              incomingGen, _highestGen);
      webSocket.disconnect(num);
      return;
    }

    IPAddress remoteIP = webSocket.remoteIP(num);
    String currentIP = remoteIP.toString();

    // ── 3. IP TAKEOVER & LOCK UPDATE ──
    if (_lockedIP != IPAddress(0, 0, 0, 0) && _lockedIP == remoteIP) {
      if (activeClientNum != -1 && activeClientNum != (int8_t)num) {
        AgriLog(TAG_NET, LEVEL_INFO,
                "♻ Connection replaced. Swapped slot #%d -> #%d (Gen %d)",
                activeClientNum, num, incomingGen);
        webSocket.disconnect(activeClientNum);
        sysState.setStreaming(false); // Stop stream on replace, wait for new client to start it
      }
    } else if (activeClientNum == -1) {
      AgriLog(TAG_NET, LEVEL_INFO, "🔒 IP Locked: %s (slot #%d, Gen %d)",
              currentIP.c_str(), num, incomingGen);
    } else {
      AgriLog(TAG_NET, LEVEL_ERR,
              "🛡 Rejection: %s tried to connect but %s is locked.",
              currentIP.c_str(), _lockedIP.toString().c_str());
      webSocket.disconnect(num);
      return;
    }

    _lockedIP = remoteIP;
    _activeSid = incomingSid;
    _highestGen = incomingGen;
    _lastCommMs = millis();
    activeClientNum = (int8_t)num;
#endif

    sysState.setFlutter(FLUTTER_CONNECTED);
    sysState.resetFlutterWatchdog();
    sysState.broadcast();

    // Auto-identify standard system state immediately on accept
    webSocket.sendTXT(num,
                      "{\"evt\":\"SYSTEM_STATE\", \"system\":\"AGRI_3D\"}");
  }

  else if (type == WStype_DISCONNECTED) {
    if ((int8_t)num == activeClientNum) {
      activeClientNum = -1;
      sysState.setFlutter(FLUTTER_DISCONNECTED);
      if (sysState.isStreaming())
        sysState.setStreaming(false);
      AgriLog(TAG_NET, LEVEL_INFO,
              "Locked client #%d disconnected. Lock held for %s", num,
              _lockedIP.toString().c_str());
    }
    return;
  }

  // ── THE DEADMAN'S SWITCH RESET ──
  else if (type == WStype_TEXT || type == WStype_BIN || type == WStype_PING) {
    if ((int8_t)num == activeClientNum) {
      _lastCommMs = millis();
      sysState.resetFlutterWatchdog();
    }
  }

  // Forward TEXT / BIN / PING / PONG to the command router
  webSocketEvent(num, type, payload, length);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void networkInit() {
  sysState.setWifi(WIFI_CONNECTING);

  WiFi.mode(WIFI_STA);
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

  webSocket.begin();
  webSocket.onEvent(wsEventWrapper);
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

void networkLoop() {
  webSocket.loop();

  // ── Zero-Copy Frame Handoff ──
  if (sysState.pendingFrameFB != nullptr) {
    if (sysState.pendingFrameClient >= 0) {
      webSocket.sendBIN((uint8_t)sysState.pendingFrameClient,
                        sysState.pendingFrame, sysState.pendingFrameLen);
      
      // Send AI results for this frame if available
      if (sysState.pendingAiResult != nullptr) {
        webSocket.sendTXT((uint8_t)sysState.pendingFrameClient, *sysState.pendingAiResult);
        delete sysState.pendingAiResult;
        sysState.pendingAiResult = nullptr;
      }
    }
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

  // ── 🔒 True Bidirectional Watchdog (Deadman's Switch) ──
  if (activeClientNum != -1) {
    if (millis() - _lastCommMs > COMM_TIMEOUT_MS) {
      AgriLog(TAG_NET, LEVEL_WARN,
              "⚠ Client Watchdog Timeout! No data in 10s.");
      webSocket.disconnect(activeClientNum);

      // EMERGENCY STOP: Stop streaming and halt machine if needed here
      if (sysState.isStreaming()) {
        sysState.setStreaming(false);
      }
    }
  } else if (_lockedIP != IPAddress(0, 0, 0, 0)) {
    if (millis() - _lastCommMs > COMM_TIMEOUT_MS) {
      AgriLog(TAG_NET, LEVEL_INFO, "🔓 Lock released: %s timed out.",
              _lockedIP.toString().c_str());
      _lockedIP = IPAddress(0, 0, 0, 0);
    }
  }
}

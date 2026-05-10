/**
 * @file agri3d_network.h
 * @brief WiFi management, AP fallback, mDNS, UDP discovery, and WebSocket server.
 *
 * Connection policy:
 *  1. At boot, try each network in the knownNetworks[] list in order.
 *  2. If all fail → start AP hotspot so the user can still connect directly.
 *  3. A background FreeRTOS task keeps retrying known networks every
 *     WIFI_RETRY_INTERVAL_MS while the ESP32 is in AP mode.
 *  4. When a station connection succeeds, the AP is taken down cleanly.
 *
 * Singleton policy (WS_SINGLETON = true):
 *  Only one WebSocket client is allowed at a time. A second connection
 *  attempt receives an error message and is immediately closed.
 */

#pragma once
#include <Arduino.h>
#include <WebSocketsServer.h>

extern WebSocketsServer webSocket;

/** The currently-connected singleton client number (-1 = none). */
extern int8_t activeClientNum;
extern TaskHandle_t networkTaskHandle;

/**
 * @brief Try to connect to each known WiFi network in order.
 *        Falls back to AP mode if all fail.
 *        Called once from setup().
 */
void networkInit();

/** Broadcast the UDP discovery beacon (called internally by networkLoop). */
void sendDiscoveryBeacon();

/** Start AP hotspot fallback. */
void startAPMode();

/** Stop AP mode (called when a station connection succeeds in background). */
void stopAPMode();

/** @return true while the ESP32 is running as an AP (hotspot). */
bool isAPMode();

/**
 * @brief Drain log messages queued by Core-1 tasks and broadcast them.
 *        MUST be called only from Core 0 (the network loop task).
 */
void drainLogQueue();

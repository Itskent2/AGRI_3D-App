    /**
    * @file agri3d_plant_map.cpp
    * @brief SCAN_PLANT command handler — SD-first, heartbeat-safe two-phase design.
    *
    * Phase 1  (executeScanPlant)   — gantry snake-grid, capture + save to SD card.
    *   No binary WebSocket traffic during scan, so Core 0 is free for PING/PONG.
    *   Only small JSON SCAN_PROGRESS events are sent, ~50 bytes each.
    *   On completion: broadcasts SCAN_COMPLETE {total, ready:true}.
    *
    * Phase 2  (executeScanUpload)  — triggered by Flutter command UPLOAD_SCAN.
    *   Reads every JPEG from the SD index, sends FRAME_META + binary one at a time.
    *   Resets Flutter watchdog between every frame.
    *   On completion: broadcasts UPLOAD_SCAN_COMPLETE.
    */

    #include "agri3d_plant_map.h"
    #include "agri3d_config.h"
    #include "agri3d_state.h"
    #include "agri3d_grbl.h"
    #include "agri3d_camera.h"
    #include "agri3d_network.h"
    #include "agri3d_sd.h"
    #include "../core/agri3d_logger.h"
    #include <ArduinoJson.h>
    #include <SD_MMC.h>

    // ============================================================================
    // SD INDEX FILE
    // ============================================================================
    // After each scan session a JSON-lines index file is written to SD:
    //   /plantmap/index.json
    // Each line is a compact JSON object:
    //   {"idx":1,"x":0.0,"y":0.0,"path":"/plantmap/20250509/f_001_x0_y0_1234.jpg"}
    //
    // This file survives a reboot so a failed upload can be retried.

    static const char* SCAN_INDEX_PATH = "/plantmap/index.json";

    // ============================================================================
    // INTERNAL: PRE-SCAN HOMING
    // ============================================================================

    /**
    * @brief Home all axes before scan.
    * @return true if homing completed successfully.
    */
    static bool homeScanAxes() {
        AgriLog(TAG_ROUTINE, LEVEL_INFO, "Homing All Axes...");
        enqueueGrblCommand("$H");
        if (!waitForGrblIdle(SCAN_HOME_TIMEOUT_MS * 3)) {
            AgriLog(TAG_ROUTINE, LEVEL_ERR, "$H timeout");
            return false;
        }
        delay(500);

        // Refresh dimension cache from Nano EEPROM after homing
        requestMachineDimensions();
        delay(1000);

        AgriLog(TAG_ROUTINE, LEVEL_SUCCESS, "Homed. Workspace: X=%.1f Y=%.1f",
                    machineDim.maxX, machineDim.maxY);
        return true;
    }

    // ============================================================================
    // INTERNAL: BROADCAST HELPERS
    // ============================================================================

    static void broadcastScanStart(uint8_t clientNum, int total,
                                    int cols, int rows,
                                    float stepX, float stepY, float zHeight) {
        StaticJsonDocument<192> doc;
        doc["evt"]     = "SCAN_START";
        doc["total"]   = total;
        doc["cols"]    = cols;
        doc["rows"]    = rows;
        doc["stepX"]   = stepX;
        doc["stepY"]   = stepY;
        doc["zHeight"] = zHeight;
        doc["maxX"]    = machineDim.maxX;
        doc["maxY"]    = machineDim.maxY;
        String out; serializeJson(doc, out);
        webSocket.sendTXT(clientNum, out);
    }

    static void broadcastScanProgress(uint8_t clientNum, int idx, int total,
                                       float x, float y) {
        StaticJsonDocument<128> doc;
        doc["evt"]   = "SCAN_PROGRESS";
        doc["idx"]   = idx;
        doc["total"] = total;
        doc["x"]     = x;
        doc["y"]     = y;
        doc["pct"]   = (int)((idx * 100) / total);
        String out; serializeJson(doc, out);
        webSocket.sendTXT(clientNum, out);
    }

    static void broadcastScanComplete(uint8_t clientNum, int total, bool aborted) {
        StaticJsonDocument<128> doc;
        doc["evt"]     = "SCAN_COMPLETE";
        doc["total"]   = total;
        doc["aborted"] = aborted;
        doc["ready"]   = !aborted; // true = SD index is available for upload
        String out; serializeJson(doc, out);
        webSocket.sendTXT(clientNum, out);
    }

    static void broadcastScanError(uint8_t clientNum, const char* reason) {
        StaticJsonDocument<96> doc;
        doc["evt"]    = "SCAN_ERROR";
        doc["reason"] = reason;
        String out; serializeJson(doc, out);
        webSocket.sendTXT(clientNum, out);
        AgriLog(TAG_SCAN, LEVEL_ERR, "%s", reason);
    }

    // ============================================================================
    // PUBLIC: SCAN_PLANT HANDLER (command entry point — runs on Core 1 via queue)
    // ============================================================================

    void checkExistingScan() {
    File indexFile = SD_MMC.open(SCAN_INDEX_PATH, FILE_READ);
    if (!indexFile) return;

    int total = 0;
    while (indexFile.available()) {
        String line = indexFile.readStringUntil('\n');
        line.trim();
        if (line.length() > 2) total++;
    }
    indexFile.close();

    if (total > 0) {
        AgriLog(TAG_SCAN, LEVEL_INFO, "Found existing scan index on SD with %d frames.", total);
        sysState.setScanReadyForUpload(true);
    }
}

void handleScanPlant(uint8_t clientNum, const String& cmdBody) {
        // Format: "cols:rows:stepX:stepY:zHeight"
        int c1 = cmdBody.indexOf(':');
        int c2 = cmdBody.indexOf(':', c1 + 1);
        int c3 = cmdBody.indexOf(':', c2 + 1);
        int c4 = cmdBody.indexOf(':', c3 + 1);

        if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) {
            broadcastScanError(clientNum, "Bad format. Expected cols:rows:stepX:stepY:zHeight");
            return;
        }

        int   cols    = cmdBody.substring(0, c1).toInt();
        int   rows    = cmdBody.substring(c1+1, c2).toInt();
        float stepX   = cmdBody.substring(c2+1, c3).toFloat();
        float stepY   = cmdBody.substring(c3+1, c4).toFloat();
        float zHeight = cmdBody.substring(c4+1).toFloat();

        if (cols <= 0 || rows <= 0 || stepX <= 0 || stepY <= 0) {
            broadcastScanError(clientNum, "Invalid scan parameters (must be > 0)");
            return;
        }

        globalScanParams.clientNum = clientNum;
        globalScanParams.cols      = cols;
        globalScanParams.rows      = rows;
        globalScanParams.stepX     = stepX;
        globalScanParams.stepY     = stepY;
        globalScanParams.zHeight   = zHeight;

        AgriLog(TAG_SCAN, LEVEL_INFO, "Queuing %dx%d scan (%d frames) → Core 1",
                    cols, rows, cols * rows);

        startRoutine(3); // ROUTINE_SCAN_PLANT
    }

    // ============================================================================
    // PHASE 1: executeScanPlant — movement + SD save (NO WebSocket binary)
    // ============================================================================

    void executeScanPlant(const ScanParams& cfg) {
        int total = cfg.cols * cfg.rows;
        uint8_t clientNum = cfg.clientNum;

        // ── Guard ─────────────────────────────────────────────────────────────
        if (sysState.getOperation() == OP_SCANNING ||
            sysState.getOperation() == OP_UPLOADING ||
            sysState.getOperation() == OP_ALARM_RECOVERY) {
            broadcastScanError(clientNum, "Cannot scan: system busy or in alarm");
            return;
        }

#if !HW_SD_CONNECTED
        broadcastScanError(clientNum, "SD card required for scan — not connected");
        return;
#endif

        // ── Pause live stream ─────────────────────────────────────────────────
        bool streamWasActive = sysState.isStreaming();
        sysState.setStreaming(false);
        sysState.setOperation(OP_HOMING);

        // ── Pre-scan homing ───────────────────────────────────────────────────
        if (!homeScanAxes()) {
            broadcastScanError(clientNum, "Homing failed — scan aborted");
            if (streamWasActive) sysState.setStreaming(true);
            sysState.setOperation(OP_IDLE);
            return;
        }

        sysState.setOperation(OP_SCANNING);
        broadcastScanStart(clientNum, total, cfg.cols, cfg.rows,
                           cfg.stepX, cfg.stepY, cfg.zHeight);

        // ── Prepare SD index file (overwrite any previous scan) ───────────────
        sdEnsureDir("/plantmap");
        File indexFile = SD_MMC.open(SCAN_INDEX_PATH, FILE_WRITE);
        if (!indexFile) {
            broadcastScanError(clientNum, "Failed to open SD index — check card");
            if (streamWasActive) sysState.setStreaming(true);
            sysState.setOperation(OP_IDLE);
            return;
        }

        // ── Move Z to requested height ────────────────────────────────────────
        char zMove[32];
        snprintf(zMove, sizeof(zMove), "G0 Z%.1f F500", cfg.zHeight);
        enqueueGrblCommand(zMove);
        waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);

        // ── Snake-pattern grid traversal ──────────────────────────────────────
        int frameIdx = 0;
        bool aborted = false;
        time_t sessionTs = time(nullptr); // Single timestamp for the whole session folder

        for (int row = 0; row < cfg.rows && !aborted; row++) {
            for (int colStep = 0; colStep < cfg.cols && !aborted; colStep++) {

                int col = (row % 2 == 0) ? colStep : (cfg.cols - 1 - colStep);

                // Apply camera offset: the camera is physically offset from
                // the gantry centre along the Y axis. Move the gantry so the
                // *camera lens* is over the grid cell, not the gantry head.
                float camOffset = sysState.getCamOffset();
                float targetX = col * cfg.stepX;          // X is not offset
                float targetY = row * cfg.stepY + camOffset;

                // Safety: clamp to measured workspace
                if (machineDim.valid) {
                    if (targetX > machineDim.maxX) targetX = machineDim.maxX;
                    if (targetY > machineDim.maxY) targetY = machineDim.maxY;
                }

                frameIdx++;

                // ── Reset watchdog: Core 0 is free, this just timestamps ────
                sysState.resetFlutterWatchdog();

                // ── Move to position ──────────────────────────────────────────
                char gcode[48];
                snprintf(gcode, sizeof(gcode), "G0 X%.2f Y%.2f F%d",
                         targetX, targetY, GRBL_DEFAULT_FEEDRATE);
                enqueueGrblCommand(gcode);
                if (!waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS)) {
                    AgriLog(TAG_ROUTINE, LEVEL_WARN, "Move timeout at frame %d — skipping", frameIdx);
                    continue;
                }

                // ── Capture frame ─────────────────────────────────────────────
                // Allow vibrations to dampen after gantry stop.
                vTaskDelay(pdMS_TO_TICKS(200));

                // Drain ALL stale frames from the camera DMA ring buffer.
                // The OV2640 captures continuously; at 15fps, a 5-second move
                // produces ~75 frames but the ring buffer (fb_count=2) only
                // holds the 2 most recent. Both slots are stale (shot while
                // moving). Drain them so the next get() returns a fresh frame.
                for (int _drain = 0; _drain < 2; _drain++) {
                    camera_fb_t* stale = esp_camera_fb_get();
                    if (stale) esp_camera_fb_return(stale);
                }
                vTaskDelay(pdMS_TO_TICKS(80)); // let sensor expose one clean frame

                camera_fb_t* fb = esp_camera_fb_get();
                if (!fb) {
                    AgriLog(TAG_CAM, LEVEL_WARN, "Camera FB fail at frame %d — skipping", frameIdx);
                    continue;
                }

                // ── Save JPEG to SD ───────────────────────────────────────────
                char sdPath[96] = {0};
                bool saved = sdSaveImage(fb->buf, fb->len,
                                         SD_IMG_PLANTMAP, 'f',
                                         frameIdx, targetX, targetY,
                                         sessionTs, sdPath);
                esp_camera_fb_return(fb);

                if (!saved) {
                    AgriLog(TAG_SCAN, LEVEL_WARN, "SD save failed at frame %d", frameIdx);
                    // Non-fatal: skip, continue scanning
                    continue;
                }

                // ── Append to index file ──────────────────────────────────────
                // One compact JSON object per line (JSON-lines format)
                StaticJsonDocument<192> entry;
                entry["idx"]  = frameIdx;
                entry["x"]    = targetX;
                entry["y"]    = targetY;
                entry["path"] = sdPath;
                String entryStr;
                serializeJson(entry, entryStr);
                indexFile.println(entryStr);

                // ── Send lightweight progress event (JSON only, no binary) ────
                broadcastScanProgress(clientNum, frameIdx, total, targetX, targetY);

                AgriLog(TAG_SCAN, LEVEL_INFO, "Frame %d/%d → SD: %s", frameIdx, total, sdPath);
            }
        }

        indexFile.close();

        // ── Return to origin ──────────────────────────────────────────────────
        AgriLog(TAG_ROUTINE, LEVEL_INFO, "Returning to origin (0, 0)");
        enqueueGrblCommand("G0 X0 Y0 F1000");
        waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);

        // ── Restore state ─────────────────────────────────────────────────────
        sysState.setOperation(OP_IDLE);
        sysState.setScanReadyForUpload(true); // Flag that SD has an index ready for upload
        if (streamWasActive) sysState.setStreaming(true);
        broadcastScanComplete(clientNum, frameIdx, aborted);

        AgriLog(TAG_ROUTINE, LEVEL_SUCCESS,
                "Scan done. %d/%d frames saved to SD. Send UPLOAD_SCAN to upload.",
                frameIdx, total);
    }

    // ============================================================================
    // PHASE 2: handleScanUpload / executeScanUpload — SD → Flutter batch upload
    // ============================================================================

    void handleScanUpload(uint8_t clientNum) {
        if (sysState.getOperation() == OP_UPLOADING ||
            sysState.getOperation() == OP_SCANNING) {
            StaticJsonDocument<96> doc;
            doc["evt"]    = "UPLOAD_SCAN_ERROR";
            doc["reason"] = "System busy";
            String out; serializeJson(doc, out);
            webSocket.sendTXT(clientNum, out);
            return;
        }

        globalScanParams.clientNum = clientNum;
        startRoutine(5); // ROUTINE_SCAN_UPLOAD
    }

    void executeScanUpload(uint8_t clientNum) {
        // ── Open index file ───────────────────────────────────────────────────
        File indexFile = SD_MMC.open(SCAN_INDEX_PATH, FILE_READ);
        if (!indexFile) {
            StaticJsonDocument<128> doc;
            doc["evt"]    = "UPLOAD_SCAN_ERROR";
            doc["reason"] = "No scan index found — run Scan Bed first";
            String out; serializeJson(doc, out);
            webSocket.sendTXT(clientNum, out);
            AgriLog(TAG_SCAN, LEVEL_ERR, "UPLOAD_SCAN: index not found");
            return;
        }

        // ── Count total lines for progress reporting ──────────────────────────
        int total = 0;
        while (indexFile.available()) {
            String line = indexFile.readStringUntil('\n');
            line.trim();
            if (line.length() > 2) total++;
        }
        indexFile.seek(0);

        if (total == 0) {
            indexFile.close();
            StaticJsonDocument<96> doc;
            doc["evt"]    = "UPLOAD_SCAN_ERROR";
            doc["reason"] = "Scan index is empty";
            String out; serializeJson(doc, out);
            webSocket.sendTXT(clientNum, out);
            return;
        }

        sysState.setOperation(OP_UPLOADING);
        // NOTE: do NOT clear scan_ready here. It stays true so that if the
        // connection drops mid-upload, Flutter can see the Upload button again
        // and resume from the beginning on reconnect.

        // ── Announce upload start ─────────────────────────────────────────────
        {
            StaticJsonDocument<96> doc;
            doc["evt"]   = "UPLOAD_SCAN_START";
            doc["total"] = total;
            String out; serializeJson(doc, out);
            webSocket.sendTXT(clientNum, out);
        }

        AgriLog(TAG_SCAN, LEVEL_INFO, "Upload started: %d frames from SD", total);

        int sent = 0;
        bool aborted = false;

        // ── Read index line by line and stream each JPEG ──────────────────────
        while (indexFile.available() && !aborted) {
            String line = indexFile.readStringUntil('\n');
            line.trim();
            if (line.length() < 3) continue; // Skip blank lines

            // ── Abort if Flutter disconnected mid-upload ──────────────────────
            if (sysState.getFlutter() == FLUTTER_DISCONNECTED) {
                AgriLog(TAG_SCAN, LEVEL_WARN, "Flutter disconnected — aborting upload");
                aborted = true;
                break;
            }

            // ── Parse index entry ─────────────────────────────────────────────
            StaticJsonDocument<192> entry;
            if (deserializeJson(entry, line) != DeserializationError::Ok) {
                AgriLog(TAG_SCAN, LEVEL_WARN, "Skipping malformed index entry: %s",
                            line.c_str());
                continue;
            }

            int   idx  = entry["idx"]  | 0;
            float x    = entry["x"]    | 0.0f;
            float y    = entry["y"]    | 0.0f;
            const char* path = entry["path"] | "";

            // ── Open JPEG from SD ─────────────────────────────────────────────
            File imgFile = SD_MMC.open(path, FILE_READ);
            if (!imgFile) {
                AgriLog(TAG_SCAN, LEVEL_WARN, "Frame %d: cannot open %s — skipping", idx, path);
                continue;
            }

            size_t fileSize = imgFile.size();
            if (fileSize == 0) {
                imgFile.close();
                AgriLog(TAG_SCAN, LEVEL_WARN, "Frame %d: zero-size file — skipping", idx);
                continue;
            }

            // ── Allocate buffer and read JPEG ─────────────────────────────────
            uint8_t* buf = (uint8_t*)malloc(fileSize);
            if (!buf) {
                imgFile.close();
                AgriLog(TAG_SCAN, LEVEL_ERR, "Frame %d: malloc(%u) failed", idx, fileSize);
                continue;
            }
            imgFile.read(buf, fileSize);
            imgFile.close();

            // ── Send FRAME_META (JSON, small) ─────────────────────────────────
            StaticJsonDocument<192> meta;
            meta["evt"]   = "FRAME_META";
            meta["idx"]   = idx;
            meta["total"] = total;
            meta["x"]     = x;
            meta["y"]     = y;
            String metaStr; serializeJson(meta, metaStr);
            webSocket.sendTXT(clientNum, metaStr);

            // ── Send binary JPEG ──────────────────────────────────────────────
            webSocket.sendBIN(clientNum, buf, fileSize);
            free(buf);

            sent++;

            // ── Reset watchdog AFTER send so heartbeat stays alive ────────────
            sysState.resetFlutterWatchdog();

            // ── Flow-control yield: give Core-0 time to TX the binary frame ───
            // 50ms base + 1ms per 4KB keeps large frames from overlapping.
            uint32_t yieldMs = 50 + (fileSize / 4096);
            vTaskDelay(pdMS_TO_TICKS(yieldMs));

            AgriLog(TAG_SCAN, LEVEL_INFO, "Uploaded frame %d/%d (%.1f,%.1f) %u B",
                        idx, total, x, y, (unsigned)fileSize);
        }

        indexFile.close();

        // ── Restore state ─────────────────────────────────────────────────────
        sysState.setOperation(OP_IDLE);

        if (!aborted) {
            // Only mark scan data as consumed when ALL frames were delivered.
            sysState.setScanReadyForUpload(false);
        }
        // If aborted (disconnect), scan_ready stays true so Flutter can retry.

        StaticJsonDocument<96> done;
        done["evt"]     = "UPLOAD_SCAN_COMPLETE";
        done["sent"]    = sent;
        done["total"]   = total;
        done["aborted"] = aborted;
        String doneOut; serializeJson(done, doneOut);
        webSocket.sendTXT(clientNum, doneOut);

        AgriLog(TAG_SCAN, LEVEL_SUCCESS,
                "Upload %s: %d/%d frames sent.",
                aborted ? "INTERRUPTED (scan_ready kept — can retry)" : "complete",
                sent, total);
    }

    // =============================================================================
    // TODO(Luna): AI Weeding Capture Implementation
    // =============================================================================
    //
    // void aiCaptureCoordsForWeeding(uint8_t clientNum,
    //                                 float* xList, float* yList, int count) {
    //     setStreaming(false);
    //     setOperation(OP_AI_WEEDING);
    //     broadcastSystemState();
    //
    //     for (int i = 0; i < count; i++) {
    //         captureFrameAtPosition(clientNum, i + 1, count, xList[i], yList[i]);
    //         // TODO(Luna): After each capture, pass frame to aiProcessFrame()
    //         // TODO(Luna): Decide whether to actuate weeder at this coordinate
    //     }
    //
    //     NanoSerial.printf("G0 X0 Y0 F%d\n", GRBL_DEFAULT_FEEDRATE);
    //     waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS);
    //     setOperation(OP_IDLE);
    //     setStreaming(true);
    // }
    //
    // =============================================================================
    // TODO(Luna): AI Frame Processor
    // =============================================================================
    //
    // Called from captureFrameAtPosition() when AI is enabled.
    // Input:  raw JPEG buffer + real-world XY coordinate of the capture
    // Output: list of weed locations (relative pixel coords or mm offsets)
    //
    // void aiProcessFrame(const uint8_t* jpegBuf, size_t jpegLen,
    //                     float captureX, float captureY) {
    //     // TODO(Luna): Load TFLite model if not already loaded
    //     // TODO(Luna): Decode JPEG → RGB tensor
    //     // TODO(Luna): Run inference
    //     // TODO(Luna): For each detected weed bounding box:
    //     //               Convert pixel offset to mm offset using zHeight + FOV calibration
    //     //               Add to weed coordinate list
    //     //               Optionally broadcast {"evt":"WEED_DETECTED","x":...,"y":...}
    // }
    // =============================================================================

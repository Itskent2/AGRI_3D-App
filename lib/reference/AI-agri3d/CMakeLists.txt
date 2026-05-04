cmake_minimum_required(VERSION 3.16)

# ── ESP-IDF / PlatformIO Component Registration ─────────────────────────────
idf_component_register(
    SRCS
        "src/agri3d_state.cpp"
        "src/agri3d_network.cpp"
        "src/agri3d_grbl.cpp"
        "src/agri3d_camera.cpp"
        "src/agri3d_plant_map.cpp"
        "src/agri3d_npk.cpp"
        "src/agri3d_routine.cpp"
        "src/agri3d_environment.cpp"
        "src/agri3d_sd.cpp"
        "src/agri3d_commands.cpp"
    INCLUDE_DIRS
        "src"
    REQUIRES
        esp32-camera
        arduino-esp32
        ArduinoJson
        WebSockets
        SD_MMC
        mdns
        esp_wifi
        esp_http_client
)

file(GLOB_RECURSE component_sources
    "AI_AGRI3D.cpp"
    "edge-impulse-sdk/classifier/*.cpp"
    "edge-impulse-sdk/dsp/*.cpp"
    "edge-impulse-sdk/porting/esp32/*.cpp"
    "edge-impulse-sdk/tensorflow/*.cpp"
    "tflite-model/*.cpp"
)

idf_component_register(SRCS ${component_sources}
                    INCLUDE_DIRS "."
                                 "edge-impulse-sdk"
                                 "model-parameters"
                                 "tflite-model"
                    REQUIRES arduino)

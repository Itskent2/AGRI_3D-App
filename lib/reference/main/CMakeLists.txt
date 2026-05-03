file(GLOB_RECURSE app_sources
    "../esp32.ino"
)

idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS ".."
                    REQUIRES arduino AI_AGRI3D)

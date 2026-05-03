cmake_minimum_required(VERSION 3.16)

# Include custom components like AI_AGRI3D
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(agri3d_esp32)

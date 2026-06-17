cmake_minimum_required(VERSION 3.22)

# PiLab Panel flash-size variant selection.
# Default remains 16MB to preserve the original v6.45.41 build behavior.
# Build 8MB variant with:
#   idf.py -B build_8mb -DPILAB_FLASH_SIZE=8MB build
# Build 16MB variant with:
#   idf.py -B build_16mb -DPILAB_FLASH_SIZE=16MB build
set(PILAB_FLASH_SIZE "16MB" CACHE STRING "PiLab Panel flash variant: 8MB or 16MB")
set_property(CACHE PILAB_FLASH_SIZE PROPERTY STRINGS "8MB" "16MB")

if(PILAB_FLASH_SIZE STREQUAL "8MB")
    set(SDKCONFIG_DEFAULTS
        "sdkconfig.defaults.common"
        "sdkconfig.defaults.8mb"
    )
elseif(PILAB_FLASH_SIZE STREQUAL "16MB")
    set(SDKCONFIG_DEFAULTS
        "sdkconfig.defaults.common"
        "sdkconfig.defaults.16mb"
    )
else()
    message(FATAL_ERROR "Unsupported PILAB_FLASH_SIZE='${PILAB_FLASH_SIZE}'. Use 8MB or 16MB.")
endif()

message(STATUS "PiLab Panel flash variant: ${PILAB_FLASH_SIZE}")
message(STATUS "PiLab Panel sdkconfig defaults: ${SDKCONFIG_DEFAULTS}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(PiLab_Panel)

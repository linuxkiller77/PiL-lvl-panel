# PiLab Panel board selection.
#
# The shared HMI runtime is intended to stay common across all targets. Board
# backends own LCD/touch/backlight/display-buffer details. For a future board,
# add its source files below and build with, for example:
#   idf.py -D PILAB_BOARD=esp32p4_your_lcd build
set(PILAB_BOARD "waveshare_s3_7inch" CACHE STRING "PiLab Panel board backend")

set(PILAB_BOARD_SRCS
    "board/pilab_board.c"
)
set(PILAB_BOARD_INCLUDE_DIRS
    "board"
)
set(PILAB_BOARD_DEFS "")

if(PILAB_BOARD STREQUAL "waveshare_s3_7inch")
    list(APPEND PILAB_BOARD_SRCS
        "board/waveshare_s3_7inch/ch422g.c"
        "board/waveshare_s3_7inch/waveshare_s3_7inch_state.c"
        "board/waveshare_s3_7inch/waveshare_s3_7inch_backlight.c"
        "board/waveshare_s3_7inch/waveshare_s3_7inch_lcd.c"
        "board/waveshare_s3_7inch/waveshare_s3_7inch_touch_gt911.c"
        "board/waveshare_s3_7inch/waveshare_s3_7inch_board.c"
    )
    list(APPEND PILAB_BOARD_INCLUDE_DIRS
        "board/waveshare_s3_7inch"
    )
    list(APPEND PILAB_BOARD_DEFS
        "PILAB_BOARD_WAVESHARE_S3_7INCH=1"
    )
else()
    message(FATAL_ERROR "Unsupported PILAB_BOARD='${PILAB_BOARD}'. Add a backend in main/CMakeLists.txt and main/board/.")
endif()

idf_component_register(
    SRCS
        "app_main.c"
        "app/app_state.c"
        "app/app_tasks.c"
        ${PILAB_BOARD_SRCS}
        "hmi/hmi_layout_store.c"
        "hmi/hmi_lvgl_runtime.c"
        "hmi/hmi_memory_diag.c"
        "hmi/hmi_tag_db.c"
        "hmi/hmi_script_engine.cpp"
        "panel/panel_storage.c"
        "panel/panel_wifi.c"
        "mqtt/panel_mqtt.c"
        "web/panel_web_assets.c"
        "web/panel_web_server.c"
    INCLUDE_DIRS
        "."
        "app"
        ${PILAB_BOARD_INCLUDE_DIRS}
        "hmi"
        "panel"
        "mqtt"
        "web"
    REQUIRES
        esp_http_server
        esp_wifi
        esp_netif
        esp_event
        mdns
        mqtt
        nvs_flash
        spiffs
        esp_timer
        angelscript
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${PILAB_BOARD_DEFS})
message(STATUS "PiLab Panel board backend: ${PILAB_BOARD}")

# Use CMAKE_CURRENT_LIST_DIR instead of CMAKE_CURRENT_SOURCE_DIR because
# ESP-IDF also includes component CMakeLists files from a script-mode
# requirements pass, where CMAKE_CURRENT_SOURCE_DIR can point at build/.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PILAB_MAIN_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PILAB_WEB_ASSET_DIR "${PILAB_MAIN_DIR}/web/assets")
set(PILAB_WEB_GZIP_SCRIPT "${PILAB_MAIN_DIR}/tools/gzip_web_assets.py")
set(PILAB_WEB_GZIP_INPUTS
    "${PILAB_WEB_ASSET_DIR}/app.js"
    "${PILAB_WEB_ASSET_DIR}/index.css"
)
set(PILAB_WEB_GZIP_OUTPUTS
    "${PILAB_WEB_ASSET_DIR}/app.js.gz"
    "${PILAB_WEB_ASSET_DIR}/index.css.gz"
)

# EMBED_FILES needs the compressed files to exist during configure / ESP-IDF's
# component requirement scan. Generate them here with execute_process, which is
# safe in script mode.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${PILAB_WEB_GZIP_SCRIPT}"
            ${PILAB_WEB_GZIP_INPUTS}
    WORKING_DIRECTORY "${PILAB_MAIN_DIR}"
    RESULT_VARIABLE PILAB_WEB_GZIP_RESULT
)
if(NOT PILAB_WEB_GZIP_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to gzip PiLab web assets")
endif()

# During the normal CMake configure/generate pass, also add a build target so
# changing app.js or index.css regenerates the .gz files before embedding. This
# block must be skipped during ESP-IDF's script-mode requirements pass because
# add_custom_command/add_custom_target are not scriptable.
if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_custom_command(
        OUTPUT ${PILAB_WEB_GZIP_OUTPUTS}
        COMMAND "${Python3_EXECUTABLE}" "${PILAB_WEB_GZIP_SCRIPT}"
                ${PILAB_WEB_GZIP_INPUTS}
        DEPENDS
            ${PILAB_WEB_GZIP_INPUTS}
            "${PILAB_WEB_GZIP_SCRIPT}"
        WORKING_DIRECTORY "${PILAB_MAIN_DIR}"
        COMMENT "Compressing PiLab web assets for embedded gzip serving"
        VERBATIM
    )

    add_custom_target(pilab_web_gzip_assets ALL
        DEPENDS ${PILAB_WEB_GZIP_OUTPUTS}
    )
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "perf_test.cpp"
        "plc_io.cpp"
        "ethernet_web.c"
        "udp_server.c"
        "script_engine.cpp"
        "pilab_script_builder.cpp"
        "plc_tags.cpp"
        "plc_filesystem.cpp"
    INCLUDE_DIRS "."
    EMBED_FILES
        "web/index.html"
        "web/assets/index.css.gz"
        "web/assets/app.js.gz"
    REQUIRES
        esp_driver_gptimer
        esp_driver_gpio
        esp_timer
        esp_eth
        esp_netif
        esp_event
        esp_http_server
        ip101
        nvs_flash
        littlefs
        angelscript
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_dependencies(${COMPONENT_LIB} pilab_web_gzip_assets)
endif()

if(NOT CMAKE_SCRIPT_MODE_FILE)
    # Enable temporary PiLab ScriptBuilder metadata logging while the feature is being brought up.
    # Set to 0 later to compile the debug output out.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PILAB_SCRIPT_BUILDER_DEBUG=1)
endif()

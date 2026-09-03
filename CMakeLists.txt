cmake_minimum_required(VERSION 3.20)
project(tsplugin_splicemanipulator LANGUAGES CXX)

# -------------------------------------------------------------------------
# Configuration
# -------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(PLUGIN_NAME tsplugin_splicemanipulator)

# -------------------------------------------------------------------------
# Set Variables
# -------------------------------------------------------------------------
if(WIN32)
    set(TSDUCK_ROOT "C:/Program Files/TSDuck" CACHE PATH "TSDuck installation directory")
    set(TSDUCK_INCLUDE_DIR "${TSDUCK_ROOT}/include")
    set(TSDUCK_LIBRARY_DIR "${TSDUCK_ROOT}/lib/Release-Win64")
    set(TSDUCK_LIB "${TSDUCK_LIBRARY_DIR}/tsduck.lib")
    set(TSCORE_LIB "${TSDUCK_LIBRARY_DIR}/tscore.lib")
    set(TSDUCK_PLUGIN_DIR "${TSDUCK_ROOT}/bin")
    set(PLUGIN_FILE "${TSDUCK_PLUGIN_DIR}/${PLUGIN_NAME}.dll")
else()
    set(TSDUCK_ROOT "/usr" CACHE PATH "TSDuck installation directory")
    set(TSDUCK_INCLUDE_DIR "${TSDUCK_ROOT}/include")
    set(TSDUCK_LIBRARY_DIR "${TSDUCK_ROOT}/lib")
    set(TSDUCK_LIB "${TSDUCK_LIBRARY_DIR}/libtsduck.so")
    set(TSCORE_LIB "${TSDUCK_LIBRARY_DIR}/libtscore.so")
    set(TSDUCK_PLUGIN_DIR "${TSDUCK_ROOT}/lib/tsduck")
    set(PLUGIN_FILE "${TSDUCK_PLUGIN_DIR}/${PLUGIN_NAME}.so")
endif()


# -------------------------------------------------------------------------
# Plugin
# -------------------------------------------------------------------------
add_library(${PLUGIN_NAME} SHARED
    src/tsplugin_splicemanipulator.cpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    "${TSDUCK_INCLUDE_DIR}/"
    "${TSDUCK_INCLUDE_DIR}/tsduck"
    "${TSDUCK_INCLUDE_DIR}/tscore"

)

target_compile_definitions(${PLUGIN_NAME} PRIVATE
    _TSDUCKDLL_USE
    _TSCOREDLL_USE
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
    "${TSDUCK_LIB}"
    "${TSCORE_LIB}"
)

# TSDuck plugins must not have the "lib" prefix added by CMake on Unix
set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "")

# -------------------------------------------------------------------------
# MSVC options
# -------------------------------------------------------------------------
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE
        /W4
        /EHsc
    )
endif()

# -------------------------------------------------------------------------
# Installation & Uninstallation 
# -------------------------------------------------------------------------

install(CODE
    "message(STATUS \"Installing ${PLUGIN_NAME} to ${TSDUCK_PLUGIN_DIR}\")"
)

install( 
    FILES $<TARGET_FILE:${PLUGIN_NAME}>
    DESTINATION "${TSDUCK_PLUGIN_DIR}"
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -E echo "Removing ${PLUGIN_FILE}"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${PLUGIN_FILE}"   
)
# pico_sdk_import.cmake
#
# This file is derived from:
#   $PICO_SDK_PATH/external/pico_sdk_import.cmake
#
# It allows the SDK to be located either via the PICO_SDK_PATH environment
# variable or via the cmake variable of the same name.
# A FETCHCONTENT fallback downloads the SDK from GitHub when neither is set.

cmake_minimum_required(VERSION 3.13)

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    message("Using PICO_SDK_PATH from environment ('${PICO_SDK_PATH}')")
endif ()

if (DEFINED PICO_SDK_PATH)
    get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
    if (NOT EXISTS ${PICO_SDK_PATH})
        message(FATAL_ERROR "Directory '${PICO_SDK_PATH}' not found")
    endif ()
    set(PICO_SDK_INIT_CMAKE_FILE ${PICO_SDK_PATH}/pico_sdk_init.cmake)
    if (NOT EXISTS ${PICO_SDK_INIT_CMAKE_FILE})
        message(FATAL_ERROR "Directory '${PICO_SDK_PATH}' does not appear to contain the Raspberry Pi Pico SDK")
    endif ()
    set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
    include(${PICO_SDK_INIT_CMAKE_FILE})
else ()
    # Fall back to fetching the SDK from GitHub
    include(FetchContent)
    set(FETCHCONTENT_QUIET FALSE)
    FetchContent_Declare(
        pico-sdk
        GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
        GIT_TAG        2.1.0
        GIT_SUBMODULES_RECURSE FALSE
    )
    FetchContent_MakeAvailable(pico-sdk)
    set(PICO_SDK_PATH ${pico-sdk_SOURCE_DIR})
    include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
endif ()

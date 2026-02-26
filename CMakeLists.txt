cmake_minimum_required(VERSION 3.13)

# Pull in Raspberry Pi Pico SDK (must be done before project)
include(pico_sdk_import.cmake)

project(rp2350_artnet_node C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(firmware)

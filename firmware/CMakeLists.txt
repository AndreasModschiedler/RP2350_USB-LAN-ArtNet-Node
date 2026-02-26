cmake_minimum_required(VERSION 3.13)

add_executable(rp2350_artnet_node
    src/main.c
    src/usb_descriptors.c
    src/usb_network.c
    src/dhcp_server.c
    src/artnet.c
    src/dmx.c
    src/rdm.c
)

target_include_directories(rp2350_artnet_node PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(rp2350_artnet_node PRIVATE
    PICO_RP2350=1
)

# Link against Pico SDK libraries
target_link_libraries(rp2350_artnet_node
    pico_stdlib
    pico_unique_id
    hardware_uart
    hardware_watchdog
    hardware_gpio
    hardware_irq
    hardware_timer
    tinyusb_device
    tinyusb_board
    pico_lwip
    pico_lwip_nosys
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(rp2350_artnet_node 0)
pico_enable_stdio_uart(rp2350_artnet_node 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(rp2350_artnet_node)

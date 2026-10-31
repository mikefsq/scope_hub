cmake_minimum_required(VERSION 3.13)

# The board is an RP2350A with a 12 MHz crystal and a 16 MB W25Q128 flash.
# The Pico 2 board header matches it except for the flash size.
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include(pico_sdk_import.cmake)

project(scope_hub_fw C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(scope_hub
    src/main.c
    src/i2cbus.c
    src/ina226.c
    src/pca9685.c
    src/env.c
    src/gps.c
    src/ports.c
    src/cmd.c
    src/hidif.c
    src/usb_descriptors.c
    src/dew.c
    src/ledauto.c
    src/settings.c
    src/labels.c
)

# tusb_config.h lives in src/.
target_include_directories(scope_hub PRIVATE src)

target_compile_definitions(scope_hub PRIVATE
    PICO_FLASH_SIZE_BYTES=16777216
    BOARD_TUD_RHPORT=0
    FW_VERSION="0.1.0"
)

target_compile_options(scope_hub PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(scope_hub
    pico_stdlib
    pico_unique_id
    hardware_i2c
    hardware_uart
    hardware_watchdog
    tinyusb_device
    hardware_flash
)

# USB carries the HID interface only. UART0 on J19 carries text commands for debugging.
pico_enable_stdio_usb(scope_hub 0)
pico_enable_stdio_uart(scope_hub 1)

pico_add_extra_outputs(scope_hub)

cmake_minimum_required(VERSION 3.13)

# Expect PICO_SDK_PATH in environment or cache.
include(pico_sdk_import.cmake)

project(mellivora_picocalc C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(mellivora_picocalc
    src/main.c
    src/lcd.c
    src/kbd.c
    src/sd.c
    src/fat.c
    src/syscall.c
    src/apps.c
    src/apps/text.c
    src/apps/hex.c
    src/apps/fs.c
    src/net.c
    src/netapps.c
)

target_include_directories(mellivora_picocalc PRIVATE src)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mellivora_picocalc PRIVATE
        -Wall
        -Wextra
        -Wshadow
        -Wundef
        -Wno-unused-parameter
    )
endif()

target_compile_definitions(mellivora_picocalc PRIVATE
    PICO_DEFAULT_UART_BAUD_RATE=115200
    MELLIVORA_VERSION="2.2.0"
    MELLIVORA_BUILD_DATE="${CMAKE_PROJECT_NAME}-${CMAKE_BUILD_TYPE}"
)

target_link_libraries(mellivora_picocalc
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_i2c
    hardware_gpio
    hardware_clocks
    hardware_timer
    hardware_pwm
    hardware_watchdog
    hardware_dma
)

# Pico 2W WiFi support — add CYW43 and lwIP when board supports it
if (PICO_CYW43_SUPPORTED)
    target_compile_definitions(mellivora_picocalc PRIVATE
        PICO_CYW43_SUPPORTED=1
    )
    target_link_libraries(mellivora_picocalc
        pico_cyw43_arch_lwip_poll
    )
endif()

# Enable both USB CDC and UART console
pico_enable_stdio_usb(mellivora_picocalc 1)
pico_enable_stdio_uart(mellivora_picocalc 1)

pico_add_extra_outputs(mellivora_picocalc)

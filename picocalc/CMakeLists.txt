cmake_minimum_required(VERSION 3.13)

# Expect PICO_SDK_PATH in environment or cache.
include(pico_sdk_import.cmake)

project(mellivora_picocalc C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

option(MELLIVORA_WERROR "Treat compiler warnings as errors" ON)

set(MELLIVORA_SOURCES
    src/main.c
    src/lcd.c
    src/kbd.c
    src/sd.c
    src/fat.c
    src/syscall.c
    src/console.c
    src/audio.c
    src/pipe.c
    src/apps.c
    src/ui/ui.c
    src/apps/app_shared.c
    src/apps/app_util.c
    src/apps/app_settings.c
    src/apps/app_editor.c
    src/apps/app_langs.c
    src/apps/app_productivity.c
    src/apps/app_games.c
    src/apps/app_system.c
    src/apps/app_builtins.c
    src/apps/app_tools.c
    src/apps/text.c
    src/apps/hex.c
    src/apps/fs.c
    src/core/expr.c
    src/core/shell_parse.c
    src/core/path_util.c
    src/core/fatname.c
    src/core/simple_re.c
    src/core/glob.c
    src/core/textutil.c
    src/core/lfn.c
    src/net.c
    src/netapps.c
    src/hal/hal_pico.c
)

add_executable(mellivora_picocalc ${MELLIVORA_SOURCES})

target_include_directories(mellivora_picocalc PRIVATE src)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Scope the strict flags to Mellivora sources: the Pico SDK is not
    # warning-clean under -Wundef/-Werror.
    set(MELLIVORA_WARNINGS -Wall -Wextra -Wshadow -Wundef -Wno-unused-parameter)
    if (MELLIVORA_WERROR)
        list(APPEND MELLIVORA_WARNINGS -Werror)
    endif()
    set_source_files_properties(${MELLIVORA_SOURCES}
        PROPERTIES COMPILE_OPTIONS "${MELLIVORA_WARNINGS}")
endif()

target_compile_definitions(mellivora_picocalc PRIVATE
    PICO_DEFAULT_UART_BAUD_RATE=115200
    MELLIVORA_VERSION="2.5.0"
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

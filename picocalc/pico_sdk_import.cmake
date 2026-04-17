# Local wrapper that prefers a workspace-local Pico SDK checkout,
# while still honoring PICO_SDK_PATH when it is explicitly set.

if (DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()

if (NOT PICO_SDK_PATH)
    set(_LOCAL_PICO_SDK "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
    if (EXISTS "${_LOCAL_PICO_SDK}/external/pico_sdk_import.cmake")
        set(PICO_SDK_PATH "${_LOCAL_PICO_SDK}")
    endif ()
endif ()

if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is not set, and no local pico-sdk checkout was found in picocalc/pico-sdk.")
endif ()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

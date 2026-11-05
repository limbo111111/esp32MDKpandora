option(PANDORA_DISPLAY_TEST_PATTERN
    "Build the synthetic display test without emulator firmware" OFF)

set(component_sources
    "main.cpp"
    "pandora_display_link.cpp"
    "ppi2c/i2c_slave_driver.c"
    "ppi2c/pp_handler.cpp"
)
set(component_requires
    driver


    esp_timer
    log
)

if(PANDORA_DISPLAY_TEST_PATTERN)
    list(APPEND component_sources "display_test_pattern.cpp")
else()
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/firmware.bin")
        message(FATAL_ERROR
            "Missing firmware.bin. Place your verified flat firmware image in "
            "${CMAKE_CURRENT_LIST_DIR}/firmware.bin before building.")
    endif()
    list(APPEND component_sources "pandora/emulator.c")
    list(APPEND component_requires thumbcore)
endif()

set(component_args
    SRCS ${component_sources}
    INCLUDE_DIRS
        "."
        "./ppi2c"
        "./pandora"
        "../../common"
        "../../uart/build"
    REQUIRES ${component_requires}
)

if(NOT PANDORA_DISPLAY_TEST_PATTERN)
    list(APPEND component_args EMBED_FILES "firmware.bin")
endif()

idf_component_register(${component_args})

if(PANDORA_DISPLAY_TEST_PATTERN)
    target_compile_definitions(${COMPONENT_LIB}
        PRIVATE PANDORA_DISPLAY_TEST_PATTERN=1)
    message(STATUS
        "Pandora View: building synthetic display test; firmware is not loaded")
endif()

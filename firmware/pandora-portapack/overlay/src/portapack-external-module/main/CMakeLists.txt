if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/firmware.bin")
    message(FATAL_ERROR
        "Missing firmware.bin. Place your verified flat firmware image in "
        "${CMAKE_CURRENT_LIST_DIR}/firmware.bin before building.")
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "pandora_display_link.cpp"
        "ppi2c/i2c_slave_driver.c"
        "ppi2c/pp_handler.cpp"
        "pandora/emulator.c"
    INCLUDE_DIRS
        "."
        "./ppi2c"
        "./pandora"
        "../../common"
        "../../uart/build"
    REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_timer
        log
        thumbcore
    EMBED_FILES
        "firmware.bin"
)

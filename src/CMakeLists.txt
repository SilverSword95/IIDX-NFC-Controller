add_executable(SilverIIDX
        main.cpp
        usb_descriptors.c
		nfc/nfc.c nfc/pn532.c
		controller_simulator.cpp)

target_include_directories(SilverIIDX PRIVATE ${CMAKE_CURRENT_LIST_DIR})

pico_set_linker_script(SilverIIDX ${CMAKE_CURRENT_LIST_DIR}/memmap.ld)
pico_generate_pio_header(SilverIIDX ${CMAKE_CURRENT_LIST_DIR}/encoders.pio)
pico_generate_pio_header(SilverIIDX ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
pico_generate_pio_header(SilverIIDX ${CMAKE_CURRENT_LIST_DIR}/psxSPI.pio)
target_sources(SilverIIDX PRIVATE main.cpp)

target_link_libraries(SilverIIDX PRIVATE
        pico_multicore
        pico_stdlib
        tinyusb_device
        tinyusb_board
        hardware_pio
        hardware_dma
        hardware_irq
		hardware_pwm
		hardware_adc
		hardware_i2c
		hardware_spi
		hardware_watchdog
		pico_unique_id)

pico_add_extra_outputs(SilverIIDX)

add_custom_command(TARGET SilverIIDX
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_BINARY_DIR}/src/SilverIIDX.uf2 ${PROJECT_SOURCE_DIR}/build_uf2/SilverIIDX.uf2
)

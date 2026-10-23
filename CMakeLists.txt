# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(seguidor_de_linha C CXX ASM)

pico_sdk_init()

add_executable(seguidor_de_linha
        src/main.c
        src/motors.c
        src/encoders.c
        src/line_sensor.c
        src/analog.c
        src/comm.c
        src/experiment.c
        lib/dhcpserver/dhcpserver.c
)

pico_set_program_name(seguidor_de_linha "seguidor_de_linha")
pico_set_program_version(seguidor_de_linha "0.3")

pico_enable_stdio_uart(seguidor_de_linha 0)
pico_enable_stdio_usb(seguidor_de_linha 1)

pico_generate_pio_header(seguidor_de_linha ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
pico_generate_pio_header(seguidor_de_linha ${CMAKE_CURRENT_LIST_DIR}/src/line_rc.pio)

target_include_directories(seguidor_de_linha PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/lib/dhcpserver
)


target_link_libraries(seguidor_de_linha
        pico_stdlib
        hardware_pwm
        hardware_pio
        hardware_adc
        hardware_clocks
        hardware_dma
        pico_multicore
        pico_cyw43_arch_lwip_threadsafe_background
)

# Commit do git embutido no firmware (comando "info"), para rastrear os ensaios
add_custom_target(git_version
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
            -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/generated/git_version.h
            -P ${CMAKE_CURRENT_LIST_DIR}/cmake/git_version.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/generated/git_version.h
)
add_dependencies(seguidor_de_linha git_version)
target_include_directories(seguidor_de_linha PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

pico_add_extra_outputs(seguidor_de_linha)

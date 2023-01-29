cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(pico-dino)

pico_sdk_init()

add_executable(pico-dino
    dino.c
    picoLCD/LCDops.c
    picoLCD/generalOps.c
    picoLCD/presetChars.c
    picoLCD/presetMessages.c
)

pico_set_program_name(pico-dino "pico-dino")

pico_enable_stdio_usb(pico-dino 1)
pico_enable_stdio_uart(pico-dino 0)
pico_add_extra_outputs(pico-dino)

# Add the standard include files to the build
target_include_directories(pico-dino PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

target_link_libraries(pico-dino pico_stdlib pico_rand)

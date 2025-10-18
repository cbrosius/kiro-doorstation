cmake_minimum_required(VERSION 3.16)

# Set IDF_PATH if not already set
if(NOT DEFINED ENV{IDF_PATH})
    set(ENV{IDF_PATH} "C:/ESP-IDF/v5.5.1/esp-idf")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sip_doorbell)
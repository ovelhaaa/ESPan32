cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH})
  message(FATAL_ERROR "IDF_PATH environment variable is not defined. Please activate the ESP-IDF environment.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pocket_pan)

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.5)

# Setting the default target to be "esp32" chip
set(IDF_TARGET "esp32")

# CMakeLists.txt from ESP-ADF
# ESP-ADF version used: v2.6
include($ENV{ADF_PATH}/CMakeLists.txt)
# Required depencancy file from ESP-IDF
# ESP-IDF version used: v4.4.4
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The project's name
project(peripheral-node)

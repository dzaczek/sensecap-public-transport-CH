# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Changed from ../../components to local components folder for standalone usage
set(EXTRA_COMPONENT_DIRS components)

set(EXCLUDE_COMPONENTS LoRaWAN)

add_compile_options(-fdiagnostics-color=always -w)

project(indicator_public_transport)

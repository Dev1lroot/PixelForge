cmake_minimum_required(VERSION 3.20)
project(PixelForge VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)
find_package(ZLIB REQUIRED)

file(GLOB CORE_SOURCES src/*.cpp src/*.h)
list(REMOVE_ITEM CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

add_library(pixelcore STATIC ${CORE_SOURCES})
target_include_directories(pixelcore PUBLIC src)
target_link_libraries(pixelcore PUBLIC Qt6::Widgets Qt6::PrintSupport ZLIB::ZLIB)
target_compile_options(pixelcore PRIVATE -Wall -Wno-unused-parameter)

add_executable(pixelforge src/main.cpp)
target_link_libraries(pixelforge PRIVATE pixelcore)

option(PIXELFORGE_TESTS "Build the headless self-test" ON)
if(PIXELFORGE_TESTS)
  add_executable(pixelforge_selftest tests/selftest.cpp)
  target_link_libraries(pixelforge_selftest PRIVATE pixelcore)
endif()

install(TARGETS pixelforge RUNTIME DESTINATION bin)
install(FILES pixelforge.desktop DESTINATION share/applications)

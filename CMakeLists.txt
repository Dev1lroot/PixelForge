cmake_minimum_required(VERSION 3.20)
project(PixelForge VERSION 1.1 LANGUAGES CXX)

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

# ---- plugins (only need src/pluginapi.h and Qt)
function(pixelforge_plugin name)
  add_library(${name} MODULE ${ARGN})
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(${name} PRIVATE Qt6::Widgets)
  set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
  install(TARGETS ${name} LIBRARY DESTINATION lib/pixelforge/plugins)
endfunction()

option(PIXELFORGE_EXAMPLE_PLUGINS "Build the example plugins in plugins/examples" ON)
if(PIXELFORGE_EXAMPLE_PLUGINS)
  pixelforge_plugin(hello plugins/examples/hello/hello.cpp plugins/examples/hello/hello.h)
  pixelforge_plugin(histogram plugins/examples/histogram/histogram.cpp plugins/examples/histogram/histogram.h)
  pixelforge_plugin(exportlayers plugins/examples/exportlayers/exportlayers.cpp plugins/examples/exportlayers/exportlayers.h)
endif()

option(PIXELFORGE_TESTS "Build the headless self-test" ON)
if(PIXELFORGE_TESTS)
  add_executable(pixelforge_selftest tests/selftest.cpp)
  target_link_libraries(pixelforge_selftest PRIVATE pixelcore)
  if(PIXELFORGE_EXAMPLE_PLUGINS)
    target_compile_definitions(pixelforge_selftest PRIVATE PIXELFORGE_HAVE_EXAMPLES)
    add_dependencies(pixelforge_selftest hello histogram exportlayers)
  endif()
endif()

# Plugins developed outside this repository: any plugins/<name>/CMakeLists.txt is picked up
# (plugins/examples is built above). They can use pixelforge_plugin() and extend the self-test.
file(GLOB LOCAL_PLUGIN_LISTS ${CMAKE_SOURCE_DIR}/plugins/*/CMakeLists.txt)
foreach(list ${LOCAL_PLUGIN_LISTS})
  get_filename_component(dir ${list} DIRECTORY)
  add_subdirectory(${dir})
endforeach()

install(TARGETS pixelforge RUNTIME DESTINATION bin)
install(FILES pixelforge.desktop DESTINATION share/applications)

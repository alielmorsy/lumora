# JS scripting on Hermes. Module code includes <jsi/jsi.h> through lumora::script.
include(${LUMORA_ROOT_DIR}/cmake/deps/hermes.cmake)

add_library(lumora_script STATIC Source/Script.cpp Source/Bridge.cpp Source/JsApp.cpp)
add_library(lumora::script ALIAS lumora_script)
lumora_defaults(lumora_script)
target_include_directories(lumora_script PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>)
target_link_libraries(lumora_script PUBLIC lumora::base hermes PRIVATE lumora::lumora lumora::log)

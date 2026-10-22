add_library(lumora_base INTERFACE)
add_library(lumora::base ALIAS lumora_base)
target_include_directories(lumora_base INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>)
target_compile_features(lumora_base INTERFACE cxx_std_20)

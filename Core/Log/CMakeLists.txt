if (NOT TARGET spdlog AND NOT TARGET spdlog::spdlog)
    include(add_spdlog.cmake)
endif ()

add_library(lumora_log STATIC Source/Log.cpp Source/Spdlog.cpp)
add_library(lumora::log ALIAS lumora_log)
lumora_defaults(lumora_log)
target_include_directories(lumora_log PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>)
target_link_libraries(lumora_log PUBLIC lumora::base spdlog::spdlog)

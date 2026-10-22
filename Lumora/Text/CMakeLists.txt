# Fonts, shaping, bidi and line breaking.
include(${LUMORA_ROOT_DIR}/cmake/deps/freetype.cmake)
include(${LUMORA_ROOT_DIR}/cmake/deps/harfbuzz.cmake)
include(${LUMORA_ROOT_DIR}/cmake/deps/sheenbidi.cmake)
include(${LUMORA_ROOT_DIR}/cmake/deps/unibreak.cmake)

add_library(lumora_text STATIC Source/FontSystem.cpp Source/GlyphAtlas.cpp)
add_library(lumora::text ALIAS lumora_text)
lumora_defaults(lumora_text)
target_include_directories(lumora_text PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include> PRIVATE Source)
target_link_libraries(lumora_text PUBLIC lumora::lumora PRIVATE lumora::log freetype harfbuzz SheenBidi unibreak)

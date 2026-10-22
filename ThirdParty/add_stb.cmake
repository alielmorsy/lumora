# stb: vendored header-only target (stb_image.h, used by the renderer's image provider).
include_guard(GLOBAL)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${CMAKE_CURRENT_LIST_DIR}/stb")

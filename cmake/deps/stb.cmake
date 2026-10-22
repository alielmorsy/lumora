# stb: vendored header-only target (stb_image.h, used by the renderer's image provider).
include_guard(GLOBAL)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${LUMORA_ROOT_DIR}/ThirdParty/stb")

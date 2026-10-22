# libunibreak: UAX#14 line breaks for lumora_text. Sources compiled directly as target "unibreak".
include_guard(GLOBAL)
include(${LUMORA_ROOT_DIR}/cmake/FetchThirdParty.cmake)

lumora_fetch_thirdparty(unibreak
        REPO adah1972/libunibreak
        TAG libunibreak_6_1
        MANUAL
        DEST "${CMAKE_CURRENT_LIST_DIR}/ThirdParty/unibreak"
        GLOB src/*.c
        INCLUDE src)

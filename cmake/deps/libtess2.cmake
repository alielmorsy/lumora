# libtess2: polygon tessellation for SVG fill. Sources compiled directly as target "libtess2".
include_guard(GLOBAL)
include(${LUMORA_ROOT_DIR}/cmake/FetchThirdParty.cmake)

lumora_fetch_thirdparty(libtess2
        REPO memononen/libtess2
        TAG v1.0.2
        MANUAL
        GLOB Source/*.c
        INCLUDE Include Source)

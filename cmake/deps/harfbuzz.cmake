# HarfBuzz: text shaping for lumora_text. Independent of FreeType.
include_guard(GLOBAL)
include(${LUMORA_ROOT_DIR}/cmake/FetchThirdParty.cmake)

lumora_fetch_thirdparty(harfbuzz
        REPO harfbuzz/harfbuzz
        TAG 10.1.0
        CMAKE
        OPTIONS "HB_HAVE_FREETYPE=OFF" "HB_BUILD_TESTS=OFF" "HB_BUILD_UTILS=OFF")

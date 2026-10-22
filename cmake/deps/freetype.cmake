# FreeType: glyph rasteriser for lumora_text. Independent of HarfBuzz (no hb-ft).
include_guard(GLOBAL)
include(${LUMORA_ROOT_DIR}/cmake/FetchThirdParty.cmake)

lumora_fetch_thirdparty(freetype
        REPO freetype/freetype
        TAG VER-2-13-3
        CMAKE
        OPTIONS "FT_DISABLE_HARFBUZZ=ON" "FT_DISABLE_BROTLI=ON" "FT_DISABLE_BZIP2=ON" "FT_DISABLE_PNG=OFF" "FT_DISABLE_ZLIB=ON")

# SheenBidi: Unicode bidi algorithm for lumora_text.
include_guard(GLOBAL)
include(${LUMORA_ROOT_DIR}/cmake/FetchThirdParty.cmake)

lumora_fetch_thirdparty(SheenBidi
        REPO Tehreer/SheenBidi
        TAG v3.0.0
        CMAKE
        DEST "${CMAKE_CURRENT_LIST_DIR}/ThirdParty/SheenBidi")

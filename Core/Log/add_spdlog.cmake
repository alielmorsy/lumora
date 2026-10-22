# spdlog: backend of lumora_log.
include_guard(GLOBAL)
include(${LUMORA_ROOT_DIR}/cmake/FetchThirdParty.cmake)

lumora_fetch_thirdparty(spdlog
        REPO gabime/spdlog
        TAG v1.15.1
        CMAKE
        DEST "${CMAKE_CURRENT_LIST_DIR}/ThirdParty/spdlog")

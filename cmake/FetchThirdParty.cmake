include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/AddThirdParty.cmake)

find_package(Git QUIET)

option(LUMORA_FETCH_SUBMODULES "Fetch submodules of fetched third-party libraries" ON)

# Shallow-clones <REPO>@<TAG> into DEST (once; an existing DEST is never touched, a changed tag
# re-fetches), then turns it into a target.
#   lumora_fetch_thirdparty(<name> REPO <owner/repo|url> TAG <tag> [DEST <dir>] [SUBMODULES]
#                           [CMAKE [OPTIONS "VAR=VALUE"...]] [MANUAL] [FETCH_ONLY] [SUBDIR <dir>] [ALIAS <ns::name>]
#                           [INCLUDE ...] [SOURCES ...] [GLOB ...] [DEFINES ...] [LINK ...])
# MANUAL (or any of INCLUDE/SOURCES/GLOB/DEFINES) builds it with lumora_add_thirdparty; otherwise the
# library's own CMakeLists.txt is added with add_subdirectory. FETCH_ONLY stops after the clone.
function(lumora_fetch_thirdparty NAME)
    cmake_parse_arguments(FT
            "SUBMODULES;CMAKE;MANUAL;FETCH_ONLY"
            "REPO;TAG;DEST;ALIAS;SUBDIR"
            "OPTIONS;INCLUDE;SOURCES;GLOB;DEFINES;LINK"
            ${ARGN})

    if (NOT FT_REPO OR NOT FT_TAG)
        message(FATAL_ERROR "lumora_fetch_thirdparty(${NAME}): REPO and TAG are required")
    endif ()

    if (FT_REPO MATCHES "://" OR FT_REPO MATCHES "@")
        set(_url "${FT_REPO}")
    else ()
        set(_url "https://github.com/${FT_REPO}.git")
    endif ()

    if (NOT FT_DEST)
        set(FT_DEST "${LUMORA_ROOT_DIR}/ThirdParty/${NAME}")
    endif ()

    # A matching .fetch_tag stamp means fetched by us; a dir without a stamp is a vendored copy.
    set(_fetched FALSE)
    if (EXISTS "${FT_DEST}/.fetch_tag")
        file(READ "${FT_DEST}/.fetch_tag" _have)
        string(STRIP "${_have}" _have)
        if (_have STREQUAL FT_TAG)
            set(_fetched TRUE)
        else ()
            message(STATUS "ThirdParty: ${NAME} ${_have} -> ${FT_TAG}, refetching")
            file(REMOVE_RECURSE "${FT_DEST}")
        endif ()
    elseif (EXISTS "${FT_DEST}")
        set(_fetched TRUE)
    endif ()

    if (NOT _fetched)
        if (NOT GIT_EXECUTABLE) # cached, so visible from every directory scope; Git_FOUND is not
            message(FATAL_ERROR "lumora_fetch_thirdparty(${NAME}): git not found")
        endif ()
        set(_sub "")
        if (FT_SUBMODULES AND LUMORA_FETCH_SUBMODULES)
            set(_sub --recurse-submodules --shallow-submodules)
        endif ()
        message(STATUS "ThirdParty: fetching ${NAME} ${FT_TAG} <- ${_url}")
        execute_process(
                COMMAND ${GIT_EXECUTABLE} clone --depth 1 --branch ${FT_TAG} ${_sub} ${_url} ${FT_DEST}
                RESULT_VARIABLE _res
                ERROR_VARIABLE _err)
        if (NOT _res EQUAL 0)
            file(REMOVE_RECURSE "${FT_DEST}")
            message(FATAL_ERROR "lumora_fetch_thirdparty(${NAME}): clone failed:\n${_err}")
        endif ()
        file(WRITE "${FT_DEST}/.fetch_tag" "${FT_TAG}\n")
    endif ()
    if (FT_FETCH_ONLY)
        return()
    endif ()

    set(_cmake_src "${FT_DEST}")
    if (FT_SUBDIR)
        set(_cmake_src "${FT_DEST}/${FT_SUBDIR}")
    endif ()

    set(_manual FALSE)
    if (FT_MANUAL OR FT_INCLUDE OR FT_SOURCES OR FT_GLOB OR FT_DEFINES)
        set(_manual TRUE)
    elseif (NOT FT_CMAKE AND NOT EXISTS "${_cmake_src}/CMakeLists.txt")
        set(_manual TRUE)
    endif ()

    if (FT_CMAKE AND NOT EXISTS "${_cmake_src}/CMakeLists.txt")
        message(FATAL_ERROR "lumora_fetch_thirdparty(${NAME}): no CMakeLists.txt at '${_cmake_src}' (pass SUBDIR)")
    endif ()

    if (_manual)
        set(_args ROOT "${FT_DEST}")
        if (FT_ALIAS)
            list(APPEND _args ALIAS ${FT_ALIAS})
        endif ()
        foreach (kw INCLUDE SOURCES GLOB DEFINES LINK)
            if (FT_${kw})
                list(APPEND _args ${kw} ${FT_${kw}})
            endif ()
        endforeach ()
        lumora_add_thirdparty(${NAME} ${_args})
    else ()
        foreach (opt IN LISTS FT_OPTIONS)
            if (opt MATCHES "^([^=]+)=(.*)$")
                set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" CACHE INTERNAL "" FORCE)
            endif ()
        endforeach ()
        add_subdirectory("${_cmake_src}" "${PROJECT_BINARY_DIR}/ThirdParty/${NAME}" EXCLUDE_FROM_ALL)
        if (FT_ALIAS)
            if (TARGET ${NAME})
                add_library(${FT_ALIAS} ALIAS ${NAME})
            else ()
                message(WARNING "lumora_fetch_thirdparty(${NAME}): ALIAS set but no target '${NAME}'")
            endif ()
        endif ()
    endif ()
endfunction()

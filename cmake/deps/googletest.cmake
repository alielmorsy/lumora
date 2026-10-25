# GoogleTest via FetchContent from release archive
include_guard(GLOBAL)

include(FetchContent)

set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
if (EMSCRIPTEN)
    set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)
endif ()

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
FetchContent_MakeAvailable(googletest)

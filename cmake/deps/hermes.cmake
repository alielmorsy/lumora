# Hermes V1 JS engine for lumora_script, as INTERFACE target "hermes" (static VM + jsi + headers).
# Tag hermes-v250829098.0.16 is the Hermes V1 release reactnative.dev/releases/branches names for
# RN 0.86/0.87 (both *-stable branches have since moved to .0.17).
# Built as its own CMake project: its bundled llvh declares gtest/gtest_main/FileCheck/not/count
# targets unconditionally, which clash with our googletest in one tree. Tools stay on because the VM
# build runs hermesc for InternalBytecode; only hermesvm_a and jsi (and what they need) get built.
# Intl is off, so Windows needs only the Win10 system ICU (icuuc/icuin) plus winmm (sampling profiler).
# MSVC builds inject hermes-msvc.cmake into the Hermes project; no Hermes source is patched.
# ponytail: single-config generators only (CMAKE_BUILD_TYPE), pass the config through for VS/Xcode.
include_guard(GLOBAL)
include(${LUMORA_ROOT_DIR}/cmake/FetchThirdParty.cmake)
include(ExternalProject)

lumora_fetch_thirdparty(hermes REPO facebook/hermes TAG hermes-v250829098.0.16 FETCH_ONLY)

set(_hermes_src "${LUMORA_ROOT_DIR}/ThirdParty/hermes")
set(_hermes_bin "${PROJECT_BINARY_DIR}/ThirdParty/hermes")
set(_hermes_libs
        "${_hermes_bin}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}hermesvm_a${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${_hermes_bin}/jsi/${CMAKE_STATIC_LIBRARY_PREFIX}jsi${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${_hermes_bin}/external/boost/boost_1_86_0/libs/context/${CMAKE_STATIC_LIBRARY_PREFIX}boost_context${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_hermes_extra "")
if (MSVC)
    set(_hermes_extra "-DCMAKE_PROJECT_Hermes_INCLUDE=${CMAKE_CURRENT_LIST_DIR}/hermes-msvc.cmake")
endif ()

ExternalProject_Add(hermes_build
        SOURCE_DIR "${_hermes_src}"
        BINARY_DIR "${_hermes_bin}"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
            -DHERMES_ENABLE_TEST_SUITE=OFF
            -DHERMES_ENABLE_TOOLS=ON
            -DHERMES_BUILD_APPLE_FRAMEWORK=OFF
            -DHERMES_ENABLE_INTL=OFF
            -DHERMES_ENABLE_DEBUGGER=OFF
            -DHERMES_BUILD_SHARED_JSI=OFF
            -DHERMES_SLOW_DEBUG=OFF
            -DHERMES_MSVC_MP=OFF
            ${_hermes_extra}
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target hermesvm_a jsi
        BUILD_ALWAYS ON # a no-op inner build, but it picks up a re-fetched tag
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS ${_hermes_libs})

add_library(hermes INTERFACE)
add_dependencies(hermes hermes_build)
target_include_directories(hermes INTERFACE "${_hermes_src}/public" "${_hermes_src}/API" "${_hermes_src}/API/jsi")
target_link_libraries(hermes INTERFACE ${_hermes_libs})
if (WIN32)
    target_link_libraries(hermes INTERFACE icuuc icuin winmm)
endif ()

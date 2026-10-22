include(${LUMORA_ROOT_DIR}/cmake/deps/googletest.cmake)
include(GoogleTest)

set(LUMORA_UI_SOURCE_DIR "${LUMORA_ROOT_DIR}/Lumora/UI/Source")

# gtest executable linked to lumora + gtest_main, registered with ctest.
function(lumora_add_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LINK" ${ARGN})
    add_executable(${name} ${T_SOURCES})
    target_link_libraries(${name} PRIVATE lumora::lumora lumora::log GTest::gtest GTest::gtest_main ${T_LINK})
    target_include_directories(${name} PRIVATE ${LUMORA_UI_SOURCE_DIR})
    lumora_defaults(${name})
    lumora_copy_runtime_dlls(${name})
    gtest_discover_tests(${name})
endfunction()

if (WIN32)
    add_subdirectory(Core/Platform)
endif ()
if (LUMORA_BUILD_GPU)
    add_subdirectory(Core/RHI)
    add_subdirectory(Renderer)
endif ()
add_subdirectory(UI)

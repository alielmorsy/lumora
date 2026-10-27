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
    if (EMSCRIPTEN)
        # Runs under node (CMAKE_CROSSCOMPILING_EMULATOR), one ctest entry per binary: gtest discovery passes
        # Windows absolute paths that gtest in wasm treats as relative. NODERAWFS gives tests the host filesystem.
        target_link_options(${name} PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
        add_test(NAME ${name} COMMAND ${name})
    else ()
        gtest_discover_tests(${name})
    endif ()
endfunction()

if (WIN32 OR LUMORA_PLATFORM_LINUX_DESKTOP)
    add_subdirectory(Core/Platform)
endif ()
if (LUMORA_BUILD_GPU)
    if (NOT EMSCRIPTEN) # needs dxc SPIR-V shaders and a Vulkan device
        add_subdirectory(Core/RHI)
    endif ()
    add_subdirectory(Renderer)
endif ()
add_subdirectory(UI)
if (TARGET lumora_script)
    add_subdirectory(Script)
endif ()

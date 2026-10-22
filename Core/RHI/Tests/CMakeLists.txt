include(${PROJECT_SOURCE_DIR}/ThirdParty/add_googletest.cmake)

find_program(LUMORA_DXC NAMES dxc HINTS "$ENV{VULKAN_SDK}/Bin")
if (NOT LUMORA_DXC)
    message(FATAL_ERROR "dxc not found in VULKAN_SDK: cannot compile test shaders")
endif ()

set(_spirv_dir "${CMAKE_CURRENT_BINARY_DIR}/spirv")
set(_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/TestShader.hlsl")
set(_vert_spv "${_spirv_dir}/TestShader.vert.spv")
set(_frag_spv "${_spirv_dir}/TestShader.frag.spv")
set(_attr_vert_spv "${_spirv_dir}/TestShaderAttr.vert.spv")

add_custom_command(
    OUTPUT "${_vert_spv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_spirv_dir}"
    COMMAND ${LUMORA_DXC} -spirv -T vs_6_0 -E VSMain "${_hlsl}" -Fo "${_vert_spv}"
    DEPENDS "${_hlsl}"
    COMMENT "dxc TestShader.vert.spv"
    VERBATIM)

add_custom_command(
    OUTPUT "${_frag_spv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_spirv_dir}"
    COMMAND ${LUMORA_DXC} -spirv -T ps_6_0 -E PSMain "${_hlsl}" -Fo "${_frag_spv}"
    DEPENDS "${_hlsl}"
    COMMENT "dxc TestShader.frag.spv"
    VERBATIM)

add_custom_command(
    OUTPUT "${_attr_vert_spv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_spirv_dir}"
    COMMAND ${LUMORA_DXC} -spirv -T vs_6_0 -E VSMainWithAttr "${_hlsl}" -Fo "${_attr_vert_spv}"
    DEPENDS "${_hlsl}"
    COMMENT "dxc TestShaderAttr.vert.spv"
    VERBATIM)

add_custom_target(rhi_test_shaders DEPENDS "${_vert_spv}" "${_frag_spv}" "${_attr_vert_spv}")

add_executable(lumora_rhi_tests RhiTests.cpp)
add_dependencies(lumora_rhi_tests rhi_test_shaders)
lumora_defaults(lumora_rhi_tests)
target_include_directories(lumora_rhi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Vulkan)
target_compile_definitions(lumora_rhi_tests PRIVATE TEST_SPIRV_DIR="${_spirv_dir}")
target_link_libraries(lumora_rhi_tests PRIVATE lumora::rhi lumora::log GTest::gtest GTest::gtest_main)

if (ANDROID)
    target_link_libraries(lumora_rhi_tests PRIVATE vulkan)
else ()
    find_package(Vulkan REQUIRED)
    target_link_libraries(lumora_rhi_tests PRIVATE Vulkan::Vulkan)
endif ()

include(GoogleTest)
gtest_discover_tests(lumora_rhi_tests)

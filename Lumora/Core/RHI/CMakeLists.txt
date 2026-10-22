file(GLOB _rhi_sources CONFIGURE_DEPENDS Source/Vulkan/*.cpp)
add_library(lumora_rhi STATIC ${_rhi_sources})
add_library(lumora::rhi ALIAS lumora_rhi)
lumora_defaults(lumora_rhi)
target_include_directories(lumora_rhi
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        PRIVATE Source/Vulkan)
target_include_directories(lumora_rhi SYSTEM PRIVATE ${LUMORA_ROOT_DIR}/ThirdParty/vma/include)
if (WIN32)
    target_compile_definitions(lumora_rhi PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()
target_link_libraries(lumora_rhi PRIVATE lumora::log)

if (ANDROID)
    target_link_libraries(lumora_rhi PRIVATE vulkan)
else ()
    find_package(Vulkan REQUIRED)
    target_link_libraries(lumora_rhi PRIVATE Vulkan::Vulkan)
endif ()

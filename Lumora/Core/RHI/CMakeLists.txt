if (EMSCRIPTEN)
    set(_rhi_backend WebGPU)
else ()
    set(_rhi_backend Vulkan)
endif ()
file(GLOB _rhi_sources CONFIGURE_DEPENDS Source/${_rhi_backend}/*.cpp)
add_library(lumora_rhi STATIC ${_rhi_sources})
add_library(lumora::rhi ALIAS lumora_rhi)
lumora_defaults(lumora_rhi)
target_include_directories(lumora_rhi
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        PRIVATE Source/${_rhi_backend})
if (WIN32)
    target_compile_definitions(lumora_rhi PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()
target_link_libraries(lumora_rhi PRIVATE lumora::log)

if (EMSCRIPTEN)
    # webgpu.h for compiling; executables linking lumora_rhi get the JS side from the same flag.
    target_compile_options(lumora_rhi PUBLIC --use-port=emdawnwebgpu)
    # emscan-deps ignores --use-port and so cannot find webgpu.h; nothing here uses modules.
    set_target_properties(lumora_rhi PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    # ASYNCIFY lets RHI::Init block on wgpuInstanceWaitAny for the adapter and device. JSPI would be cheaper but
    # Safari, Firefox and the emsdk node used by ctest lack it.
    target_link_options(lumora_rhi PUBLIC --use-port=emdawnwebgpu -sASYNCIFY)
    return()
endif ()

target_include_directories(lumora_rhi SYSTEM PRIVATE ${LUMORA_ROOT_DIR}/ThirdParty/vma/include)
if (ANDROID)
    target_link_libraries(lumora_rhi PRIVATE vulkan)
else ()
    find_package(Vulkan REQUIRED)
    target_link_libraries(lumora_rhi PRIVATE Vulkan::Vulkan)
endif ()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # vulkan_xlib.h and vulkan_wayland.h need the X11 and Wayland client headers; the libraries themselves are the platform's.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_rhi_surface REQUIRED x11 wayland-client)
    target_include_directories(lumora_rhi SYSTEM PRIVATE ${_rhi_surface_INCLUDE_DIRS})
endif ()

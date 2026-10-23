# RHI painter: batches Lumora display lists into Vulkan draws.
include(${LUMORA_ROOT_DIR}/cmake/deps/stb.cmake)

add_library(lumora_renderer STATIC
    Source/Lumora.cpp
    Source/RHIPainter.cpp
    Source/RHICanvas.cpp
    Source/BatchBuilder.cpp
    Source/FrameRings.cpp
    Source/LumoraPipelines.cpp
    Source/RHIImageProvider.cpp
    Source/RenderThread.cpp
)
add_library(lumora::renderer ALIAS lumora_renderer)
lumora_defaults(lumora_renderer)
target_include_directories(lumora_renderer PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include> PRIVATE Source)
target_link_libraries(lumora_renderer
    PUBLIC  lumora::lumora lumora::text lumora_rhi
    PRIVATE lumora::platform lumora::log stb)

# Web: hand-written WGSL (Shaders/<name>.wgsl, VSMain/PSMain) embedded as-is; no dxc.
if (EMSCRIPTEN)
    set(_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
    file(GLOB _wgsl CONFIGURE_DEPENDS "${_shader_dir}/*.wgsl")
    set(_embed_header "${CMAKE_CURRENT_BINARY_DIR}/embedded/EmbeddedShaders.h")
    set(_embed_source "${CMAKE_CURRENT_BINARY_DIR}/embedded/EmbeddedShaders.cpp")
    add_custom_command(
        OUTPUT "${_embed_header}" "${_embed_source}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/embedded"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_DIR=${_shader_dir}
            -DPATTERN=*.wgsl
            -DOUTPUT_HEADER=${_embed_header}
            -DOUTPUT_SOURCE=${_embed_source}
            -P "${LUMORA_ROOT_DIR}/cmake/EmbedFiles.cmake"
        DEPENDS ${_wgsl} "${LUMORA_ROOT_DIR}/cmake/EmbedFiles.cmake"
        COMMENT "Embedding WGSL shaders into C++"
        VERBATIM)
    add_library(lumora_shaders_embedded STATIC "${_embed_header}" "${_embed_source}")
    add_library(lumora::shaders::embedded ALIAS lumora_shaders_embedded)
    target_include_directories(lumora_shaders_embedded PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/embedded>")
    lumora_defaults(lumora_shaders_embedded)
    return()
endif ()

# HLSL -> SPIR-V. The painter loads <name>.{vert,frag}.spv from the directory it is handed;
# stage $<TARGET_PROPERTY:lumora_shaders,SPIRV_DIR> there.
find_program(LUMORA_DXC NAMES dxc HINTS "$ENV{VULKAN_SDK}/Bin")
if (LUMORA_DXC)
    set(_spirv_dir "${CMAKE_CURRENT_BINARY_DIR}/spirv")
    set(_spirv "")
    foreach (_shader IN ITEMS LumoraRect LumoraTextured LumoraTris LumoraSDF LumoraMask)
        set(_src "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/${_shader}.hlsl")
        foreach (_stage IN ITEMS vert frag)
            if (_stage STREQUAL "vert")
                set(_profile vs_6_0)
                set(_entry VSMain)
            else ()
                set(_profile ps_6_0)
                set(_entry PSMain)
            endif ()
            set(_out "${_spirv_dir}/${_shader}.${_stage}.spv")
            add_custom_command(
                OUTPUT "${_out}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_spirv_dir}"
                COMMAND ${LUMORA_DXC} -spirv -T ${_profile} -E ${_entry} "${_src}" -Fo "${_out}"
                DEPENDS "${_src}"
                COMMENT "dxc ${_shader}.${_stage}.spv"
                VERBATIM)
            list(APPEND _spirv "${_out}")
        endforeach ()
    endforeach ()
    add_custom_target(lumora_shaders DEPENDS ${_spirv})
    set_target_properties(lumora_shaders PROPERTIES SPIRV_DIR "${_spirv_dir}")

    set(_embed_header "${CMAKE_CURRENT_BINARY_DIR}/embedded/EmbeddedShaders.h")
    set(_embed_source "${CMAKE_CURRENT_BINARY_DIR}/embedded/EmbeddedShaders.cpp")
    add_custom_command(
        OUTPUT "${_embed_header}" "${_embed_source}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/embedded"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_DIR=${_spirv_dir}
            -DOUTPUT_HEADER=${_embed_header}
            -DOUTPUT_SOURCE=${_embed_source}
            -P "${LUMORA_ROOT_DIR}/cmake/EmbedFiles.cmake"
        DEPENDS ${_spirv} "${LUMORA_ROOT_DIR}/cmake/EmbedFiles.cmake"
        COMMENT "Embedding SPIR-V shaders into C++"
        VERBATIM)

    add_library(lumora_shaders_embedded STATIC "${_embed_header}" "${_embed_source}")
    add_library(lumora::shaders::embedded ALIAS lumora_shaders_embedded)
    target_include_directories(lumora_shaders_embedded PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/embedded>")
    lumora_defaults(lumora_shaders_embedded)
else ()
    message(WARNING "dxc not found: lumora_shaders target disabled")
endif ()

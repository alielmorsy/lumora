add_library(lumora_sample_host STATIC Host/SampleHost.cpp)
target_include_directories(lumora_sample_host PUBLIC Host)
target_link_libraries(lumora_sample_host PUBLIC lumora::lumora lumora::text lumora::platform)
lumora_defaults(lumora_sample_host)

# Windowed path when the renderer and shaders are available; headless otherwise.
if (TARGET lumora_renderer AND TARGET lumora_shaders_embedded)
    target_link_libraries(lumora_sample_host PRIVATE lumora::renderer lumora::log lumora_shaders_embedded)
    target_compile_definitions(lumora_sample_host PRIVATE LUMORA_SAMPLE_GPU=1)
    add_dependencies(lumora_sample_host lumora_shaders_embedded)
else ()
    target_compile_definitions(lumora_sample_host PRIVATE LUMORA_SAMPLE_GPU=0)
endif ()

# Each sample: Samples/<Name>/CMakeLists.txt calling lumora_add_sample(<Name> <sources...>).
function(lumora_add_sample name)
    if (ANDROID)
        add_library(${name} SHARED ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Host/SampleEntry.cpp ${ARGN})
        target_link_options(${name} PRIVATE "-uJava_com_google_androidgamesdk_GameActivity_initializeNativeCode")
        target_link_libraries(${name} PRIVATE lumora_sample_host)
        lumora_defaults(${name})
    elseif (EMSCRIPTEN)
        # <name>.html/.js/.wasm; serve the build dir over http (python -m http.server) and open <name>.html.
        add_executable(${name} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Host/SampleEntry.cpp ${ARGN})
        set_target_properties(${name} PROPERTIES SUFFIX ".html")
        target_link_libraries(${name} PRIVATE lumora_sample_host)
        lumora_defaults(${name})
        set(_shell "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Host/web/shell.html")
        set(_font "${LUMORA_ROOT_DIR}/ThirdParty/harfbuzz/perf/fonts/Roboto-Regular.ttf")
        target_link_options(${name} PRIVATE
            "SHELL:--shell-file ${_shell}"
            -sALLOW_MEMORY_GROWTH=1
            -sEXIT_RUNTIME=0
            "SHELL:--preload-file ${_font}@/fonts/Roboto-Regular.ttf")
        set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "${_shell}" "${_font}")
        if (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets")
            target_link_options(${name} PRIVATE "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets")
        endif ()
    else ()
        add_executable(${name} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Host/SampleEntry.cpp ${ARGN})
        target_link_libraries(${name} PRIVATE lumora_sample_host)
        lumora_defaults(${name})
        if (LUMORA_BUILD_TESTS)
            add_test(NAME ${name}.smoke COMMAND ${name} --headless-frames 3)
        endif ()
    endif ()
endfunction()

file(GLOB _samples LIST_DIRECTORIES true "${CMAKE_CURRENT_LIST_DIR}/*")
foreach (_dir ${_samples})
    if (IS_DIRECTORY ${_dir} AND EXISTS "${_dir}/CMakeLists.txt")
        add_subdirectory(${_dir})
    endif ()
endforeach ()

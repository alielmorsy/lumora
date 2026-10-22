include_guard(GLOBAL)

# C++20 plus warning flags.
function(lumora_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
    if (ANDROID)
        # NDK 28 libc++ keeps std::stop_token / jthread behind the experimental library.
        target_compile_options(${target} PUBLIC -fexperimental-library)
        target_link_options(${target} PUBLIC -fexperimental-library)
    endif ()
endfunction()

# Copies the target's runtime DLLs next to it.
function(lumora_copy_runtime_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>,copy_if_different,true>
            $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
        COMMAND_EXPAND_LISTS)
endfunction()

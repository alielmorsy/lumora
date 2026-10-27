set(_common Source/Common/CursorLock.cpp Source/OS/Common/FileSystem.cpp)

# Desktop Linux needs the X11 and Wayland development packages; without them the build stays headless (see the final else).
set(_linux_desktop FALSE)
set(LUMORA_PLATFORM_LINUX_DESKTOP FALSE CACHE INTERNAL "Linux window backends (X11 and Wayland) are built")
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    find_program(_wayland_scanner wayland-scanner)
    if (PkgConfig_FOUND AND _wayland_scanner)
        pkg_check_modules(_x11 IMPORTED_TARGET x11 x11-xcb xcb xkbcommon xkbcommon-x11 xi xcursor xfixes xrandr)
        pkg_check_modules(_wl IMPORTED_TARGET wayland-client wayland-cursor)
        pkg_check_modules(_decor IMPORTED_TARGET libdecor-0)
        if (_x11_FOUND AND _wl_FOUND)
            set(_linux_desktop TRUE)
            set(LUMORA_PLATFORM_LINUX_DESKTOP TRUE CACHE INTERNAL "Linux window backends (X11 and Wayland) are built")
        else ()
            message(STATUS "lumora_platform: X11/Wayland development packages missing, Linux build stays headless")
        endif ()
    endif ()
endif ()

if (WIN32)
    set(_windows
            Source/Windows/WindowsWindow.cpp
            Source/Windows/WindowsSystemMetrics.cpp
            Source/Windows/WindowsUserPaths.cpp
            Source/Windows/WindowsFileDialog.cpp
            Source/Windows/WindowsClipboard.cpp
            Source/Windows/WindowsProgressDialog.cpp
            Source/Windows/XInputGamepads.cpp)
    set(_os
            Source/OS/Windows/Thread.cpp
            Source/OS/Windows/Process.cpp
            Source/OS/Windows/DynamicLibrary.cpp
            Source/OS/Windows/Environment.cpp
            Source/OS/Windows/CrashHandler.cpp
            Source/OS/Windows/DirectoryWatcher.cpp
            Source/OS/Windows/Shell.cpp
            Source/OS/Common/Asset.cpp)
elseif (ANDROID)
    set(_android
            Source/Android/AndroidSystemMetrics.cpp
            Source/Android/AndroidUserPaths.cpp
            Source/Android/AndroidFileDialog.cpp
            Source/Android/AndroidClipboard.cpp
            Source/Android/AndroidProgressDialog.cpp)
    set(_os
            Source/OS/Posix/Thread.cpp
            Source/OS/Posix/Process.cpp
            Source/OS/Posix/DynamicLibrary.cpp
            Source/OS/Posix/Environment.cpp
            Source/OS/Posix/DirectoryWatcher.cpp
            Source/OS/Android/CrashHandler.cpp
            Source/OS/Android/Shell.cpp
            Source/OS/Android/Asset.cpp)
    find_package(game-activity CONFIG QUIET)
    if (game-activity_FOUND)
        message(STATUS "lumora_platform: game-activity prefab found, building window/entry")
        list(APPEND _android
                Source/Android/AndroidApp.cpp
                Source/Android/AndroidWindow.cpp)
    else ()
        message(STATUS "lumora_platform: standalone NDK build (game-activity not found), OS-only subset")
    endif ()
elseif (_linux_desktop)
    # X11 and Wayland are both compiled in; the backend is picked at runtime (see Linux/LinuxWindow.cpp).
    # Generated client glue for the protocols the compositor does not ship in libwayland-client.
    set(_proto_dir ${CMAKE_CURRENT_BINARY_DIR}/protocols)
    file(MAKE_DIRECTORY ${_proto_dir})
    set(_proto_sources)
    foreach (_proto xdg-shell xdg-decoration-unstable-v1 pointer-constraints-unstable-v1 relative-pointer-unstable-v1)
        add_custom_command(
                OUTPUT  ${_proto_dir}/${_proto}-client-protocol.h ${_proto_dir}/${_proto}-protocol.c
                COMMAND ${_wayland_scanner} client-header ${CMAKE_CURRENT_SOURCE_DIR}/Protocols/${_proto}.xml ${_proto_dir}/${_proto}-client-protocol.h
                COMMAND ${_wayland_scanner} private-code  ${CMAKE_CURRENT_SOURCE_DIR}/Protocols/${_proto}.xml ${_proto_dir}/${_proto}-protocol.c
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Protocols/${_proto}.xml
                VERBATIM)
        list(APPEND _proto_sources ${_proto_dir}/${_proto}-protocol.c)
    endforeach ()

    set(_linux
            Source/Linux/LinuxWindow.cpp
            Source/Linux/Keyboard.cpp
            Source/Linux/X11Backend.cpp
            Source/Linux/WaylandBackend.cpp
            Source/Linux/LinuxClipboard.cpp
            Source/Linux/LinuxFileDialog.cpp
            Source/Linux/LinuxProgressDialog.cpp
            Source/Linux/LinuxSystemMetrics.cpp
            Source/Linux/LinuxUserPaths.cpp
            Source/Linux/LinuxGamepads.cpp
            ${_proto_sources})
    set(_os
            Source/OS/Posix/Thread.cpp
            Source/OS/Posix/Process.cpp
            Source/OS/Posix/DynamicLibrary.cpp
            Source/OS/Posix/Environment.cpp
            Source/OS/Posix/DirectoryWatcher.cpp
            Source/OS/Linux/CrashHandler.cpp
            Source/OS/Linux/Shell.cpp
            Source/OS/Common/Asset.cpp)
elseif (EMSCRIPTEN)
    file(GLOB _web CONFIGURE_DEPENDS Source/Web/*.cpp Source/OS/Web/*.cpp)
    set(_os
            Source/OS/Posix/Thread.cpp
            Source/OS/Posix/Process.cpp
            Source/OS/Posix/DynamicLibrary.cpp
            Source/OS/Common/Asset.cpp)
else ()
    message(STATUS "lumora_platform: no backend for ${CMAKE_SYSTEM_NAME}, headless (common sources only)")
    set(_os Source/OS/Posix/Thread.cpp)
endif ()

add_library(lumora_platform STATIC ${_common} ${_os} ${_windows} ${_android} ${_linux} ${_web})
add_library(lumora::platform ALIAS lumora_platform)
lumora_defaults(lumora_platform)

target_include_directories(lumora_platform
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        PRIVATE Source)
target_link_libraries(lumora_platform PUBLIC lumora::base PRIVATE lumora::log)

if (WIN32)
    target_compile_definitions(lumora_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_source_files_properties(${_windows} PROPERTIES COMPILE_DEFINITIONS "UNICODE;_UNICODE")
    target_link_libraries(lumora_platform PRIVATE user32 gdi32 psapi ole32 shell32 advapi32 XInput dbghelp)
elseif (ANDROID)
    target_link_libraries(lumora_platform PRIVATE android log dl)
    if (game-activity_FOUND)
        target_link_libraries(lumora_platform PUBLIC game-activity::game-activity_static)
    endif ()
endif ()
if (_linux_desktop)
    target_include_directories(lumora_platform PRIVATE ${_proto_dir} Source/Linux)
    target_link_libraries(lumora_platform PRIVATE PkgConfig::_x11 PkgConfig::_wl dl pthread)
    if (_decor_FOUND)
        target_compile_definitions(lumora_platform PRIVATE LUMORA_HAVE_LIBDECOR=1)
        target_link_libraries(lumora_platform PRIVATE PkgConfig::_decor)
    endif ()
endif ()

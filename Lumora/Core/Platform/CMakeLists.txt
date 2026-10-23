set(_common Source/Common/CursorLock.cpp Source/OS/Common/FileSystem.cpp)

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
elseif (EMSCRIPTEN)
    file(GLOB _web CONFIGURE_DEPENDS Source/Web/*.cpp Source/OS/Web/*.cpp)
    set(_os Source/OS/Posix/Thread.cpp)
else ()
    message(STATUS "lumora_platform: no backend for ${CMAKE_SYSTEM_NAME}, headless (common sources only)")
    set(_os Source/OS/Posix/Thread.cpp)
endif ()

add_library(lumora_platform STATIC ${_common} ${_os} ${_windows} ${_android} ${_web})
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

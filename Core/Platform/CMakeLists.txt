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
            Source/OS/Windows/Process.cpp
            Source/OS/Windows/DynamicLibrary.cpp
            Source/OS/Windows/Environment.cpp
            Source/OS/Windows/CrashHandler.cpp
            Source/OS/Windows/DirectoryWatcher.cpp
            Source/OS/Windows/Shell.cpp)
else ()
    message(FATAL_ERROR "lumora_platform: unsupported platform")
endif ()

add_library(lumora_platform STATIC ${_common} ${_os} ${_windows})
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
endif ()

if (LUMORA_BUILD_TESTS)
    add_subdirectory(Tests)
endif ()

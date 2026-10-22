#pragma once

// Include this header in EXACTLY ONE translation unit of the application. It
// expands to the real OS entry point for the target platform and forwards into
// lumora::platform::Main (declared in Launch.h), which the application defines.

#include "Launch.h"

#if defined(_WIN32)

    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0A00
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef UNICODE
        #define UNICODE
    #endif

    #include <windows.h>

    #include <cstdlib>   // __argc / __wargv
    #include <string>
    #include <vector>

namespace lumora::platform::detail
{
    /// UTF-8 copies of the wide command line; storage outlives Main. `__argv` is not populated for a wWinMain entry point, so
    /// passing it would hand Main a null array.
    struct NarrowArgs
    {
        std::vector<std::string> storage;
        std::vector<char*>       argv;

        NarrowArgs()
        {
            storage.reserve(static_cast<std::size_t>(__argc > 0 ? __argc : 0));

            for (int i = 0; i < __argc && __wargv != nullptr; ++i)
            {
                const wchar_t* wide = __wargv[i];
                if (wide == nullptr) continue;

                const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
                std::string narrow(bytes > 0 ? static_cast<std::size_t>(bytes - 1) : 0u, '\0');
                if (bytes > 1)
                    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow.data(), bytes, nullptr, nullptr);
                storage.push_back(std::move(narrow));
            }

            argv.reserve(storage.size() + 1);
            for (std::string& arg : storage) argv.push_back(arg.data());
            argv.push_back(nullptr);   // argv[argc] is null, as a C program expects
        }

        [[nodiscard]] int Count() const noexcept { return static_cast<int>(storage.size()); }
    };
}

/// Windows entry point (wWinMain); forwards to lumora::platform::Main.
inline int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                           PWSTR /*lpCmdLine*/, int /*nShowCmd*/)
{
    static lumora::platform::detail::NarrowArgs args;
    return lumora::platform::Main(lumora::platform::LaunchArgs{ args.Count(), args.argv.data() });
}

#elif defined(__ANDROID__)

    #include "Launch.h"
    #include <lumora/platform/android/AndroidApp.h>
    #include <game-activity/GameActivity.h>
    #include <game-activity/native_app_glue/android_native_app_glue.h>

    extern "C" void android_main(struct android_app* app)
    {
        lumora::platform::android::SetApp(app);
        if (app && app->activity)
        {
            lumora::platform::android::SetAssetManager(app->activity->assetManager);
        }

        lumora::platform::LaunchArgs args{0, nullptr};
        lumora::platform::Main(args);

        if (app && app->activity)
        {
            GameActivity_finish(app->activity);
        }
    }

#else

/// Standard entry point (main); forwards to lumora::platform::Main.
int main(int argc, char** argv)
{
    return lumora::platform::Main(lumora::platform::LaunchArgs{ argc, argv });
}

#endif

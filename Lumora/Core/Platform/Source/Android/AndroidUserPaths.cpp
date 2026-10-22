#include <lumora/platform/UserPaths.h>
#include <lumora/platform/android/AndroidApp.h>

#if __has_include(<game-activity/GameActivity.h>)
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#endif

namespace lumora::platform
{
    std::filesystem::path GetUserConfigDir(std::string_view app)
    {
#if __has_include(<game-activity/GameActivity.h>)
        android_app* androidApp = android::App();
        if (androidApp && androidApp->activity && androidApp->activity->internalDataPath)
        {
            std::filesystem::path base(androidApp->activity->internalDataPath);
            if (!app.empty())
            {
                base /= app;
            }
            std::error_code ec;
            std::filesystem::create_directories(base, ec);
            return base;
        }
#else
        (void)app;
#endif
        return {};
    }
}

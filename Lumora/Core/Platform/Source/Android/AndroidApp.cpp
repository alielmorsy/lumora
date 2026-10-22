#include <lumora/platform/android/AndroidApp.h>

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <atomic>

namespace
{
    std::atomic<android_app*> g_app{nullptr};
}

namespace lumora::platform::android
{
    void SetApp(android_app* app) noexcept
    {
        g_app.store(app, std::memory_order_release);
    }

    android_app* App() noexcept
    {
        return g_app.load(std::memory_order_acquire);
    }

    JNIEnv* Env() noexcept
    {
        android_app* app = App();
        if (!app || !app->activity || !app->activity->vm)
        {
            return nullptr;
        }

        JavaVM* vm = app->activity->vm;
        JNIEnv* env = nullptr;
        const jint res = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (res == JNI_OK)
        {
            return env;
        }

        if (res == JNI_EDETACHED)
        {
            JavaVMAttachArgs args{};
            args.version = JNI_VERSION_1_6;
            args.name = "LumoraThread";
            args.group = nullptr;
            if (vm->AttachCurrentThread(&env, &args) == JNI_OK)
            {
                thread_local struct ThreadDetacher
                {
                    JavaVM* jvm = nullptr;
                    ~ThreadDetacher()
                    {
                        if (jvm) jvm->DetachCurrentThread();
                    }
                } detacher{vm};
                return env;
            }
        }

        return nullptr;
    }
}

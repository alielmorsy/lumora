#pragma once

#include <lumora/Api.h>

#include <jni.h>

struct android_app;
struct AAssetManager;

namespace lumora::platform::android
{
    /// Returns the global android_app instance passed to android_main.
    [[nodiscard]] LUMORA_API android_app* App() noexcept;

    /// Returns a JNIEnv attached to the current thread, caching it in thread-local storage.
    [[nodiscard]] LUMORA_API JNIEnv* Env() noexcept;

    /// Sets the global android_app instance.
    void SetApp(android_app* app) noexcept;

    /// Sets the global AAssetManager pointer for asset reading.
    void SetAssetManager(AAssetManager* manager) noexcept;

    /// Returns the global AAssetManager pointer.
    [[nodiscard]] AAssetManager* GetAssetManager() noexcept;
}

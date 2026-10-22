#pragma once

struct AAssetManager;

namespace lumora::platform::android
{
    void SetAssetManager(AAssetManager* manager) noexcept;
    [[nodiscard]] AAssetManager* GetAssetManager() noexcept;
}

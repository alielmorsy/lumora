#include <lumora/os/Asset.h>
#include <lumora/platform/android/AndroidApp.h>

#include <android/asset_manager.h>

#include <atomic>
#include <string>

namespace
{
    std::atomic<AAssetManager*> g_assetManager{nullptr};
}

namespace lumora::platform::android
{
    void SetAssetManager(AAssetManager* manager) noexcept
    {
        g_assetManager.store(manager, std::memory_order_release);
    }

    AAssetManager* GetAssetManager() noexcept
    {
        return g_assetManager.load(std::memory_order_acquire);
    }
}

namespace lumora::os
{
    std::optional<std::vector<std::byte>> ReadAsset(std::string_view path)
    {
        AAssetManager* mgr = lumora::platform::android::GetAssetManager();
        if (!mgr)
        {
            return std::nullopt;
        }

        while (!path.empty() && (path.front() == '/' || path.front() == '\\'))
        {
            path.remove_prefix(1);
        }

        std::string nullTerminated(path);
        AAsset* asset = AAssetManager_open(mgr, nullTerminated.c_str(), AASSET_MODE_BUFFER);
        if (!asset)
        {
            return std::nullopt;
        }

        const off64_t size = AAsset_getLength64(asset);
        if (size < 0)
        {
            AAsset_close(asset);
            return std::nullopt;
        }

        std::vector<std::byte> buffer(static_cast<std::size_t>(size));
        const int bytesRead = AAsset_read(asset, buffer.data(), buffer.size());
        AAsset_close(asset);

        if (bytesRead < 0 || static_cast<std::size_t>(bytesRead) != buffer.size())
        {
            return std::nullopt;
        }
        return buffer;
    }

    std::optional<std::string> ReadAssetText(std::string_view path)
    {
        const auto bytes = ReadAsset(path);
        if (!bytes)
        {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }
}

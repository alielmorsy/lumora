#include "RHIImageProvider.h"

#include <lumora/rhi/RHIPainter.h>

#include <lumora/log/Log.h>

#include <cstddef>
#include <span>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>
LUMORA_LOG_CATEGORY("RHIImageProvider")
namespace lumora {

RHIImageProvider::RHIImageProvider(RHIPainter* painter, AssetLoader loader) noexcept
    : m_painter(painter), m_loader(loader ? loader : &DefaultFileLoad) {}

ImageHandle RHIImageProvider::Acquire(std::string_view src) {
    if (src.empty() || !m_painter) {
        return {};
    }
    const std::string key{src};
    if (auto it = m_cache.find(key); it != m_cache.end()) {
        ++it->second.refs;
        return it->second.handle;
    }

    std::string bytes;
    if (!m_loader(src, bytes)) {
        LOG_ERROR("Image: failed to read '{}'", key);
        return {};
    }

    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                            static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
        LOG_ERROR("Image: failed to decode '{}': {}", key, stbi_failure_reason());
        return {};
    }

    const std::span<const std::byte> span{reinterpret_cast<const std::byte*>(pixels),
                                          static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u};
    const ImageHandle handle = m_painter->UploadTexture(span, static_cast<std::uint32_t>(w),
                                                        static_cast<std::uint32_t>(h), AtlasFormat::Rgba8888);
    stbi_image_free(pixels);

    if (!handle.Valid()) {
        LOG_ERROR("Image: failed to upload '{}'", key);
        return {};
    }
    m_cache.emplace(key, Entry{handle, 1});
    return handle;
}

void RHIImageProvider::Release(std::string_view src) {
    if (src.empty()) {
        return;
    }
    const auto it = m_cache.find(std::string{src});
    if (it == m_cache.end()) {
        return;
    }
    if (--it->second.refs == 0) {
        m_painter->FreeTexture(it->second.handle);
        m_cache.erase(it);
    }
}

} // namespace lumora

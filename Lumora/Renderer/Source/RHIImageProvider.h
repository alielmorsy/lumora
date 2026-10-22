#pragma once

#include <lumora/AssetLoader.h>
#include <lumora/Paint/ImageProvider.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lumora {

class RHIPainter;

/// The GPU-backed ImageProvider: reads a file through the AssetLoader, decodes it with stb_image to
/// RGBA8, uploads it through the RHIPainter, and caches the resulting handle refcounted by path.
class RHIImageProvider final : public ImageProvider {
public:
    RHIImageProvider(RHIPainter* painter, AssetLoader loader) noexcept;

    [[nodiscard]] ImageHandle Acquire(std::string_view src) override;
    void                      Release(std::string_view src) override;

private:
    struct Entry {
        ImageHandle   handle{};
        std::uint32_t refs = 0;
    };

    RHIPainter*                            m_painter;
    AssetLoader                            m_loader;
    std::unordered_map<std::string, Entry> m_cache;
};

} // namespace lumora

#pragma once

#include <lumora/Api.h>
#include <lumora/Text/TextMeasurer.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace lumora {

/// Identifies a face registered in a FontSystem (a family + weight/style slot). 0 = invalid.
enum class FaceId : std::uint32_t { Invalid = 0 };

/// Describes a face being registered: the family it joins and its weight/style slot.
struct FontSpec
{
    std::string family;
    bool bold = false;
    bool italic = false;
};

/// Headless text engine: shapes runs (HarfBuzz), rasterises glyphs (FreeType) into two CPU atlas planes (R8 coverage + RGBA8
/// colour), and lays text out into positioned quads. Implements TextMeasurer.
/// Full Unicode: SheenBidi levels, UAX#14 line breaking, per-run shaping with a fallback chain, UBA reorder, ellipsis, colour
/// emoji. FreeType/HarfBuzz stay behind the pimpl.
class LUMORA_API FontSystem final : public TextMeasurer {
public:
    FontSystem();
    ~FontSystem() override;

    FontSystem(const FontSystem&)            = delete;
    FontSystem& operator=(const FontSystem&) = delete;

    /// Register a face from an in-memory TTF/OTF (bytes are copied). The first face becomes the default family if none is
    /// set. Returns FaceId::Invalid on failure.
    FaceId RegisterFont(std::span<const std::byte> fontFile, const FontSpec& spec);

    /// Append `face` to the ordered fallback chain consulted when the resolved face lacks a glyph.
    void AddFallback(FaceId face);

    /// The family used when a FontProps names an empty / unregistered family.
    void SetDefaultFamily(std::string_view family);

    [[nodiscard]] Vec2 Measure(std::string_view text, const FontProps& font, float size,
                               const TextFlowProps& flow, AvailableSpace width) const override;

    void BuildGlyphCache(std::string_view text, const FontProps& font, float size,
                         const TextFlowProps& flow, AvailableSpace width,
                         std::vector<CachedGlyph>& coverage,
                         std::vector<CachedGlyph>& color) const override;

    [[nodiscard]] AtlasView Atlas(AtlasKind kind) const override;
    [[nodiscard]] Vec2 AtlasSize(AtlasKind kind) const override;

    void SetContentScale(float scale) override;
    [[nodiscard]] float ContentScale() const noexcept override;
    [[nodiscard]] std::uint64_t Generation() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace lumora

#pragma once

#include <lumora/Text/TextMeasurer.h>

#include <cstddef>
#include <string_view>
#include <vector>

namespace lumora
{
    /// A measurer that lays every byte out as one same-width coverage quad and never emits colour glyphs: drives layout and
    /// emit without a real font system, and keeps layout benchmarks from measuring HarfBuzz.
    struct FakeFonts final : TextMeasurer
    {
        [[nodiscard]] Vec2 Measure(std::string_view text, const FontProps&, float size,
                                   const TextFlowProps&, AvailableSpace) const override
        {
            return {static_cast<float>(text.size()) * size * 0.5f, size};
        }

        void BuildGlyphCache(std::string_view text, const FontProps&, float size, const TextFlowProps&,
                             AvailableSpace, std::vector<CachedGlyph>& coverage,
                             std::vector<CachedGlyph>& color) const override
        {
            coverage.clear();
            color.clear();
            const float w = size * 0.5f;
            float x = 0.f; // pen-relative; the emit pass re-adds the pen
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                coverage.push_back(CachedGlyph{Rect{x, 0.f, w, size}, 0, 0, 1, 1});
                x += w;
            }
        }

        [[nodiscard]] AtlasView Atlas(AtlasKind kind) const override
        {
            AtlasView v;
            v.width = 1; // non-zero so emit-time UV normalisation stays finite
            v.height = 1;
            v.version = kind == AtlasKind::Coverage ? 7 : 0;
            return v;
        }

        [[nodiscard]] Vec2 AtlasSize(AtlasKind) const override { return {1.f, 1.f}; }
    };
} // namespace lumora

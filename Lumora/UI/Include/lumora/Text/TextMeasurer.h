#pragma once

#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Text/Glyphs.h>
#include <lumora/Widgets/Props.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lumora
{
    /// The text seam: measurement for layout and glyph building for emit, injected through AppCtx. A null measurer is the
    /// headless contract (measures 0x0, emits no glyphs). Abstract and GPU-free.
    class TextMeasurer
    {
    public:
        virtual ~TextMeasurer() = default;

        /// Natural size of `text` in `font` at `size` under `flow` into the available width (Definite wraps; MaxContent is one
        /// line). Uses the same `flow` as emit so measure and paint agree.
        [[nodiscard]] virtual Vec2 Measure(std::string_view text, const FontProps& font, float size,
                                           const TextFlowProps& flow, AvailableSpace width) const = 0;

        /// Lay out + rasterise `text` into pen-relative cached glyphs split by plane (coverage tints by run colour, colour draws
        /// as-is). Dest rects use origin 0 and source rects are atlas pixel coords, so the caller can cache across frames. `width`
        /// is the wrap constraint the bridge measured against (hits the layout cache). Output vectors are cleared and filled.
        virtual void BuildGlyphCache(std::string_view text, const FontProps& font, float size,
                                     const TextFlowProps& flow, AvailableSpace width,
                                     std::vector<CachedGlyph>& coverage,
                                     std::vector<CachedGlyph>& color) const = 0;

        /// The CPU atlas plane for `kind`, mirrored to the GPU by the painter. Taking the view CONSUMES its dirty row span, so only
        /// the painter's sync may call it: another caller steals rows and those glyphs stay blank until a full upload. Use AtlasSize() for the size.
        [[nodiscard]] virtual AtlasView Atlas(AtlasKind kind) const = 0;

        /// The plane's pixel size without touching dirty bookkeeping (emit normalises cached rects against it every run). Zero when
        /// the plane does not exist.
        [[nodiscard]] virtual Vec2 AtlasSize(AtlasKind) const { return {}; }

        /// Set the logical to physical pixel ratio: glyphs rasterise at physical size while Measure() and emitted rects stay
        /// logical. Bumps Generation() so cached runs rebuild. Default no-op.
        virtual void SetContentScale(float) {}

        /// Current logical to physical pixel ratio (1 = none); emit snaps coverage glyphs to whole physical pixels.
        [[nodiscard]] virtual float ContentScale() const noexcept { return 1.f; }

        /// Bumped when cached glyph pixel rects are invalidated (e.g. a scale change); a node caching runs rebuilds on change.
        [[nodiscard]] virtual std::uint64_t Generation() const noexcept { return 0; }

        /// Caret hit-testing for editable text. Defaults are correct for single-line LTR (derived from Measure() of prefixes); a
        /// measurer may override for multi-line/bidi.
        struct CaretHit
        {
            std::size_t byteOffset = 0;
            float       x          = 0.f;
            int         line       = 0;
            float       lineTop    = 0.f;
            float       lineHeight = 0.f;
        };

        /// The caret position for a byte offset into `text`: x is the advance width of the prefix.
        [[nodiscard]] virtual CaretHit CaretAtOffset(std::string_view text, const FontProps& font,
                                                     float size, const TextFlowProps& flow,
                                                     AvailableSpace width, std::size_t byteOffset) const
        {
            const std::size_t clamped = std::min(byteOffset, text.size());
            const float        x      = Measure(text.substr(0, clamped), font, size, flow, width).x;
            const float        h      = Measure(text.empty() ? std::string_view{"Mg"} : text, font, size, flow, width).y;
            return CaretHit{clamped, x, 0, 0.f, h};
        }

        /// Byte offset whose caret x is nearest `boxRel.x` (pen-relative); scans code-point boundaries.
        [[nodiscard]] virtual CaretHit CaretAtPoint(std::string_view text, const FontProps& font,
                                                    float size, const TextFlowProps& flow,
                                                    AvailableSpace width, Vec2 boxRel) const
        {
            const float h = Measure(text.empty() ? std::string_view{"Mg"} : text, font, size, flow, width).y;
            std::size_t bestOffset = 0;
            float       bestX      = 0.f;
            float       bestDist   = std::abs(boxRel.x);
            std::size_t i          = 0;
            while (i < text.size())
            {
                const auto        c   = static_cast<unsigned char>(text[i]);
                const std::size_t adv = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
                i                     = std::min(text.size(), i + adv);
                const float x         = Measure(text.substr(0, i), font, size, flow, width).x;
                const float dist      = std::abs(x - boxRel.x);
                if (dist < bestDist)
                {
                    bestDist   = dist;
                    bestOffset = i;
                    bestX      = x;
                }
                if (x > boxRel.x)
                {
                    break;
                }
            }
            return CaretHit{bestOffset, bestX, 0, 0.f, h};
        }

        /// Append the rectangles covering bytes [begin, end) of `text` (one for single-line LTR).
        virtual void SelectionRects(std::string_view text, const FontProps& font, float size,
                                    const TextFlowProps& flow, AvailableSpace width, std::size_t begin,
                                    std::size_t end, std::vector<Rect>& out) const
        {
            out.clear();
            if (begin >= end)
            {
                return;
            }
            const CaretHit a = CaretAtOffset(text, font, size, flow, width, begin);
            const CaretHit b = CaretAtOffset(text, font, size, flow, width, end);
            out.push_back(Rect{a.x, a.lineTop, b.x - a.x, a.lineHeight});
        }
    };
}

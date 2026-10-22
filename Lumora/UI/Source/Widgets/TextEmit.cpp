#include "Widgets/TextEmit.h"

#include "Node/LiveNode.h"
#include "Node/Payloads.h"
#include "Runtime/Paint.h"

#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Text/Glyphs.h>
#include <lumora/Text/TextMeasurer.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace lumora
{
    namespace
    {
        /// Draw a plane's pen-relative cached glyphs normalised to the live atlas size, offset by `pen`. Reuses the
        /// context's GlyphQuad scratch (no steady-state allocation); a no-op for an empty plane or atlas.
        void EmitCachedGlyphs(Canvas& canvas, PaintContext& ctx, Vec2 pen,
                              const std::vector<CachedGlyph>& glyphs, AtlasKind kind, Color tint)
        {
            if (glyphs.empty())
            {
                return;
            }

            const Vec2 atlas = ctx.fonts->AtlasSize(kind);
            if (atlas.x <= 0.f || atlas.y <= 0.f)
            {
                return;
            }
            const float rcpW = 1.f / atlas.x;
            const float rcpH = 1.f / atlas.y;
            std::vector<GlyphQuad>& out = (kind == AtlasKind::Coverage) ? ctx.coverageScratch : ctx.colorScratch;
            // Coverage glyphs sample a linear-filtered mask: snap the final origin to whole PHYSICAL pixels (round at
            // content scale) for crisp edges. Colour strikes stay subpixel.
            const bool snap = (kind == AtlasKind::Coverage);
            const float scale = ctx.fonts->ContentScale();
            out.clear();
            out.reserve(glyphs.size());
            for (const CachedGlyph& g : glyphs)
            {
                float x = g.local.x + pen.x;
                float y = g.local.y + pen.y;
                if (snap)
                {
                    x = std::round(x * scale) / scale;
                    y = std::round(y * scale) / scale;
                }
                out.push_back(
                    GlyphQuad{
                        Rect{x, y, g.local.width, g.local.height},
                        Rect{
                            static_cast<float>(g.sx) * rcpW, static_cast<float>(g.sy) * rcpH,
                            static_cast<float>(g.sw) * rcpW, static_cast<float>(g.sh) * rcpH
                        }
                    });
            }
            canvas.DrawGlyphs(out, kind, tint);
        }

        /// Shared front half of the text emit (aligned pen origin + cached or scratch glyph planes), so EmitText
        /// and CollectTextGlyphRects agree to the pixel. `coverage` is null for a font-less context or empty text.
        struct ResolvedRun
        {
            Vec2 pen{};
            const std::vector<CachedGlyph>* coverage = nullptr;
            const std::vector<CachedGlyph>* color = nullptr;
        };

        [[nodiscard]] ResolvedRun ResolveRun(PaintContext& ctx, const Rect& rect, const Spacing& pad,
                                             std::string_view text, const FontProps& font, float size,
                                             const TextFlowProps& flow, TextPayload* cache, Align hAlign,
                                             Align vAlign, bool wrapToBox)
        {
            if (!ctx.fonts || text.empty())
            {
                return {};
            }
            const float maxWidth = std::max(0.f, rect.width - pad.Horizontal());
            // The bridge sized this node from a single-line MaxContent measure: shape against the same constraint,
            // wrapping into the box only with a real width constraint (wrapToBox), or measure and paint disagree.
            const AvailableSpace shapeWidth = wrapToBox
                                                  ? AvailableSpace::Definite(maxWidth)
                                                  : AvailableSpace::MaxContent();
            Vec2 pen{rect.x + pad.left, rect.y + pad.top};

            // Horizontal alignment offsets the pen by the slack against the run's advance width; vertical alignment is
            // resolved later against the real ink box. Start keeps the top-left pen; only non-Start callers pay the measure.
            if (hAlign != Align::Start)
            {
                const float runW = ctx.MeasureText(text, font, size, flow, shapeWidth).x;
                pen.x += (hAlign == Align::Center) ? (maxWidth - runW) * 0.5f : maxWidth - runW;
            }

            const std::vector<CachedGlyph>* cov = nullptr;
            const std::vector<CachedGlyph>* col = nullptr;
            if (cache)
            {
                if (!(cache->glyphsValid && cache->glyphsEpoch == cache->contentEpoch &&
                    cache->glyphsWidth == shapeWidth && cache->glyphsGen == ctx.fonts->Generation()))
                {
                    ctx.BuildGlyphs(text, font, size, flow, shapeWidth, cache->coverageGlyphs,
                                    cache->colorGlyphs);
                    cache->glyphsValid = true;
                    cache->glyphsEpoch = cache->contentEpoch;
                    cache->glyphsWidth = shapeWidth;
                    cache->glyphsGen = ctx.fonts->Generation();
                }
                cov = &cache->coverageGlyphs;
                col = &cache->colorGlyphs;
            }
            else
            {
                ctx.BuildGlyphs(text, font, size, flow, shapeWidth, ctx.coverageCacheScratch,
                                ctx.colorCacheScratch);
                cov = &ctx.coverageCacheScratch;
                col = &ctx.colorCacheScratch;
            }

            // Vertical centre/end alignment uses the run's real ink box, not the font line box: ascent reserves blank
            // space that makes short or lowercase labels sit visibly low.
            if (vAlign != Align::Start && (!cov->empty() || !col->empty()))
            {
                float inkTop = std::numeric_limits<float>::max();
                float inkBottom = std::numeric_limits<float>::lowest();
                for (const std::vector<CachedGlyph>* plane : {cov, col})
                {
                    for (const CachedGlyph& g : *plane)
                    {
                        inkTop = std::min(inkTop, g.local.y);
                        inkBottom = std::max(inkBottom, g.local.y + g.local.height);
                    }
                }
                const float boxH = rect.height - pad.Vertical();
                const float inkH = inkBottom - inkTop;
                pen.y += (vAlign == Align::Center ? (boxH - inkH) * 0.5f : boxH - inkH) - inkTop;
            }

            return {pen, cov, col};
        }
    } // namespace

    TextRunGeometry TextRunGeometryOf(const LiveNode& node, const TextPayload& text)
    {
        const Spacing& pad = node.style.padding;
        const float maxWidth = std::max(0.f, node.solved.width - pad.Horizontal());
        return {Vec2{node.solved.x + pad.left, node.solved.y + pad.top},
                (text.flow.wrap && node.wrapInBox) ? AvailableSpace::Definite(maxWidth)
                                                   : AvailableSpace::MaxContent()};
    }

    void EmitText(Canvas& canvas, PaintContext& ctx, const Rect& rect, const Spacing& pad,
                  std::string_view text, const FontProps& font, float size, const TextFlowProps& flow,
                  Color tint, TextPayload* cache, Align hAlign, Align vAlign, bool wrapToBox)
    {
        const ResolvedRun run = ResolveRun(ctx, rect, pad, text, font, size, flow, cache, hAlign, vAlign,
                                           wrapToBox);
        if (!run.coverage)
        {
            return;
        }
        EmitCachedGlyphs(canvas, ctx, run.pen, *run.coverage, AtlasKind::Coverage, tint);
        EmitCachedGlyphs(canvas, ctx, run.pen, *run.color, AtlasKind::Color, tint);
    }

    void CollectTextGlyphRects(PaintContext& ctx, const Rect& rect, const Spacing& pad,
                               std::string_view text, const FontProps& font, float size,
                               const TextFlowProps& flow, TextPayload* cache, Align hAlign, Align vAlign,
                               bool wrapToBox, std::vector<Rect>& out)
    {
        const ResolvedRun run = ResolveRun(ctx, rect, pad, text, font, size, flow, cache, hAlign, vAlign,
                                           wrapToBox);
        if (!run.coverage)
        {
            return;
        }
        // Coverage glyphs draw pixel-snapped (EmitCachedGlyphs), colour strikes subpixel: mirror that so borders fit.
        const float scale = ctx.fonts->ContentScale();
        const auto append = [&](const std::vector<CachedGlyph>& glyphs, bool snap)
        {
            for (const CachedGlyph& g : glyphs)
            {
                float x = g.local.x + run.pen.x;
                float y = g.local.y + run.pen.y;
                if (snap)
                {
                    x = std::round(x * scale) / scale;
                    y = std::round(y * scale) / scale;
                }
                out.push_back(Rect{x, y, g.local.width, g.local.height});
            }
        };
        append(*run.coverage, true);
        append(*run.color, false);
    }
} // namespace lumora

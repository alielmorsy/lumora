#pragma once

#include "Runtime/DisplayList.h"

#include <lumora/Paint/PaintReuse.h>

#include <lumora/Debug/FrameCounters.h>
#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Text/Glyphs.h>
#include <lumora/Text/TextMeasurer.h>

#include <vector>

namespace lumora {

struct AppCtx;
class NodeArena;
class TextMeasurer;
class Canvas;
class ImageProvider;

/// Threaded into every per-kind paint callback: the arena, the font seam (null = headless, no glyphs), and reusable scratch
/// buffers so text runs allocate nothing in steady state.
struct PaintContext {
    NodeArena*               arena = nullptr;
    const TextMeasurer*      fonts = nullptr;
    Vec2                     pointerPos{};         // current cursor (surface px) for Paint2D::Mouse/Hovered
    Vec2                     layerOffset{};        // accumulated paint-layer translate: surface = local + layerOffset
    float                    contentScale = 1.f;   // OS DPI scale; vector widgets pick device-space density
    ImageProvider*           images = nullptr;     // path -> ImageHandle decoder (null = headless)
    std::vector<GlyphQuad>   coverageScratch;     // emit output (normalised, pen-offset)
    std::vector<GlyphQuad>   colorScratch;
    std::vector<CachedGlyph> coverageCacheScratch; // label controls (no per-node cache) build here
    std::vector<CachedGlyph> colorCacheScratch;
    FrameCounters*           counters = nullptr;   // null outside a MountedApp frame

    /// Measures/shapes through the seam and counts the call so the emit walk's text cost is separable from layout's. No-ops without a measurer.
    [[nodiscard]] Vec2 MeasureText(std::string_view content, const FontProps& font, float size,
                                   const TextFlowProps& flow, AvailableSpace width) const
    {
        if (!fonts) {
            return {};
        }
        if (counters) {
            ++counters->textMeasureCalls;
        }
        return fonts->Measure(content, font, size, flow, width);
    }

    void BuildGlyphs(std::string_view content, const FontProps& font, float size,
                     const TextFlowProps& flow, AvailableSpace width,
                     std::vector<CachedGlyph>& coverage, std::vector<CachedGlyph>& color) const
    {
        if (!fonts) {
            coverage.clear();
            color.clear();
            return;
        }
        if (counters) {
            ++counters->glyphCacheCalls;
        }
        fonts->BuildGlyphCache(content, font, size, flow, width, coverage, color);
    }
};

/// The emit pass: walks the live tree at `root` once and draws it straight onto `canvas`. No-op for a null root. The caller brackets
/// the frame (BeginFrame/EndFrame) and syncs the glyph atlas first, outside any active GPU rendering scope.
void Paint(AppCtx& ctx, NodeId root, Canvas& canvas);
/// The retained recording plus the emit walk's persistent scratch. Double-buffered: a clean subtree is copied from last frame's
/// stream while this frame's is built, so a steady frame allocates nothing.
struct PaintCache {
    DisplayList  current;
    DisplayList  previous;
    PaintContext scratch;         ///< the glyph/vertex scratch, hoisted so it keeps its capacity
    bool         primed = false;  ///< false until one full recording exists to reuse from
};

/// Rebuilds `cache.current` for the tree at `root`, republishing the bytes of every non-repaint-dirty subtree (when `subtreeReuse`);
/// consumes the Repaint bits it acts on. Replay with `cache.current.Replay(canvas, counters)`.
void Record(AppCtx& ctx, NodeId root, PaintCache& cache, bool subtreeReuse);


} // namespace lumora

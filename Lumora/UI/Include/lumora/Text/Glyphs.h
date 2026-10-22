#pragma once

#include <lumora/Foundation/Rect.h>

#include <cstdint>

namespace lumora
{
    /// Everything the rendering backend needs for text: positioned atlas quads plus a read-only CPU atlas view. The backend
    /// never sees a font or glyph index.

    /// Which atlas plane a glyph lives in: Coverage is an 8-bit alpha mask tinted by the run colour; Color is full RGBA (emoji).
    enum class AtlasKind : std::uint8_t { Coverage, Color };

    /// CPU atlas plane layout: R8 is one coverage byte per texel; Rgba8888 is four straight-alpha bytes.
    enum class AtlasFormat : std::uint8_t { R8, Rgba8888 };

    /// Snapshot of a CPU glyph-atlas plane for the backend to mirror. `version` bumps when a glyph is first rasterised, so the
    /// backend re-uploads only on change. `[dirtyY0, dirtyY1)` is the row span changed since the plane was last viewed (viewing
    /// consumes it); an empty range on a version change means a full upload. The pointer belongs to the FontSystem and is
    /// valid until the next mutation: consume it during SyncGlyphAtlas, never retain it.
    struct AtlasView
    {
        const std::uint8_t* pixels = nullptr;
        int width = 0;
        int height = 0;
        AtlasFormat format = AtlasFormat::R8;
        std::uint64_t version = 0;
        int dirtyY0 = 0; ///< first changed row since last view
        int dirtyY1 = 0; ///< one past the last changed row (empty when <= dirtyY0)
    };

    /// One positioned glyph: a destination rect in surface pixels and a source rect in atlas UV (0..1); one textured quad each.
    struct GlyphQuad
    {
        Rect dst; ///< surface-space pixel rect (top-left origin)
        Rect uv; ///< atlas UV rect: x,y = u0,v0 ; width,height = Δu,Δv
    };

    /// A glyph cached on a text node: the destination rect is pen-relative (the node can move without invalidation) and the
    /// source is an atlas pixel rect (atlas growth does not invalidate). The paint pass re-adds the pen and normalises against
    /// the current atlas size. The plane is implied by which list holds it.
    struct CachedGlyph
    {
        Rect local; ///< pen-relative pixel rect (scaled for colour strikes)
        std::uint16_t sx, sy; ///< atlas pixel top-left
        std::uint16_t sw, sh; ///< atlas pixel size
    };
}

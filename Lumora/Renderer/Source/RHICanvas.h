#pragma once

#include <lumora/Paint/Canvas.h>
#include <lumora/rhi/Handles.h>

#include "BatchBuilder.h"

#include <cstdint>
#include <span>
#include <vector>

namespace lumora {

/// A logical texture resolved to its RHI view + sampler. `alphaMask` marks the R8 coverage plane (tinted alpha mask) vs an
/// RGBA source (emoji/image, sampled straight).
struct TextureRef {
    rhi::TextureViewHandle view{};
    rhi::SamplerHandle     sampler{};
    bool                   alphaMask = false;

    [[nodiscard]] constexpr bool operator==(const TextureRef&) const = default;
};

/// Resolves the canvas's logical textures (atlas planes, uploaded images) to RHI views; implemented by the painter, which owns them.
class TextureResolver {
public:
    virtual ~TextureResolver() = default;
    virtual TextureRef AtlasRef(AtlasKind kind) = 0;
    virtual TextureRef ImageRef(ImageHandle image) = 0;
};

/// One arbitrary-clip coverage mask rasterized in a pre-pass: a slice [firstVertex, firstVertex+vertexCount) of the
/// mask-vertex stream (surface px, coverage in alpha);
/// `bounds` is the surface-pixel bbox the mask texture is sized and mapped to. 1-based ids: id N is `_masks[N-1]`.
struct MaskRequest {
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    Rect          bounds{};
};

/// The single concrete Canvas: adapts per-primitive calls onto the RHI-free BatchBuilder, resolving each glyph run/image to a
/// texture key. `DrawExternal` is a concrete-only raw-handle debug path.
class RHICanvas final : public Canvas {
public:
    explicit RHICanvas(TextureResolver& resolver) noexcept : _resolver(&resolver) {}

    void BeginFrame(Vec2 surfaceSize) override;
    bool ReuseFrame() override;
    void EndFrame() override;
    void DrawRect(const RectCmd& rect) override;
    void DrawShape(const ShapeCmd& shape) override;
    void DrawGlyphs(std::span<const GlyphQuad> quads, AtlasKind atlas, Color tint) override;
    void DrawImage(ImageHandle image, const Rect& dst, float opacity, float radius) override;
    void DrawTriangles(std::span<const PaintVertex> verts) override;
    void PushClip(const Rect& bounds, float radius) override;
    std::uint32_t PushClipMask(std::span<const PaintVertex> coverageTris, const Rect& boundsSurface) override;
    void PopClip() override;
    void PushLayer(float opacity, const float transform[9]) override;
    void PopLayer() override;

    /// Draw a raw RHI texture view - the debug "render an RHI image" path.
    void DrawExternal(rhi::TextureViewHandle view, rhi::SamplerHandle sampler, const Rect& dst);

    /// Logical → physical pixel ratio applied to the next BeginFrame (seeds the builder's base layer).
    void SetContentScale(float scale) noexcept { _contentScale = scale; }

    [[nodiscard]] const BatchBuilder&         Builder()  const noexcept { return _builder; }
    [[nodiscard]] std::span<const TextureRef> Textures() const noexcept { return _textures; }

    /// Mask pre-pass inputs: a flat vertex stream and per-mask slices into it; the painter uploads once and renders one R8
    /// mask per request.
    [[nodiscard]] std::span<const UiVertex>    MaskVertices() const noexcept { return _maskVerts; }
    [[nodiscard]] std::span<const MaskRequest> MaskRequests() const noexcept { return _masks; }

private:
    [[nodiscard]] std::uint32_t KeyFor(const TextureRef& ref);

    TextureResolver*         _resolver;
    BatchBuilder             _builder;
    std::vector<TextureRef>  _textures; // index = batch textureKey; [0] = none
    std::vector<UiVertex>    _maskVerts;
    std::vector<MaskRequest> _masks;    // mask id N = _masks[N-1]
    float                    _contentScale = 1.f;

    // Last frame's side tables, kept with the builder's retained streams so ReuseFrame restores a self-consistent frame.
    std::vector<TextureRef>  _retainedTextures;
    std::vector<UiVertex>    _retainedMaskVerts;
    std::vector<MaskRequest> _retainedMasks;
    float                    _retainedScale = 1.f;
};

} // namespace lumora

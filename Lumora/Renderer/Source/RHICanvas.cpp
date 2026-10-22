#include "RHICanvas.h"

#include <algorithm>

namespace lumora {

std::uint32_t RHICanvas::KeyFor(const TextureRef& ref) {
    if (!ref.view.IsValid()) {
        return 0; // untextured
    }
    for (std::uint32_t i = 1; i < _textures.size(); ++i) {
        if (_textures[i] == ref) {
            return i; // dedup so repeated runs of the same plane merge into one batch
        }
    }
    _textures.push_back(ref);
    return static_cast<std::uint32_t>(_textures.size() - 1);
}

void RHICanvas::BeginFrame(Vec2 surfaceSize) {
    _retainedTextures.swap(_textures);
    _retainedMaskVerts.swap(_maskVerts);
    _retainedMasks.swap(_masks);
    _builder.Retain();

    _textures.clear();
    _textures.push_back(TextureRef{}); // key 0 = untextured
    _maskVerts.clear();
    _masks.clear();
    _builder.Begin(surfaceSize, _contentScale);
}

bool RHICanvas::ReuseFrame() {
    // A content-scale change re-seeds the base layer, so vertices built at the old scale are the
    // wrong size on screen: refuse, and let the caller rebuild.
    if (_contentScale != _retainedScale || !_builder.Restore()) {
        return false;
    }
    _textures.swap(_retainedTextures);
    _maskVerts.swap(_retainedMaskVerts);
    _masks.swap(_retainedMasks);
    return true;
}

void RHICanvas::EndFrame() {
    _builder.End();
    _retainedScale = _contentScale; // the scale the streams the painter is about to read were built at
}

void RHICanvas::DrawRect(const RectCmd& rect) {
    _builder.AddRect(rect);
}

void RHICanvas::DrawShape(const ShapeCmd& shape) {
    _builder.AddShape(shape);
}

void RHICanvas::DrawGlyphs(std::span<const GlyphQuad> quads, AtlasKind atlas, Color tint) {
    _builder.AddGlyphs(quads, KeyFor(_resolver->AtlasRef(atlas)), tint);
}

void RHICanvas::DrawImage(ImageHandle image, const Rect& dst, float opacity, float radius) {
    _builder.AddImage(dst, KeyFor(_resolver->ImageRef(image)), opacity, radius);
}

void RHICanvas::DrawTriangles(std::span<const PaintVertex> verts) {
    _builder.AddTriangles(verts);
}

void RHICanvas::PushClip(const Rect& bounds, float radius) {
    _builder.PushClip(bounds, radius);
}

std::uint32_t RHICanvas::PushClipMask(std::span<const PaintVertex> tris, const Rect& bounds) {
    // Coverage tris + bounds arrive in the caller's draw space; fold the active layer into both so the
    // mask lands in surface space aligned with the fills it clips (matches DrawTriangles + PushClip).
    const std::uint32_t first = static_cast<std::uint32_t>(_maskVerts.size());
    for (const PaintVertex& v : tris) {
        const std::uint32_t rgba = static_cast<std::uint32_t>(v.col.r) |
            (static_cast<std::uint32_t>(v.col.g) << 8) | (static_cast<std::uint32_t>(v.col.b) << 16) |
            (static_cast<std::uint32_t>(v.col.a) << 24);
        _maskVerts.push_back({.pos = _builder.ApplyCurrentLayer(v.pos), .uv = {0.f, 0.f}, .rgba = rgba});
    }
    const std::uint32_t count = static_cast<std::uint32_t>(_maskVerts.size()) - first;

    const Vec2 c0 = _builder.ApplyCurrentLayer({bounds.x, bounds.y});
    const Vec2 c1 = _builder.ApplyCurrentLayer({bounds.x + bounds.width, bounds.y});
    const Vec2 c2 = _builder.ApplyCurrentLayer({bounds.x + bounds.width, bounds.y + bounds.height});
    const Vec2 c3 = _builder.ApplyCurrentLayer({bounds.x, bounds.y + bounds.height});
    const float minX = std::min({c0.x, c1.x, c2.x, c3.x});
    const float minY = std::min({c0.y, c1.y, c2.y, c3.y});
    const float maxX = std::max({c0.x, c1.x, c2.x, c3.x});
    const float maxY = std::max({c0.y, c1.y, c2.y, c3.y});
    const Rect surf{minX, minY, maxX - minX, maxY - minY};

    if (count < 3 || surf.width <= 0.f || surf.height <= 0.f) {
        _maskVerts.resize(first);            // degenerate: drop the verts, fall back to a bbox scissor
        _builder.PushClipMask(0, surf);
        return 0;
    }
    _masks.push_back({first, count, surf});
    const std::uint32_t maskId = static_cast<std::uint32_t>(_masks.size()); // 1-based
    _builder.PushClipMask(maskId, surf);
    return maskId;
}

void RHICanvas::PopClip() {
    _builder.PopClip();
}

void RHICanvas::PushLayer(float opacity, const float transform[9]) {
    _builder.PushLayer(opacity, transform);
}

void RHICanvas::PopLayer() {
    _builder.PopLayer();
}

void RHICanvas::DrawExternal(rhi::TextureViewHandle view, rhi::SamplerHandle sampler, const Rect& dst) {
    _builder.AddImage(dst, KeyFor(TextureRef{.view = view, .sampler = sampler}), 1.f, 0.f);
}

} // namespace lumora

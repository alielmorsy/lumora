#pragma once

#include <lumora/Paint/Canvas.h>

#include <cstdint>
#include <vector>

namespace lumora {

/// A headless test Canvas that records every primitive into an ordered call log, so emit-walk tests can assert draw order,
/// payloads and balanced clip/layer nesting without a GPU.
struct CanvasCall {
    enum class Op { BeginFrame, EndFrame, Rect, Shape, Glyphs, Image, Triangles, PushClip, PopClip, PushLayer, PopLayer };

    Op            op;
    RectCmd       rect{};                                       // Rect
    ShapeCmd      shape{};                                      // Shape
    Rect          bounds{};                                     // PushClip / Image dst
    float         radius     = 0.f;                             // PushClip / Image
    AtlasKind     atlas      = AtlasKind::Coverage;             // Glyphs
    Color         tint       = Color::Black;                    // Glyphs
    std::uint32_t glyphCount = 0;                               // Glyphs
    Rect          glyphDst{};                                   // Glyphs: first quad's screen dst
    ImageHandle   image{};                                      // Image
    float         opacity    = 1.f;                             // Image / PushLayer
    float         transform[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f}; // PushLayer
    std::uint32_t triCount   = 0;                               // Triangles
    Vec2          surfaceSize{};                                // BeginFrame
};

class RecordingCanvas final : public Canvas {
public:
    std::vector<CanvasCall> calls;

    void Clear() { calls.clear(); }

    void BeginFrame(Vec2 surfaceSize) override {
        calls.push_back({.op = CanvasCall::Op::BeginFrame, .surfaceSize = surfaceSize});
    }
    void EndFrame() override { calls.push_back({.op = CanvasCall::Op::EndFrame}); }

    void DrawRect(const RectCmd& rect) override {
        calls.push_back({.op = CanvasCall::Op::Rect, .rect = rect});
    }

    void DrawShape(const ShapeCmd& shape) override {
        calls.push_back({.op = CanvasCall::Op::Shape, .shape = shape});
    }

    void DrawGlyphs(std::span<const GlyphQuad> quads, AtlasKind atlas, Color tint) override {
        calls.push_back({.op         = CanvasCall::Op::Glyphs,
                         .atlas      = atlas,
                         .tint       = tint,
                         .glyphCount = static_cast<std::uint32_t>(quads.size()),
                         .glyphDst   = quads.empty() ? Rect{} : quads.front().dst});
    }

    void DrawImage(ImageHandle image, const Rect& dst, float opacity, float radius) override {
        calls.push_back({.op      = CanvasCall::Op::Image,
                         .bounds  = dst,
                         .radius  = radius,
                         .image   = image,
                         .opacity = opacity});
    }

    void DrawTriangles(std::span<const PaintVertex> verts) override {
        calls.push_back({.op = CanvasCall::Op::Triangles,
                         .triCount = static_cast<std::uint32_t>(verts.size())});
    }

    void PushClip(const Rect& bounds, float radius) override {
        calls.push_back({.op = CanvasCall::Op::PushClip, .bounds = bounds, .radius = radius});
    }
    void PopClip() override { calls.push_back({.op = CanvasCall::Op::PopClip}); }

    void PushLayer(float opacity, const float transform[9]) override {
        CanvasCall c{.op = CanvasCall::Op::PushLayer, .opacity = opacity};
        if (transform) {
            for (int i = 0; i < 9; ++i) {
                c.transform[i] = transform[i];
            }
        }
        calls.push_back(c);
    }
    void PopLayer() override { calls.push_back({.op = CanvasCall::Op::PopLayer}); }

    [[nodiscard]] int CountOp(CanvasCall::Op op) const {
        int n = 0;
        for (const CanvasCall& c : calls) {
            if (c.op == op) {
                ++n;
            }
        }
        return n;
    }

    /// True when every PushClip/PushLayer has its matching Pop and no Pop underflows.
    [[nodiscard]] bool Balanced() const {
        int clip = 0, layer = 0;
        for (const CanvasCall& c : calls) {
            switch (c.op) {
                case CanvasCall::Op::PushClip:  ++clip; break;
                case CanvasCall::Op::PopClip:   if (--clip < 0) return false; break;
                case CanvasCall::Op::PushLayer: ++layer; break;
                case CanvasCall::Op::PopLayer:  if (--layer < 0) return false; break;
                default:                        break;
            }
        }
        return clip == 0 && layer == 0;
    }
};

} // namespace lumora

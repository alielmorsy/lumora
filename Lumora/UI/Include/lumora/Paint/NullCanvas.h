#pragma once

#include <lumora/Paint/Canvas.h>

namespace lumora
{
    /// Headless/server Canvas: every primitive is a no-op (builds without a render backend paint here). Image minting
    /// returns unique non-zero handles.
    class NullCanvas final : public Canvas
    {
    public:
        void BeginFrame(Vec2) override
        {
        }

        /// Reports a re-presented frame so the headless bench and counters can observe skips (an idle frame reports emitVisited == 0).
        bool ReuseFrame() override { return true; }

        void EndFrame() override
        {
        }

        void DrawRect(const RectCmd&) override
        {
        }

        void DrawShape(const ShapeCmd&) override
        {
        }

        void DrawGlyphs(std::span<const GlyphQuad>, AtlasKind, Color) override
        {
        }

        void DrawImage(ImageHandle, const Rect&, float, float) override
        {
        }

        void DrawTriangles(std::span<const PaintVertex>) override
        {
        }

        void PushClip(const Rect&, float) override
        {
        }

        void PopClip() override
        {
        }

        void PushLayer(float, const float [9]) override
        {
        }

        void PopLayer() override
        {
        }
    };
}

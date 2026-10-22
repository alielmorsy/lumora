#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Text/Glyphs.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lumora
{
    struct FrameCounters;

    /// Retained paint recording: each node remembers its subtree's byte range (LiveNode::paint) so a clean subtree is
    /// republished by copying it. Ranges are parent-relative and clips/layers are recorded as ops, so a moved ancestor re-records only itself.
    enum class PaintOp : std::uint32_t
    {
        Rect,
        Shape,
        Glyphs,
        Image,
        Triangles,
        PushClip,
        PushClipMask,
        PopClip,
        PushLayer,
        PopLayer,
    };

    /// Record prologue; `bytes` spans header + payload and is 8-aligned.
    struct PaintRecord
    {
        PaintOp       op    = PaintOp::PopClip;
        std::uint32_t bytes = 0;
    };

    /// Flat, append-only recording of Canvas calls; trivially copyable payloads only.
    class DisplayList
    {
    public:
        /// Keeps the buffer so the steady-state frame never allocates.
        void Clear() noexcept { m_bytes.clear(); }

        [[nodiscard]] std::uint32_t Size() const noexcept
        {
            return static_cast<std::uint32_t>(m_bytes.size());
        }
        [[nodiscard]] bool Empty() const noexcept { return m_bytes.empty(); }

        void Swap(DisplayList& other) noexcept { m_bytes.swap(other.m_bytes); }

        /// Copies `bytes` of `src` starting at `first` onto the end. False if the range is not wholly inside `src` (caller must re-record).
        [[nodiscard]] bool Append(const DisplayList& src, std::uint32_t first, std::uint32_t bytes);

        /// Push the whole recording at `canvas` in order. `counters` is optional.
        void Replay(Canvas& canvas, FrameCounters* counters) const;

        void RecordRect(const RectCmd& rect);
        void RecordShape(const ShapeCmd& shape);
        void RecordGlyphs(std::span<const GlyphQuad> quads, AtlasKind atlas, Color tint);
        void RecordImage(ImageHandle image, const Rect& dst, float opacity, float radius);
        void RecordTriangles(std::span<const PaintVertex> verts);
        void RecordPushClip(const Rect& bounds, float radius);
        void RecordPushClipMask(std::span<const PaintVertex> coverage, const Rect& bounds);
        void RecordPopClip();
        void RecordPushLayer(float opacity, const float transform[9]);
        void RecordPopLayer();

    private:
        /// Reserve an 8-aligned record of `payloadBytes` and return the offset its payload starts at.
        [[nodiscard]] std::size_t Open(PaintOp op, std::size_t payloadBytes);
        void Write(std::size_t at, const void* data, std::size_t bytes) noexcept;

        std::vector<std::byte> m_bytes;
    };

    /// Canvas that records into a DisplayList instead of drawing.
    class DisplayListCanvas final : public Canvas
    {
    public:
        explicit DisplayListCanvas(DisplayList& list) noexcept : m_list(&list) {}

        void BeginFrame(Vec2) override {}
        void EndFrame() override {}

        void DrawRect(const RectCmd& rect) override { m_list->RecordRect(rect); }
        void DrawShape(const ShapeCmd& shape) override { m_list->RecordShape(shape); }

        void DrawGlyphs(std::span<const GlyphQuad> quads, AtlasKind atlas, Color tint) override
        {
            m_list->RecordGlyphs(quads, atlas, tint);
        }

        void DrawImage(ImageHandle image, const Rect& dst, float opacity, float radius) override
        {
            m_list->RecordImage(image, dst, opacity, radius);
        }

        void DrawTriangles(std::span<const PaintVertex> verts) override
        {
            m_list->RecordTriangles(verts);
        }

        void PushClip(const Rect& bounds, float radius) override
        {
            m_list->RecordPushClip(bounds, radius);
        }

        /// Returns 0: the backend mask id is minted at replay. Callers only use it as a "supported" probe.
        std::uint32_t PushClipMask(std::span<const PaintVertex> coverage, const Rect& bounds) override
        {
            m_list->RecordPushClipMask(coverage, bounds);
            return 0;
        }

        void PopClip() override { m_list->RecordPopClip(); }

        void PushLayer(float opacity, const float transform[9]) override
        {
            m_list->RecordPushLayer(opacity, transform);
        }

        void PopLayer() override { m_list->RecordPopLayer(); }

    private:
        DisplayList* m_list;
    };
} // namespace lumora

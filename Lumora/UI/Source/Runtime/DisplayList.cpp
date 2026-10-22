#include "Runtime/DisplayList.h"

#include <lumora/Debug/FrameCounters.h>

#include <cstring>

namespace lumora
{
    namespace
    {
        constexpr std::size_t kAlign = 8;

        [[nodiscard]] constexpr std::size_t AlignUp(std::size_t n) noexcept
        {
            return (n + (kAlign - 1)) & ~(kAlign - 1);
        }

        struct GlyphsHead
        {
            std::uint32_t count = 0;
            std::uint32_t atlas = 0;
            std::uint32_t tint  = 0;
            std::uint32_t pad   = 0; // keeps the trailing GlyphQuad array 8-aligned
        };

        struct ImageHead
        {
            std::uint64_t imageId     = 0;
            float         imageWidth  = 0.f;
            float         imageHeight = 0.f;
            Rect          dst{};
            float         opacity     = 1.f;
            float         radius      = 0.f;
        };

        struct TrisHead
        {
            std::uint32_t count = 0;
            std::uint32_t pad   = 0; // keeps the trailing PaintVertex array 8-aligned
        };

        struct ClipHead
        {
            Rect  bounds{};
            float radius = 0.f;
        };

        struct ClipMaskHead
        {
            Rect          bounds{};
            std::uint32_t count = 0;
            std::uint32_t pad   = 0; // keeps the trailing PaintVertex array 8-aligned
        };

        struct LayerHead
        {
            float         opacity      = 1.f;
            std::uint32_t hasTransform = 0;
            float         transform[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
        };

        [[nodiscard]] std::uint32_t PackColor(const Color c) noexcept
        {
            return static_cast<std::uint32_t>(c.r) | (static_cast<std::uint32_t>(c.g) << 8) |
                (static_cast<std::uint32_t>(c.b) << 16) | (static_cast<std::uint32_t>(c.a) << 24);
        }

        [[nodiscard]] Color UnpackColor(const std::uint32_t v) noexcept
        {
            return Color{static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                         static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
        }

        template<typename T>
        [[nodiscard]] T ReadAs(const std::byte* at) noexcept
        {
            T value{};
            std::memcpy(&value, at, sizeof(T));
            return value;
        }
    } // namespace

    std::size_t DisplayList::Open(const PaintOp op, const std::size_t payloadBytes)
    {
        const std::size_t start = m_bytes.size();
        const std::size_t total = AlignUp(sizeof(PaintRecord) + payloadBytes);
        m_bytes.resize(start + total);
        const PaintRecord header{op, static_cast<std::uint32_t>(total)};
        std::memcpy(m_bytes.data() + start, &header, sizeof(header));
        return start + sizeof(PaintRecord);
    }

    void DisplayList::Write(const std::size_t at, const void* data, const std::size_t bytes) noexcept
    {
        if (bytes != 0)
        {
            std::memcpy(m_bytes.data() + at, data, bytes);
        }
    }

    bool DisplayList::Append(const DisplayList& src, const std::uint32_t first, const std::uint32_t bytes)
    {
        if (static_cast<std::size_t>(first) + bytes > src.m_bytes.size())
        {
            return false;
        }
        if (bytes != 0)
        {
            m_bytes.insert(m_bytes.end(), src.m_bytes.begin() + first,
                           src.m_bytes.begin() + first + bytes);
        }
        return true;
    }

    void DisplayList::RecordRect(const RectCmd& rect)
    {
        Write(Open(PaintOp::Rect, sizeof(RectCmd)), &rect, sizeof(RectCmd));
    }

    void DisplayList::RecordShape(const ShapeCmd& shape)
    {
        Write(Open(PaintOp::Shape, sizeof(ShapeCmd)), &shape, sizeof(ShapeCmd));
    }

    void DisplayList::RecordGlyphs(const std::span<const GlyphQuad> quads, const AtlasKind atlas,
                                   const Color tint)
    {
        if (quads.empty())
        {
            return;
        }
        const GlyphsHead head{static_cast<std::uint32_t>(quads.size()),
                              static_cast<std::uint32_t>(atlas), PackColor(tint), 0};
        const std::size_t at = Open(PaintOp::Glyphs, sizeof(head) + quads.size_bytes());
        Write(at, &head, sizeof(head));
        Write(at + sizeof(head), quads.data(), quads.size_bytes());
    }

    void DisplayList::RecordImage(const ImageHandle image, const Rect& dst, const float opacity,
                                  const float radius)
    {
        const ImageHead head{image.id, image.width, image.height, dst, opacity, radius};
        Write(Open(PaintOp::Image, sizeof(head)), &head, sizeof(head));
    }

    void DisplayList::RecordTriangles(const std::span<const PaintVertex> verts)
    {
        if (verts.empty())
        {
            return;
        }
        const TrisHead head{static_cast<std::uint32_t>(verts.size()), 0};
        const std::size_t at = Open(PaintOp::Triangles, sizeof(head) + verts.size_bytes());
        Write(at, &head, sizeof(head));
        Write(at + sizeof(head), verts.data(), verts.size_bytes());
    }

    void DisplayList::RecordPushClip(const Rect& bounds, const float radius)
    {
        const ClipHead head{bounds, radius};
        Write(Open(PaintOp::PushClip, sizeof(head)), &head, sizeof(head));
    }

    void DisplayList::RecordPushClipMask(const std::span<const PaintVertex> coverage, const Rect& bounds)
    {
        const ClipMaskHead head{bounds, static_cast<std::uint32_t>(coverage.size()), 0};
        const std::size_t at = Open(PaintOp::PushClipMask, sizeof(head) + coverage.size_bytes());
        Write(at, &head, sizeof(head));
        Write(at + sizeof(head), coverage.data(), coverage.size_bytes());
    }

    void DisplayList::RecordPopClip() { (void) Open(PaintOp::PopClip, 0); }

    void DisplayList::RecordPushLayer(const float opacity, const float transform[9])
    {
        LayerHead head{};
        head.opacity = opacity;
        if (transform != nullptr)
        {
            head.hasTransform = 1;
            std::memcpy(head.transform, transform, sizeof(head.transform));
        }
        Write(Open(PaintOp::PushLayer, sizeof(head)), &head, sizeof(head));
    }

    void DisplayList::RecordPopLayer() { (void) Open(PaintOp::PopLayer, 0); }

    void DisplayList::Replay(Canvas& canvas, FrameCounters* counters) const
    {
        const std::byte* const begin = m_bytes.data();
        const std::size_t total = m_bytes.size();
        std::size_t offset = 0;
        std::uint32_t replayed = 0;
        while (offset + sizeof(PaintRecord) <= total)
        {
            const PaintRecord header = ReadAs<PaintRecord>(begin + offset);
            if (header.bytes < sizeof(PaintRecord) || offset + header.bytes > total)
            {
                break; // truncated stream: refuse to walk off the end rather than trust the bytes
            }
            const std::byte* const payload = begin + offset + sizeof(PaintRecord);
            switch (header.op)
            {
            case PaintOp::Rect:
                canvas.DrawRect(ReadAs<RectCmd>(payload));
                break;
            case PaintOp::Shape:
                canvas.DrawShape(ReadAs<ShapeCmd>(payload));
                break;
            case PaintOp::Glyphs:
            {
                const GlyphsHead head = ReadAs<GlyphsHead>(payload);
                const auto* quads = reinterpret_cast<const GlyphQuad*>(payload + sizeof(GlyphsHead));
                canvas.DrawGlyphs(std::span<const GlyphQuad>(quads, head.count),
                                  static_cast<AtlasKind>(head.atlas), UnpackColor(head.tint));
                break;
            }
            case PaintOp::Image:
            {
                const ImageHead head = ReadAs<ImageHead>(payload);
                canvas.DrawImage(ImageHandle{head.imageId, head.imageWidth, head.imageHeight},
                                 head.dst, head.opacity, head.radius);
                break;
            }
            case PaintOp::Triangles:
            {
                const TrisHead head = ReadAs<TrisHead>(payload);
                const auto* verts = reinterpret_cast<const PaintVertex*>(payload + sizeof(TrisHead));
                canvas.DrawTriangles(std::span<const PaintVertex>(verts, head.count));
                break;
            }
            case PaintOp::PushClip:
            {
                const ClipHead head = ReadAs<ClipHead>(payload);
                canvas.PushClip(head.bounds, head.radius);
                break;
            }
            case PaintOp::PushClipMask:
            {
                const ClipMaskHead head = ReadAs<ClipMaskHead>(payload);
                const auto* verts = reinterpret_cast<const PaintVertex*>(payload + sizeof(ClipMaskHead));
                canvas.PushClipMask(std::span<const PaintVertex>(verts, head.count), head.bounds);
                break;
            }
            case PaintOp::PopClip:
                canvas.PopClip();
                break;
            case PaintOp::PushLayer:
            {
                const LayerHead head = ReadAs<LayerHead>(payload);
                canvas.PushLayer(head.opacity, head.hasTransform != 0 ? head.transform : nullptr);
                break;
            }
            case PaintOp::PopLayer:
                canvas.PopLayer();
                break;
            }
            ++replayed;
            offset += header.bytes;
        }
        if (counters)
        {
            counters->paintReplayed += replayed;
        }
    }
} // namespace lumora

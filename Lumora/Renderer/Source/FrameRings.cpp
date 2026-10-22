#include "FrameRings.h"

#include <lumora/rhi/RHI.h>

#include <algorithm>
#include <cstddef>

namespace lumora
{
    namespace
    {
        constexpr std::uint64_t kMinCapacity = 64U * 1024U;
        /// Uses under a quarter full before a slot shrinks (tens of seconds at framesInFlight 2-3); avoids allocator thrash.
        constexpr std::uint32_t kShrinkAfterUses = 600;
    } // namespace

    void FrameRings::Init(rhi::RHI& rhi, std::uint32_t framesInFlight)
    {
        _rhi = &rhi;
        _slots.assign(std::max(1u, framesInFlight), Slot{});
    }

    void FrameRings::Shutdown()
    {
        if (!_rhi)
        {
            return;
        }
        for (Slot& s : _slots)
        {
            if (s.vertex.buffer.IsValid()) _rhi->Resources().Destroy(s.vertex.buffer);
        }
        _slots.clear();
        _rhi = nullptr;
    }

    FrameRings::FitResult FrameRings::Fit(Ring& ring, std::uint64_t needed, rhi::BufferUsage usage, std::string_view name)
    {
        const bool fits = ring.buffer.IsValid() && ring.capacity >= needed;
        const bool oversized = fits && ring.capacity > kMinCapacity && needed * 4u < ring.capacity;
        ring.lowFrames = oversized ? ring.lowFrames + 1u : 0u;
        if (fits && ring.lowFrames < kShrinkAfterUses)
        {
            return {.recreated = false, .grew = false};
        }

        const bool grew = !fits;
        const std::uint64_t capacity = fits
                                           ? std::max(needed * 2u, kMinCapacity)
                                           : std::max({needed, ring.capacity + ring.capacity / 2u, kMinCapacity});
        if (ring.buffer.IsValid())
        {
            _rhi->Resources().Destroy(ring.buffer); // last use was framesInFlight ago - fence already waited
        }
        ring.buffer = _rhi->Resources().CreateBuffer({
            .size = capacity,
            .usage = usage,
            .memory = rhi::MemoryPattern::CpuToGpuMapped,
            .debugName = name,
            .preferHost = true
        });
        ring.capacity = ring.buffer.IsValid() ? capacity : 0u;
        ring.lowFrames = 0;
        return {.recreated = true, .grew = grew};
    }

    FrameRings::Frame FrameRings::Prepare(std::uint32_t frameIndex, std::uint64_t vertexBytes,
                                          std::uint64_t generation)
    {
        if (_slots.empty())
        {
            return {};
        }
        Slot& s = _slots[frameIndex % _slots.size()];
        const FitResult vertexFit = Fit(s.vertex, vertexBytes, rhi::BufferUsage::Vertex, "Lumora.VertexRing");
        if (vertexFit.recreated)
        {
            s.uploadedGeneration = 0;
        }
        const bool needsUpload = vertexFit.recreated || (generation == 0) || (s.uploadedGeneration != generation);
        return {s.vertex.buffer, vertexFit.grew, needsUpload};
    }

    void FrameRings::MarkUploaded(std::uint32_t frameIndex, std::uint64_t generation)
    {
        if (_slots.empty())
        {
            return;
        }
        _slots[frameIndex % _slots.size()].uploadedGeneration = generation;
    }

    void FrameRings::Invalidate()
    {
        for (Slot& s : _slots)
        {
            s.uploadedGeneration = 0;
        }
    }

    std::uint64_t FrameRings::VertexCapacity(std::uint32_t frameIndex) const noexcept
    {
        if (_slots.empty())
        {
            return 0;
        }
        return _slots[frameIndex % _slots.size()].vertex.capacity;
    }
} // namespace lumora

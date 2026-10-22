#pragma once

#include <lumora/rhi/Flags.h>
#include <lumora/rhi/Handles.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace lumora::rhi
{
    class RHI;
}

namespace lumora
{
    /// Per-frame-in-flight, host-visible vertex buffer ring. A frame's buffer is rewritten when its index comes round; a
    /// resize destroys the old buffer immediately, safe because the RHI already waited that frame's fence.
    /// Plain host memory, not BAR (without ReBAR the device-local host-visible heap is only 256 MiB). A ring grows 1.5x on
    /// demand and shrinks after running under a quarter full for long enough.
    class FrameRings
    {
    public:
        void Init(rhi::RHI& rhi, std::uint32_t framesInFlight);
        void Shutdown();

        struct Frame
        {
            rhi::BufferHandle vertex{};
            bool grew = false;
            bool needsUpload = true;

            /// False when an allocation failed; the frame's UI must be skipped, not written.
            [[nodiscard]] bool Valid() const noexcept { return vertex.IsValid(); }
        };

        /// Buffer for `frameIndex` holding at least `vertexBytes`, resizing if needed. `needsUpload` is false when
        /// `generation` matches the slot and the ring was not recreated.
        [[nodiscard]] Frame Prepare(std::uint32_t frameIndex, std::uint64_t vertexBytes,
                                    std::uint64_t generation = 0);

        void MarkUploaded(std::uint32_t frameIndex, std::uint64_t generation);
        void Invalidate();

        [[nodiscard]] std::uint64_t VertexCapacity(std::uint32_t frameIndex) const noexcept;

    private:
        struct Ring
        {
            rhi::BufferHandle buffer{};
            std::uint64_t capacity = 0;
            std::uint32_t lowFrames = 0;
        };

        struct Slot
        {
            Ring vertex;
            std::uint64_t uploadedGeneration = 0;
        };

        struct FitResult
        {
            bool recreated = false;
            bool grew = false;
        };

        /// Ensures the ring has at least `needed` capacity, resizing if needed or shrinking when underutilized.
        FitResult Fit(Ring& ring, std::uint64_t needed, rhi::BufferUsage usage, std::string_view name);

        rhi::RHI* _rhi = nullptr;
        std::vector<Slot> _slots;
    };
} // namespace lumora

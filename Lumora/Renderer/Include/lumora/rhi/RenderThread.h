#pragma once

#include <lumora/Api.h>
#include <lumora/platform/Thread.h>
#include <lumora/rhi/Common.h>
#include <lumora/rhi/Descriptions.h>
#include <lumora/rhi/Handles.h>
#include <lumora/rhi/Lumora.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace lumora
{
    struct RenderStats
    {
        double acquireMs = 0.0;
        double waitPacketMs = 0.0;
        double renderFrameMs = 0.0;
        double presentMs = 0.0;
        std::uint64_t frameCount = 0;
    };

    struct RenderThreadDesc
    {
        rhi::RHI& device;
        Lumora& ui;
        rhi::SwapchainHandle swapchain{};
        std::function<void()> wakeUi;
        std::function<rhi::Extent2D()> getWindowExtent;
    };

    class LUMORA_API RenderThread
    {
    public:
        explicit RenderThread(const RenderThreadDesc& desc);
        ~RenderThread();

        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        /// Starts the render thread ("LumoraRender" at ThreadPriority::Display).
        bool Start();

        /// Stops the render thread, wakes any waiting threads, and joins.
        void Stop();

        /// UI thread: waits for the render thread to request a frame. Sets extent to swapchain extent.
        /// Returns false if stopping.
        bool WaitFrameRequest(rhi::Extent2D& extent);

        /// UI thread: returns the packet currently being prepared.
        [[nodiscard]] FramePacket& Packet();

        /// UI thread: submits the current packet to the render thread and advances rotation.
        void Submit();

        /// OS thread: blocks until current frame finishes and swapchain is destroyed.
        void LoseSurface();

        /// OS thread: creates a new swapchain for handles and resumes rendering. Blocks until done.
        void SetSurface(rhi::NativeWindowHandles handles);

        /// Returns true if the render thread is still running.
        [[nodiscard]] bool IsRunning() const noexcept;

        /// Resets and returns accumulated render stats.
        RenderStats TakeRenderStats();

    private:
        void Run();

        rhi::RHI* m_device = nullptr;
        Lumora* m_ui = nullptr;
        rhi::SwapchainHandle m_swapchain{};
        rhi::Extent2D m_swapchainExtent{};
        std::function<void()> m_wakeUi;
        std::function<rhi::Extent2D()> m_getWindowExtent;

        platform::Thread m_thread;

        mutable std::mutex m_mutex;
        std::condition_variable m_cvRender;
        std::condition_variable m_cvUi;
        std::condition_variable m_cvSurface;

        FramePacket m_packets[3];
        std::size_t m_writeIndex = 0;
        std::size_t m_pendingIndex = 0;

        bool m_stopping = false;
        bool m_running = false;
        bool m_hasSurface = true;
        bool m_surfaceLostPending = false;
        std::optional<rhi::NativeWindowHandles> m_newSurfaceHandles;

        bool m_hasFrameRequest = false;
        rhi::Extent2D m_requestedExtent{};
        bool m_hasPendingPacket = false;

        RenderStats m_accumStats;
    };
}

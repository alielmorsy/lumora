#include <lumora/rhi/RenderThread.h>

#include <lumora/rhi/CommandBuffer.h>
#include <lumora/rhi/RHI.h>

#include <chrono>
#include <thread>
#include <utility>

namespace lumora
{
    RenderThread::RenderThread(const RenderThreadDesc& desc)
        : m_device(&desc.device)
        , m_ui(&desc.ui)
        , m_swapchain(desc.swapchain)
        , m_getWindowExtent(desc.getWindowExtent)
        , m_hasSurface(desc.swapchain.IsValid())
    {
        if (m_swapchain.IsValid() && m_device)
        {
            m_swapchainExtent = m_device->Swapchain().GetSwapchainExtent(m_swapchain);
        }
    }

    RenderThread::~RenderThread()
    {
        Stop();
    }

    bool RenderThread::Start()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running)
        {
            return false;
        }
        m_stopping = false;
        m_running = true;

        return m_thread.Start(
            "LumoraRender",
            [this] {
                Run();
                std::lock_guard<std::mutex> lk(m_mutex);
                m_running = false;
            },
            platform::ThreadPriority::Display);
    }

    void RenderThread::RequestStop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
        {
            return;
        }
        m_stopping = true;
        m_cvRender.notify_all();
        m_cvUi.notify_all();
        m_cvSurface.notify_all();
    }

    void RenderThread::Stop()
    {
        RequestStop();
        if (m_thread.Joinable())
        {
            m_thread.Join();
        }
    }

    bool RenderThread::WaitFrameRequest(rhi::Extent2D& extent)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvUi.wait(lock, [&] {
            return (m_hasFrameRequest && !m_hasPendingPacket) || m_stopping;
        });
        if (m_stopping)
        {
            return false;
        }
        extent = m_requestedExtent;
        return true;
    }

    FramePacket& RenderThread::Packet()
    {
        return m_packets[m_writeIndex];
    }

    void RenderThread::Submit()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingIndex = m_writeIndex;
        m_hasPendingPacket = true;
        m_writeIndex = (m_writeIndex + 1) % 3;
        m_cvRender.notify_all();
    }

    void RenderThread::LoseSurface()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_hasSurface)
        {
            return;
        }
        m_surfaceLostPending = true;
        m_cvRender.notify_all();
        m_cvSurface.wait(lock, [&] {
            return !m_hasSurface || m_stopping;
        });
    }

    void RenderThread::SetSurface(rhi::NativeWindowHandles handles)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_newSurfaceHandles = handles;
        m_cvRender.notify_all();
        m_cvSurface.wait(lock, [&] {
            return m_hasSurface || m_stopping;
        });
    }

    bool RenderThread::IsRunning() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

    RenderStats RenderThread::TakeRenderStats()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        RenderStats copy = m_accumStats;
        m_accumStats = RenderStats{};
        return copy;
    }

    void RenderThread::Run()
    {
        platform::SetCurrentThreadName("LumoraRender");
        platform::SetCurrentThreadPriority(platform::ThreadPriority::Display);

        using Clock = std::chrono::steady_clock;
        auto ms = [](Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };

        while (true)
        {
            // 1. Check stopping and surface state
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (m_stopping)
                {
                    break;
                }

                if (m_surfaceLostPending)
                {
                    if (m_swapchain.IsValid())
                    {
                        m_device->Swapchain().DestroySwapchain(m_swapchain);
                        m_swapchain = {};
                    }
                    m_hasSurface = false;
                    m_surfaceLostPending = false;
                    m_cvSurface.notify_all();
                }

                while (!m_hasSurface && !m_stopping)
                {
                    if (m_newSurfaceHandles.has_value())
                    {
                        rhi::SwapchainDescription desc{};
                        desc.nativeWindow = *m_newSurfaceHandles;
                        desc.extent = m_getWindowExtent ? m_getWindowExtent() : rhi::Extent2D{800, 600};
                        m_swapchain = m_device->Swapchain().CreateSwapchain(desc);
                        m_swapchainExtent = desc.extent;
                        m_hasSurface = true;
                        m_newSurfaceHandles.reset();
                        m_cvSurface.notify_all();
                        break;
                    }
                    m_cvRender.wait(lock);
                    if (m_stopping)
                    {
                        break;
                    }
                }

                if (m_stopping)
                {
                    break;
                }

                // Post frame request before BeginFrame if no packet is pending
                if (!m_hasPendingPacket && !m_hasFrameRequest)
                {
                    m_hasFrameRequest = true;
                    m_requestedExtent = m_device->Swapchain().GetSwapchainExtent(m_swapchain);
                    m_swapchainExtent = m_requestedExtent;
                    m_cvUi.notify_all();
                }
            }

            // 2. BeginFrame
            const auto tAcquireStart = Clock::now();
            rhi::FrameContext ctx = m_device->BeginFrame(m_swapchain);
            const auto tAcquireEnd = Clock::now();
            const double acquireMs = ms(tAcquireStart, tAcquireEnd);

            // 3. Process frame or carry over
            double waitPacketMs = 0.0;
            double renderFrameMs = 0.0;
            double presentMs = 0.0;

            if (ctx.swapchainImage.IsValid())
            {
                const auto tWaitStart = Clock::now();
                size_t packetIdx = 0;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cvRender.wait(lock, [&] {
                        return m_hasPendingPacket || m_stopping;
                    });
                    if (m_stopping)
                    {
                        ctx.cmd->TransitionTexture(ctx.swapchainImage, rhi::ImageLayout::Undefined,
                                                   rhi::ImageLayout::PresentSrc);
                        m_device->EndFrame(ctx);
                        continue;
                    }
                    packetIdx = m_pendingIndex;
                }
                const auto tWaitEnd = Clock::now();
                waitPacketMs = ms(tWaitStart, tWaitEnd);

                const auto tRenderStart = Clock::now();
                FramePacket& packet = m_packets[packetIdx];
                const rhi::Extent2D extent = m_device->Swapchain().GetSwapchainExtent(m_swapchain);
                rhi::CommandBuffer& cmd = *ctx.cmd;

                cmd.TransitionTexture(ctx.swapchainImage, rhi::ImageLayout::Undefined,
                                      rhi::ImageLayout::ColorAttachment);
                m_ui->RenderFrame(packet, cmd, ctx.frameIndex,
                                  m_device->Resources().GetDefaultView(ctx.swapchainImage), extent,
                                  rhi::Color{0.f, 0.f, 0.f, 1.f});
                cmd.TransitionTexture(ctx.swapchainImage, rhi::ImageLayout::ColorAttachment,
                                      rhi::ImageLayout::PresentSrc);
                const auto tRenderEnd = Clock::now();
                renderFrameMs = ms(tRenderStart, tRenderEnd);

                const auto tPresentStart = Clock::now();
                m_device->EndFrame(ctx);
                const auto tPresentEnd = Clock::now();
                presentMs = ms(tPresentStart, tPresentEnd);

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_hasPendingPacket = false;
                    m_hasFrameRequest = false;
                    m_cvUi.notify_all();

                    m_accumStats.acquireMs += acquireMs;
                    m_accumStats.waitPacketMs += waitPacketMs;
                    m_accumStats.renderFrameMs += renderFrameMs;
                    m_accumStats.presentMs += presentMs;
                    ++m_accumStats.frameCount;
                }
            }
            else
            {
                const auto tPresentStart = Clock::now();
                m_device->EndFrame(ctx);
                const auto tPresentEnd = Clock::now();
                presentMs = ms(tPresentStart, tPresentEnd);

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_accumStats.acquireMs += acquireMs;
                    m_accumStats.presentMs += presentMs;
                }

                // Nothing presented (minimized / zero-sized surface / timeout); throttle instead of spinning.
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }

            if (ctx.swapchainOutOfDate)
            {
                const rhi::Extent2D newExtent = m_getWindowExtent ? m_getWindowExtent() : m_swapchainExtent;
                if (newExtent.width > 0 && newExtent.height > 0)
                {
                    m_device->Swapchain().RecreateSwapchain(m_swapchain, newExtent);
                    m_swapchainExtent = newExtent;
                }
            }
        }

        if (m_swapchain.IsValid())
        {
            m_device->Swapchain().DestroySwapchain(m_swapchain);
            m_swapchain = {};
        }
    }
}

#include <gtest/gtest.h>

#include <EmbeddedShaders.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/SvgImage.h>
#include <lumora/rhi/RHI.h>
#include <lumora/rhi/RHIPainter.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace rhi = lumora::rhi;
using namespace lumora;

namespace
{
    constexpr std::uint32_t kW = 1080;
    constexpr std::uint32_t kH = 1920;

    // A radial-gradient blob drawn at a non-integer scale (the case scenekit hit).
    constexpr const char* kWax =
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="-176 -172 352 352"><defs><radialGradient id="g0" gradientUnits="userSpaceOnUse" cx="-50" cy="-60" r="260"><stop offset="0" stop-color="#b5233a"/><stop offset="0.5" stop-color="#8e1424"/><stop offset="1" stop-color="#5e0b17"/></radialGradient></defs><path d="M 0 -170 C 95 -175 168 -100 172 -10 C 176 80 110 165 5 172 C -90 178 -170 110 -174 10 C -178 -85 -100 -165 0 -170 Z" fill="url(#g0)"/></svg>)svg";

    // Renders `svg` into a transparent RGBA8 target under `scale` + (tx, ty) and returns the pixels.
    std::vector<std::uint8_t> Render(const SvgImage& svg, float scale, float tx, float ty)
    {
        rhi::RHI device;
        device.Init({.adapterIndex = -1, .framesInFlight = 1});
        std::vector<std::uint8_t> out;
        {
            RHIPainter painter;
            painter.Init(device);
            const auto loader = [](std::string_view name) {
                const std::span<const std::byte> s = EmbeddedSpirv(name);
                return std::vector<std::byte>(s.begin(), s.end());
            };
            EXPECT_TRUE(painter.LoadPipelines("", rhi::Format::R8G8B8A8_Unorm, loader));

            rhi::TextureDescription td{};
            td.extent = {kW, kH, 1};
            td.format = rhi::Format::R8G8B8A8_Unorm;
            td.usage  = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc;
            const rhi::TextureHandle tex = device.Resources().CreateTexture(td);
            const rhi::BufferHandle  buf = device.Resources().CreateBuffer(
                {.size = std::uint64_t{kW} * kH * 4, .usage = rhi::BufferUsage::TransferDst, .memory = rhi::MemoryPattern::GpuToCpu});

            rhi::FrameContext ctx = device.BeginFrame({});
            ctx.cmd->TransitionTexture(tex, rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment);
            Canvas& canvas = painter.BeginFrame(*ctx.cmd, ctx.frameIndex, device.Resources().GetDefaultView(tex), {kW, kH},
                                                rhi::Color{0, 0, 0, 0});
            const float m[9] = {scale, 0.f, 0.f, 0.f, scale, 0.f, tx, ty, 1.f};
            canvas.PushLayer(1.f, m);
            svg.Draw(canvas, svg.ViewBox(), 1.f, scale);
            canvas.PopLayer();
            painter.EndFrame();
            ctx.cmd->TransitionTexture(tex, rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::TransferSrc);
            rhi::BufferImageCopy region{};
            region.imageExtent = {kW, kH, 1};
            ctx.cmd->CopyImageToBuffer(tex, rhi::ImageLayout::TransferSrc, buf, std::span(&region, 1));
            device.EndFrame(ctx);
            device.WaitIdle();
            device.Resources().InvalidateMapped(buf, 0, std::uint64_t{kW} * kH * 4);

            const auto* px = static_cast<const std::uint8_t*>(device.Resources().GetMappedPointer(buf));
            out.assign(px, px + std::size_t{kW} * kH * 4);
            device.Resources().Destroy(buf);
            device.Resources().Destroy(tex);
            painter.Shutdown();
        }
        device.Shutdown();
        return out;
    }

    // Pixels that are (nearly) empty while both left/right or both up/down neighbours are opaque.
    int CountCracks(const std::vector<std::uint8_t>& px)
    {
        const auto a = [&](std::uint32_t x, std::uint32_t y) { return px[(std::size_t{y} * kW + x) * 4 + 3]; };
        int n = 0;
        for (std::uint32_t y = 1; y + 1 < kH; ++y)
        {
            for (std::uint32_t x = 1; x + 1 < kW; ++x)
            {
                if (a(x, y) < 250
                    && ((a(x - 1, y) == 255 && a(x + 1, y) == 255) || (a(x, y - 1) == 255 && a(x, y + 1) == 255)))
                {
                    ++n;
                }
            }
        }
        return n;
    }
} // namespace

TEST(SvgRaster, GradientFillHasNoCracks)
{
    if (rhi::EnumerateAdapters().empty())
    {
        GTEST_SKIP() << "No Vulkan adapters available";
    }
    const SvgImage svg = SvgImage::FromText(kWax);
    ASSERT_TRUE(svg.Valid());
    const std::vector<std::uint8_t> px = Render(svg, 1.06f, 560.f, 820.f);
    ASSERT_EQ(px.size(), std::size_t{kW} * kH * 4);
    EXPECT_EQ(px[(std::size_t{820} * kW + 560) * 4 + 3], 255) << "shape centre not drawn";
    EXPECT_EQ(CountCracks(px), 0);
}

#include <gtest/gtest.h>
#include <lumora/rhi/Lumora.h>
#include <lumora/rhi/RHI.h>

namespace rhi = lumora::rhi;
using namespace lumora;

namespace
{
    rhi::TextureViewHandle MakeView(rhi::RHI& device, rhi::TextureHandle& out)
    {
        out = device.Resources().CreateTexture({
            .type = rhi::TextureType::e2D,
            .extent = {8u, 8u, 1u},
            .format = rhi::Format::R8G8B8A8_Unorm,
            .usage = rhi::TextureUsage::Sampled,
            .debugName = "SharedImageTests",
        });
        return device.Resources().GetDefaultView(out);
    }
}

TEST(SharedImageTests, HandlesAreSharedAndSurviveTheSecondInstance)
{
    if (rhi::EnumerateAdapters().empty())
    {
        GTEST_SKIP() << "No adapter available";
    }

    rhi::RHI device;
    device.Init({.adapterIndex = 0, .enableValidation = true, .framesInFlight = 2});

    rhi::TextureHandle ta, tb, tc;
    const rhi::TextureViewHandle va = MakeView(device, ta);
    const rhi::TextureViewHandle vb = MakeView(device, tb);
    const rhi::TextureViewHandle vc = MakeView(device, tc);

    lumora::Ref<Lumora> primary = MakeLumora({.rhi = &device, .spirvDir = "."});
    lumora::Ref<Lumora> secondary = MakeLumora({.rhi = &device, .spirvDir = ".", .shareImagesWith = primary.Get()});

    const ImageHandle a = primary->RegisterExternalImage(va, 8, 8);
    const ImageHandle b = secondary->RegisterExternalImage(vb, 8, 8);
    EXPECT_TRUE(a.Valid());
    EXPECT_TRUE(b.Valid());
    EXPECT_EQ(b.id, a.id + 1);

    secondary = nullptr;
    const ImageHandle c = primary->RegisterExternalImage(vc, 8, 8);
    EXPECT_EQ(c.id, b.id + 1);

    primary->ReleaseImage(a);
    primary = nullptr;
    device.WaitIdle();
    device.Resources().Destroy(tc);
    device.Resources().Destroy(tb);
    device.Resources().Destroy(ta);

    device.Shutdown();
}

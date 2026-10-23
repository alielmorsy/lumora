#include <gtest/gtest.h>

#include <lumora/platform/SystemMetrics.h>
#include <lumora/rhi/Lumora.h>
#include <lumora/rhi/RHI.h>
#include <lumora/Widgets/Widgets.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace rhi = lumora::rhi;
using namespace lumora;

namespace
{
    constexpr std::uint32_t kW = 800;
    constexpr std::uint32_t kH = 600;

    [[nodiscard]] Elem Tiles(const Signal<Color>& fill)
    {
        std::vector<Elem> kids;
        for (int i = 0; i < 20; ++i)
        {
            BoxCfg cfg{};
            cfg.layout = {.width = Expand, .height = Fixed(10.f)};
            if (i == 0)
            {
                cfg.style.fill = fill;
            }
            else
            {
                cfg.style.fill = Color::Hex(0xFF202027);
            }
            kids.push_back(Box(cfg));
        }
        return Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}}, kids);
    }

    void Frame(Lumora& ui, FramePacket& packet, std::uint32_t w = kW, std::uint32_t h = kH)
    {
        ui.Update(1.f / 60.f);
        ui.Layout(static_cast<float>(w), static_cast<float>(h));
        ui.RecordFrame(packet, {w, h});
    }

    [[nodiscard]] bool LoadDefaultFont(Lumora& ui)
    {
        std::ifstream file(platform::GetDefaultUiFont().regular, std::ios::binary);
        if (!file)
        {
            return false;
        }
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto* data = reinterpret_cast<const std::byte*>(bytes.data());
        if (ui.Fonts().RegisterFont({data, bytes.size()}, {.family = "Sans"}) == FaceId::Invalid)
        {
            return false;
        }
        ui.Fonts().SetDefaultFamily("Sans");
        return true;
    }

    // Applies a delta to a CPU copy of the plane, the way the painter applies it to the texture.
    void ApplyDelta(std::vector<std::uint8_t>& plane, int& width, int& height, const AtlasDelta& d)
    {
        if (d.version == 0)
        {
            return;
        }
        const std::size_t stride = static_cast<std::size_t>(d.width) * (d.format == AtlasFormat::R8 ? 1u : 4u);
        if (d.width != width || d.height != height)
        {
            width = d.width;
            height = d.height;
            plane.assign(stride * static_cast<std::size_t>(height), 0);
        }
        ASSERT_EQ(d.rows.size(), stride * static_cast<std::size_t>(d.y1 - d.y0));
        std::memcpy(plane.data() + stride * static_cast<std::size_t>(d.y0), d.rows.data(), d.rows.size());
    }
} // namespace

TEST(FramePacketTests, HeadlessRecordFrameFillsThePacket)
{
    Ref<Lumora> ui = MakeLumora({});
    const Signal<Color> fill{Color::Hex(0xFF3A80FF)};
    ui->Mount([fill] { return Tiles(fill); });

    FramePacket packet;
    Frame(*ui, packet);
    EXPECT_FALSE(packet.reuse);
    EXPECT_FALSE(packet.list.Empty());
    EXPECT_EQ(packet.extent.width, kW);
    EXPECT_EQ(packet.extent.height, kH);
    EXPECT_EQ(packet.contentScale, 1.f);
    EXPECT_EQ(packet.seq, 1u);
    EXPECT_TRUE(packet.ops.Empty());

    Frame(*ui, packet);
    EXPECT_EQ(packet.seq, 2u);
}

TEST(FramePacketTests, PacketMatchesPaintInto)
{
    Ref<Lumora> ui = MakeLumora({});
    const Signal<Color> fill{Color::Hex(0xFF3A80FF)};
    ui->Mount([fill] { return Tiles(fill); });
    ui->Mount([fill] { return Tiles(fill); });

    FramePacket packet;
    Frame(*ui, packet);
    ASSERT_FALSE(packet.reuse);

    // Nothing changed since, so PaintInto republishes exactly what the packet holds.
    DisplayList reference;
    DisplayListCanvas canvas(reference);
    ui->PaintInto(canvas);
    EXPECT_EQ(packet.list.Size(), reference.Size());
    EXPECT_TRUE(std::ranges::equal(packet.list.Bytes(), reference.Bytes()));
    EXPECT_EQ(packet.list.RecordCount(), reference.RecordCount());
}

TEST(FramePacketTests, IdleFrameReusesAndADirtyNodeDoesNot)
{
    Ref<Lumora> ui = MakeLumora({});
    const Signal<Color> fill{Color::Hex(0xFF3A80FF)};
    MountedApp* app = ui->Mount([fill] { return Tiles(fill); });

    FramePacket packet;
    Frame(*ui, packet);
    EXPECT_FALSE(packet.reuse);
    const std::uint32_t fullSize = packet.list.Size();

    Frame(*ui, packet);
    EXPECT_TRUE(packet.reuse);
    EXPECT_TRUE(packet.list.Empty());
    EXPECT_EQ(app->Counters().paintCanvasReused, 1u);

    app->RunScoped([fill] { fill = Color::Hex(0xFFFF0000); });
    Frame(*ui, packet);
    EXPECT_FALSE(packet.reuse);
    EXPECT_EQ(packet.list.Size(), fullSize);
}

TEST(FramePacketTests, ContentScaleRidesThePacket)
{
    Ref<Lumora> ui = MakeLumora({});
    const Signal<Color> fill{Color::Hex(0xFF3A80FF)};
    ui->Mount([fill] { return Tiles(fill); });

    FramePacket packet;
    Frame(*ui, packet);
    Frame(*ui, packet);
    ASSERT_TRUE(packet.reuse);

    ui->SetContentScale(2.f);
    Frame(*ui, packet);
    EXPECT_EQ(packet.contentScale, 2.f);
    EXPECT_FALSE(packet.reuse);
}

TEST(FramePacketTests, AtlasDeltasMirrorTheGlyphAtlas)
{
    Ref<Lumora> ui = MakeLumora({});
    if (!LoadDefaultFont(*ui))
    {
        GTEST_SKIP() << "No default UI font available";
    }
    const Signal<std::string> caption{std::string("hello")};
    MountedApp* app = ui->Mount([caption] { return Text(caption, TextCfg{.size = 16.f}); });

    std::vector<std::uint8_t> mirror;
    int width = 0;
    int height = 0;

    FramePacket packet;
    Frame(*ui, packet);
    const AtlasDelta& coverage = packet.atlas[0];
    ASSERT_NE(coverage.version, 0u);
    EXPECT_EQ(coverage.y0, 0);
    EXPECT_EQ(coverage.y1, coverage.height);
    ApplyDelta(mirror, width, height, coverage);

    Frame(*ui, packet);
    EXPECT_EQ(packet.atlas[0].version, 0u);
    EXPECT_EQ(packet.atlas[1].version, 0u);

    app->RunScoped([caption] { caption = std::string("QUIZ jackdaws 0123456789"); });
    Frame(*ui, packet);
    ASSERT_NE(coverage.version, 0u);
    if (coverage.height == height)
    {
        EXPECT_LT(coverage.y1 - coverage.y0, coverage.height); // only the new glyphs' rows
    }
    else
    {
        EXPECT_EQ(coverage.y1 - coverage.y0, coverage.height); // growth sends the whole plane
    }
    ApplyDelta(mirror, width, height, coverage);

    const AtlasView live = ui->Fonts().Atlas(AtlasKind::Coverage);
    ASSERT_EQ(live.width, width);
    ASSERT_EQ(live.height, height);
    EXPECT_EQ(std::memcmp(mirror.data(), live.pixels, mirror.size()), 0);
}

TEST(FramePacketTests, ImageOpsApplyOnTheRenderSide)
{
    if (rhi::EnumerateAdapters().empty())
    {
        GTEST_SKIP() << "No adapter available";
    }

    rhi::RHI device;
    device.Init({.adapterIndex = 0, .enableValidation = true, .framesInFlight = 2});
    {
        RHIPainter painter;
        painter.Init(device);

        const std::vector<std::byte> pixels(4 * 4 * 4, std::byte{255});
        const ImageHandle a = painter.UploadTexture(pixels, 4, 4, AtlasFormat::Rgba8888);
        const ImageHandle b = painter.UploadTexture(pixels, 4, 4, AtlasFormat::Rgba8888);
        ASSERT_TRUE(a.Valid());
        EXPECT_EQ(b.id, a.id + 1);
        EXPECT_FALSE(painter.ResolveImage(a).IsValid());

        PainterOps ops;
        painter.TakePendingOps(ops);
        EXPECT_EQ(ops.uploads.size(), 2u);
        painter.ApplyOps(ops);
        EXPECT_TRUE(ops.Empty());
        EXPECT_TRUE(painter.ResolveImage(a).IsValid());
        EXPECT_TRUE(painter.ResolveImage(b).IsValid());

        painter.FreeTexture(a);
        EXPECT_TRUE(painter.ResolveImage(a).IsValid()); // queued, not applied yet
        painter.TakePendingOps(ops);
        EXPECT_EQ(ops.frees.size(), 1u);

        // A packet that was never applied keeps its ops: the next take appends.
        const ImageHandle c = painter.UploadTexture(pixels, 4, 4, AtlasFormat::Rgba8888);
        EXPECT_EQ(c.id, b.id + 1);
        painter.TakePendingOps(ops);
        EXPECT_EQ(ops.frees.size(), 1u);
        EXPECT_EQ(ops.uploads.size(), 1u);

        painter.ApplyOps(ops);
        EXPECT_FALSE(painter.ResolveImage(a).IsValid());
        EXPECT_TRUE(painter.ResolveImage(b).IsValid());
        EXPECT_TRUE(painter.ResolveImage(c).IsValid());

        device.WaitIdle();
        painter.Shutdown();
    }
    device.Shutdown();
}
